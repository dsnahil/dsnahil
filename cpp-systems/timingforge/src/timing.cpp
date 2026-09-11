#include "timingforge/timing.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <exception>
#include <functional>
#include <iomanip>
#include <istream>
#include <limits>
#include <mutex>
#include <ostream>
#include <queue>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <unordered_set>

namespace timingforge {
namespace {
constexpr double inf = std::numeric_limits<double>::infinity();
void valid_window(Window w, bool nonnegative = false) {
    if (!std::isfinite(w.early) || !std::isfinite(w.late) || w.early > w.late ||
        (nonnegative && w.early < 0)) throw std::invalid_argument("invalid timing window");
}
void valid_name(const std::string& s) {
    if (s.empty() || !std::all_of(s.begin(), s.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
               (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '/';
    })) throw std::invalid_argument("invalid node name: " + s);
}
void end_line(std::istringstream& line) {
    std::string extra;
    if (line >> extra) throw std::invalid_argument("unexpected token: " + extra);
}
}

Graph::Graph(std::vector<Node> nodes, std::vector<Arc> arcs)
    : nodes_(std::move(nodes)), arcs_(std::move(arcs)) {
    if (nodes_.empty() || nodes_.size() > std::numeric_limits<NodeId>::max() ||
        arcs_.size() > std::numeric_limits<ArcId>::max())
        throw std::invalid_argument("graph size outside supported range");
    const auto n = nodes_.size();
    bool has_output = false;
    for (std::size_t i = 0; i < n; ++i) {
        const auto& node = nodes_[i];
        valid_name(node.name);
        if (!ids_.emplace(node.name, static_cast<NodeId>(i)).second)
            throw std::invalid_argument("duplicate node: " + node.name);
        if (node.input) valid_window(*node.input);
        if (node.output) { valid_window(*node.output); has_output = true; }
    }
    if (!has_output) throw std::invalid_argument("at least one constrained output required");
    in_offsets_.resize(n + 1); out_offsets_.resize(n + 1);
    std::unordered_set<std::uint64_t> pairs;
    for (const auto& arc : arcs_) {
        if (arc.from >= n || arc.to >= n) throw std::invalid_argument("arc node out of range");
        valid_window(arc.delay, true);
        const auto key = (static_cast<std::uint64_t>(arc.from) << 32U) | arc.to;
        if (!pairs.insert(key).second) throw std::invalid_argument("duplicate arc");
        ++out_offsets_[arc.from + 1]; ++in_offsets_[arc.to + 1];
    }
    for (std::size_t i = 1; i <= n; ++i) {
        in_offsets_[i] += in_offsets_[i - 1]; out_offsets_[i] += out_offsets_[i - 1];
    }
    in_arcs_.resize(arcs_.size()); out_arcs_.resize(arcs_.size());
    auto ic = in_offsets_, oc = out_offsets_;
    for (std::size_t i = 0; i < arcs_.size(); ++i) {
        in_arcs_[ic[arcs_[i].to]++] = static_cast<ArcId>(i);
        out_arcs_[oc[arcs_[i].from]++] = static_cast<ArcId>(i);
    }
    std::vector<std::size_t> degree(n);
    std::queue<NodeId> ready;
    for (std::size_t i = 0; i < n; ++i) {
        degree[i] = incoming(static_cast<NodeId>(i)).size();
        if (nodes_[i].input && degree[i] != 0) throw std::invalid_argument("input has incoming arc");
        if (degree[i] == 0) {
            if (!nodes_[i].input) throw std::invalid_argument("unconstrained source: " + nodes_[i].name);
            ready.push(static_cast<NodeId>(i));
        }
    }
    rank_.resize(n);
    while (!ready.empty()) {
        const auto v = ready.front(); ready.pop();
        rank_[v] = order_.size(); order_.push_back(v);
        for (const auto a : outgoing(v)) if (--degree[arcs_[a].to] == 0) ready.push(arcs_[a].to);
    }
    if (order_.size() != n) throw std::invalid_argument("combinational cycle detected");
}
std::span<const ArcId> Graph::incoming(NodeId id) const {
    const auto first = in_offsets_.at(id), last = in_offsets_.at(static_cast<std::size_t>(id) + 1);
    return std::span<const ArcId>(in_arcs_).subspan(first, last - first);
}
std::span<const ArcId> Graph::outgoing(NodeId id) const {
    const auto first = out_offsets_.at(id), last = out_offsets_.at(static_cast<std::size_t>(id) + 1);
    return std::span<const ArcId>(out_arcs_).subspan(first, last - first);
}
NodeId Graph::find(const std::string& name) const {
    const auto it = ids_.find(name);
    if (it == ids_.end()) throw std::invalid_argument("unknown node: " + name);
    return it->second;
}
Engine::Engine(std::shared_ptr<const Graph> graph, double scale) : graph_(std::move(graph)) {
    if (!graph_ || !std::isfinite(scale) || scale <= 0) throw std::invalid_argument("invalid graph or corner scale");
    for (const auto& arc : graph_->arcs()) {
        Window w{arc.delay.early * scale, arc.delay.late * scale};
        valid_window(w, true); delays_.push_back(w);
    }
    const auto n = graph_->nodes().size();
    arrival_.resize(n, {inf, -inf}); required_.resize(n);
    early_pred_.resize(n); late_pred_.resize(n);
}
bool Engine::recompute(NodeId id) {
    Window w{inf, -inf};
    std::optional<ArcId> ep, lp;
    if (graph_->nodes()[id].input) w = *graph_->nodes()[id].input;
    for (const auto a : graph_->incoming(id)) {
        const auto from = graph_->arcs()[a].from;
        const double early = arrival_[from].early + delays_[a].early;
        const double late = arrival_[from].late + delays_[a].late;
        if (!std::isfinite(early) || !std::isfinite(late)) throw std::overflow_error("arrival time overflow");
        if (early < w.early || (early == w.early && (!ep || a < *ep))) { w.early = early; ep = a; }
        if (late > w.late || (late == w.late && (!lp || a < *lp))) { w.late = late; lp = a; }
    }
    const bool changed = w.early != arrival_[id].early || w.late != arrival_[id].late;
    arrival_[id] = w; early_pred_[id] = ep; late_pred_[id] = lp;
    return changed;
}
void Engine::backward() {
    std::fill(required_.begin(), required_.end(), Window{-inf, inf});
    for (auto it = graph_->order().rbegin(); it != graph_->order().rend(); ++it) {
        const auto v = *it;
        if (graph_->nodes()[v].output) required_[v] = *graph_->nodes()[v].output;
        for (const auto a : graph_->outgoing(v)) {
            const auto to = graph_->arcs()[a].to;
            if (std::isfinite(required_[to].early)) {
                const double r = required_[to].early - delays_[a].early;
                if (!std::isfinite(r)) throw std::overflow_error("required time overflow");
                required_[v].early = std::max(required_[v].early, r);
            }
            if (std::isfinite(required_[to].late)) {
                const double r = required_[to].late - delays_[a].late;
                if (!std::isfinite(r)) throw std::overflow_error("required time overflow");
                required_[v].late = std::min(required_[v].late, r);
            }
        }
    }
}
Result Engine::analyze() {
    initialized_ = false;
    for (const auto v : graph_->order()) recompute(v);
    backward(); initialized_ = true;
    return result(graph_->nodes().size());
}
Result Engine::update(std::span<const Update> updates) {
    std::unordered_set<ArcId> seen;
    for (const auto& u : updates) {
        if (u.arc >= delays_.size() || !seen.insert(u.arc).second)
            throw std::invalid_argument("unknown or duplicate ECO arc id");
        valid_window(u.delay, true);
    }
    if (!initialized_) analyze();
    auto compare = [this](NodeId a, NodeId b) { return graph_->rank(a) > graph_->rank(b); };
    std::priority_queue<NodeId, std::vector<NodeId>, decltype(compare)> queue(compare);
    std::vector<bool> queued(graph_->nodes().size());
    auto enqueue = [&](NodeId v) { if (!queued[v]) { queue.push(v); queued[v] = true; } };
    // All allocations for rollback state precede mutation: overflow during propagation
    // restores a fully usable engine, including predecessor choices and required times.
    auto old_delays = delays_, old_arrival = arrival_, old_required = required_;
    auto old_ep = early_pred_, old_lp = late_pred_;
    try {
        for (const auto& u : updates) { delays_[u.arc] = u.delay; enqueue(graph_->arcs()[u.arc].to); }
        std::size_t visited = 0;
        while (!queue.empty()) {
            const auto v = queue.top(); queue.pop(); ++visited;
            if (recompute(v)) for (const auto a : graph_->outgoing(v)) enqueue(graph_->arcs()[a].to);
        }
        backward(); // Deliberately full reverse pass: simpler and exact.
        return result(visited);
    } catch (...) {
        delays_.swap(old_delays); arrival_.swap(old_arrival); required_.swap(old_required);
        early_pred_.swap(old_ep); late_pred_.swap(old_lp);
        throw;
    }
}
std::vector<NodeId> Engine::path(NodeId id, bool late) const {
    std::vector<NodeId> p;
    const auto& pred = late ? late_pred_ : early_pred_;
    for (;;) {
        p.push_back(id);
        if (!pred[id]) break;
        id = graph_->arcs()[*pred[id]].from;
    }
    std::reverse(p.begin(), p.end()); return p;
}
Result Engine::result(std::size_t visited) const {
    Result r{{}, inf, inf, visited};
    for (std::size_t i = 0; i < graph_->nodes().size(); ++i) {
        const auto& node = graph_->nodes()[i];
        if (!node.output) continue;
        const auto id = static_cast<NodeId>(i);
        const double hold = arrival_[i].early - node.output->early;
        const double setup = node.output->late - arrival_[i].late;
        if (!std::isfinite(hold) || !std::isfinite(setup)) throw std::overflow_error("slack overflow");
        r.endpoints.push_back({id, arrival_[i].early, arrival_[i].late, hold, setup, path(id, false), path(id, true)});
        r.worst_hold_slack = std::min(r.worst_hold_slack, hold);
        r.worst_setup_slack = std::min(r.worst_setup_slack, setup);
    }
    return r;
}
std::shared_ptr<const Graph> parse(std::istream& in) {
    std::vector<Node> nodes;
    struct RawArc { std::string from, to; Window delay; };
    std::vector<RawArc> raw;
    std::unordered_map<std::string, NodeId> ids;
    std::string text; std::size_t line_no = 0;
    while (std::getline(in, text)) {
        ++line_no; text = text.substr(0, text.find('#'));
        std::istringstream line(text); std::string type;
        if (!(line >> type)) continue;
        try {
            if (type == "arc") {
                RawArc a;
                if (!(line >> a.from >> a.to >> a.delay.early >> a.delay.late)) throw std::invalid_argument("expected arc FROM TO MIN MAX");
                raw.push_back(a);
            } else if (type == "node" || type == "input" || type == "output") {
                Node n;
                if (!(line >> n.name)) throw std::invalid_argument("expected node name");
                if (type != "node") {
                    Window w;
                    if (!(line >> w.early >> w.late)) throw std::invalid_argument("expected min and max times");
                    if (type == "input") n.input = w; else n.output = w;
                }
                if (nodes.size() >= std::numeric_limits<NodeId>::max()) throw std::invalid_argument("too many nodes");
                if (!ids.emplace(n.name, static_cast<NodeId>(nodes.size())).second) throw std::invalid_argument("duplicate node");
                nodes.push_back(std::move(n));
            } else throw std::invalid_argument("unknown directive: " + type);
            end_line(line);
        } catch (const std::exception& e) { throw std::invalid_argument("line " + std::to_string(line_no) + ": " + e.what()); }
    }
    if (in.bad()) throw std::runtime_error("netlist read failed");
    std::vector<Arc> arcs;
    for (const auto& a : raw) {
        if (!ids.contains(a.from) || !ids.contains(a.to)) throw std::invalid_argument("arc references undeclared node");
        arcs.push_back({ids.at(a.from), ids.at(a.to), a.delay});
    }
    return std::make_shared<const Graph>(std::move(nodes), std::move(arcs));
}
std::vector<Update> parse_updates(std::istream& in) {
    std::vector<Update> result;
    std::string text;
    while (std::getline(in, text)) {
        text = text.substr(0, text.find('#'));
        std::istringstream line(text); std::string type;
        if (!(line >> type)) continue;
        Update u; std::uint64_t id;
        if (type != "set" || !(line >> id >> u.delay.early >> u.delay.late) || id > std::numeric_limits<ArcId>::max())
            throw std::invalid_argument("expected set ARC_ID MIN MAX");
        u.arc = static_cast<ArcId>(id); valid_window(u.delay, true); end_line(line); result.push_back(u);
    }
    if (in.bad()) throw std::runtime_error("ECO read failed");
    return result;
}
std::vector<Result> analyze_corners(std::shared_ptr<const Graph> graph,
                                  std::span<const double> scales, std::size_t workers) {
    if (workers == 0 || workers > 256) throw std::invalid_argument("workers must be in [1, 256]");
    for (double s : scales) if (!std::isfinite(s) || s <= 0) throw std::invalid_argument("invalid corner scale");
    std::vector<Result> results(scales.size());
    std::atomic<std::size_t> next{0};
    std::mutex mutex; std::exception_ptr failure;
    {
        std::vector<std::jthread> pool;
        for (std::size_t w = 0; w < std::min(workers, scales.size()); ++w) {
            pool.emplace_back([&] {
                try {
                    for (;;) {
                        const auto i = next.fetch_add(1, std::memory_order_relaxed);
                        if (i >= scales.size()) break;
                        results[i] = Engine(graph, scales[i]).analyze();
                    }
                } catch (...) { std::lock_guard lock(mutex); if (!failure) failure = std::current_exception(); }
            });
        }
    } // jthread joins before results or exception state is read.
    if (failure) std::rethrow_exception(failure);
    return results;
}
void write_json(std::ostream& out, const Graph& graph, const Result& result) {
    out << std::setprecision(17) << "{\"worst_setup_slack_ns\":" << result.worst_setup_slack
        << ",\"worst_hold_slack_ns\":" << result.worst_hold_slack
        << ",\"forward_nodes_visited\":" << result.forward_nodes_visited << ",\"endpoints\":[";
    bool first = true;
    for (const auto& e : result.endpoints) {
        if (!first) out << ',';
        first = false;
        out << "{\"node\":\"" << graph.nodes()[e.node].name << "\",\"earliest_ns\":" << e.earliest
            << ",\"latest_ns\":" << e.latest << ",\"hold_slack_ns\":" << e.hold_slack
            << ",\"setup_slack_ns\":" << e.setup_slack;
        auto path_json = [&](const char* name, const std::vector<NodeId>& p) {
            out << ",\"" << name << "\":[";
            for (std::size_t i = 0; i < p.size(); ++i) { if (i) out << ','; out << '"' << graph.nodes()[p[i]].name << '"'; }
            out << ']';
        };
        path_json("early_path", e.early_path); path_json("late_path", e.late_path); out << '}';
    }
    out << "]}";
    if (!out) throw std::runtime_error("JSON write failed");
}
} // namespace timingforge
