#pragma once
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace timingforge {
using NodeId = std::uint32_t;
using ArcId = std::uint32_t;
struct Window { double early; double late; };
struct Node {
    std::string name;
    std::optional<Window> input;
    std::optional<Window> output;
};
struct Arc { NodeId from; NodeId to; Window delay; };

// Immutable after construction. CSR adjacency avoids one allocation per node.
class Graph {
public:
    Graph(std::vector<Node> nodes, std::vector<Arc> arcs);
    [[nodiscard]] const std::vector<Node>& nodes() const noexcept { return nodes_; }
    [[nodiscard]] const std::vector<Arc>& arcs() const noexcept { return arcs_; }
    [[nodiscard]] std::span<const ArcId> incoming(NodeId id) const;
    [[nodiscard]] std::span<const ArcId> outgoing(NodeId id) const;
    [[nodiscard]] const std::vector<NodeId>& order() const noexcept { return order_; }
    [[nodiscard]] std::size_t rank(NodeId id) const { return rank_.at(id); }
    [[nodiscard]] NodeId find(const std::string& name) const;
private:
    std::vector<Node> nodes_;
    std::vector<Arc> arcs_;
    std::unordered_map<std::string, NodeId> ids_;
    std::vector<std::size_t> in_offsets_, out_offsets_, rank_;
    std::vector<ArcId> in_arcs_, out_arcs_;
    std::vector<NodeId> order_;
};

struct Endpoint {
    NodeId node;
    double earliest, latest, hold_slack, setup_slack;
    std::vector<NodeId> early_path, late_path;
};
struct Result {
    std::vector<Endpoint> endpoints;
    double worst_setup_slack, worst_hold_slack;
    std::size_t forward_nodes_visited;
};
struct Update { ArcId arc; Window delay; };

// Each engine owns its arrival/required state; shared graphs are read-only.
// Engine mutations are single-threaded. Parallel corners use independent engines.
class Engine {
public:
    explicit Engine(std::shared_ptr<const Graph> graph, double scale = 1.0);
    Result analyze();
    Result update(std::span<const Update> updates);
    [[nodiscard]] const std::vector<Window>& arrivals() const noexcept { return arrival_; }
    [[nodiscard]] const std::vector<Window>& required() const noexcept { return required_; }
private:
    bool recompute(NodeId id);
    void backward();
    Result result(std::size_t visited) const;
    std::vector<NodeId> path(NodeId id, bool late) const;
    std::shared_ptr<const Graph> graph_;
    std::vector<Window> delays_, arrival_, required_;
    std::vector<std::optional<ArcId>> early_pred_, late_pred_;
    bool initialized_ = false;
};

std::shared_ptr<const Graph> parse(std::istream& in);
std::vector<Update> parse_updates(std::istream& in);
std::vector<Result> analyze_corners(std::shared_ptr<const Graph> graph,
                                  std::span<const double> scales, std::size_t workers);
void write_json(std::ostream& out, const Graph& graph, const Result& result);
} // namespace timingforge
