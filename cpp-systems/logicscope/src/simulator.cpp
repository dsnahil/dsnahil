#include "logicscope/simulator.hpp"
#include <algorithm>
#include <charconv>
#include <functional>
#include <istream>
#include <limits>
#include <ostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <unordered_set>

namespace logicscope {
namespace {
bool known(Logic v) { return v == Logic::zero || v == Logic::one; }
Logic invert(Logic v) { return v == Logic::zero ? Logic::one : v == Logic::one ? Logic::zero : Logic::x; }
void valid_name(const std::string& name) {
    if (name.empty() || !std::all_of(name.begin(), name.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
               (c >= '0' && c <= '9') || c == '_';
    })) throw std::invalid_argument("names must contain only letters, digits, or underscores");
}
Tick number(const std::string& s) {
    Tick n; const auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), n);
    if (ec != std::errc{} || p != s.data() + s.size()) throw std::invalid_argument("invalid unsigned time: " + s);
    return n;
}
Operation operation(const std::string& name) {
    static const std::unordered_map<std::string, Operation> ops{
        {"BUF", Operation::buf}, {"NOT", Operation::inv}, {"AND", Operation::land},
        {"OR", Operation::lor}, {"XOR", Operation::lxor}, {"NAND", Operation::nand},
        {"NOR", Operation::nor}, {"XNOR", Operation::xnor}, {"TRI", Operation::tri},
        {"RESOLVE", Operation::resolve}, {"DFF", Operation::dff}};
    if (!ops.contains(name)) throw std::invalid_argument("unknown operation: " + name);
    return ops.at(name);
}
void arity(Operation op, std::size_t size) {
    if ((op == Operation::buf || op == Operation::inv) ? size != 1 :
        (op == Operation::tri || op == Operation::dff) ? size != 2 :
        op == Operation::resolve ? size == 0 : size < 2)
        throw std::invalid_argument("incorrect gate arity");
}
}
Logic parse_logic(char c) {
    switch (c) {
        case '0': return Logic::zero; case '1': return Logic::one;
        case 'x': case 'X': return Logic::x; case 'z': case 'Z': return Logic::z;
        default: throw std::invalid_argument("expected 0, 1, X, or Z");
    }
}
char symbol(Logic value) noexcept {
    switch (value) { case Logic::zero: return '0'; case Logic::one: return '1'; case Logic::z: return 'z'; default: return 'x'; }
}
Logic evaluate(Operation op, std::span<const Logic> in) {
    arity(op, in.size());
    if (op == Operation::dff) throw std::invalid_argument("DFF requires simulator state");
    if (op == Operation::buf) return known(in[0]) ? in[0] : Logic::x;
    if (op == Operation::inv) return invert(in[0]);
    if (op == Operation::tri) {
        if (in[1] == Logic::zero) return Logic::z;
        if (in[1] == Logic::one) return in[0];
        return in[0] == Logic::z ? Logic::z : Logic::x;
    }
    if (op == Operation::resolve) {
        Logic result = Logic::z;
        for (auto v : in) {
            if (v == Logic::z) continue;
            if (v == Logic::x || (result != Logic::z && result != v)) return Logic::x;
            result = v;
        }
        return result;
    }
    const bool and_op = op == Operation::land || op == Operation::nand;
    const bool or_op = op == Operation::lor || op == Operation::nor;
    Logic value;
    if (and_op || or_op) {
        const auto control = and_op ? Logic::zero : Logic::one;
        if (std::find(in.begin(), in.end(), control) != in.end()) value = control;
        else if (!std::all_of(in.begin(), in.end(), known)) value = Logic::x;
        else value = and_op ? Logic::one : Logic::zero;
    } else {
        if (!std::all_of(in.begin(), in.end(), known)) value = Logic::x;
        else value = std::count(in.begin(), in.end(), Logic::one) % 2 ? Logic::one : Logic::zero;
    }
    if (op == Operation::nand || op == Operation::nor || op == Operation::xnor) return invert(value);
    return value;
}
Circuit::Circuit(std::vector<Net> nets, std::vector<Gate> gates)
    : nets_(std::move(nets)), gates_(std::move(gates)), fanout_(nets_.size()) {
    if (nets_.empty() || nets_.size() > std::numeric_limits<NetId>::max() || gates_.size() > std::numeric_limits<GateId>::max())
        throw std::invalid_argument("circuit size outside supported range");
    for (std::size_t i = 0; i < nets_.size(); ++i) {
        valid_name(nets_[i].name);
        if (!names_.emplace(nets_[i].name, static_cast<NetId>(i)).second) throw std::invalid_argument("duplicate net");
    }
    std::vector<bool> driven(nets_.size());
    std::unordered_set<std::string> gate_names;
    for (std::size_t i = 0; i < gates_.size(); ++i) {
        const auto& g = gates_[i]; valid_name(g.name); arity(g.op, g.inputs.size());
        if (!gate_names.insert(g.name).second) throw std::invalid_argument("duplicate gate");
        if (g.output >= nets_.size() || nets_[g.output].input || driven[g.output])
            throw std::invalid_argument("gate output must have exactly one driver and cannot be primary input");
        driven[g.output] = true;
        for (auto n : g.inputs) {
            if (n >= nets_.size()) throw std::invalid_argument("gate input out of range");
            fanout_[n].push_back(static_cast<GateId>(i));
        }
    }
    for (std::size_t i = 0; i < nets_.size(); ++i)
        if (!nets_[i].input && !driven[i]) throw std::invalid_argument("undriven net: " + nets_[i].name);
}
NetId Circuit::find(const std::string& name) const {
    const auto it = names_.find(name);
    if (it == names_.end()) throw std::invalid_argument("unknown net: " + name);
    return it->second;
}
Trace Simulator::run(std::span<const Stimulus> inputs, Tick until, std::size_t max_events, std::uint32_t max_delta) const {
    if (max_events == 0 || max_delta == 0) throw std::invalid_argument("simulation limits must be positive");
    struct Event { Tick time; std::uint32_t delta; std::uint64_t serial; NetId net; Logic value;
                   std::optional<GateId> gate; std::uint64_t generation; };
    auto compare = [](const Event& a, const Event& b) { return std::tie(a.time,a.delta,a.serial) > std::tie(b.time,b.delta,b.serial); };
    std::priority_queue<Event, std::vector<Event>, decltype(compare)> queue(compare);
    struct State { std::uint64_t generation = 0; Logic target = Logic::x; Logic clock = Logic::x; };
    std::vector<State> states(circuit_.gates().size());
    Trace trace; trace.final_values.resize(circuit_.nets().size(), Logic::x);
    std::uint64_t serial = 0;
    auto push = [&](Event e) {
        if (queue.size() >= max_events || serial == std::numeric_limits<std::uint64_t>::max())
            throw std::runtime_error("event queue budget exceeded");
        e.serial = serial++; queue.push(e);
        trace.statistics.peak_queue = std::max(trace.statistics.peak_queue, queue.size());
    };
    std::set<std::pair<Tick,NetId>> unique;
    for (const auto& s : inputs) {
        if (s.net >= circuit_.nets().size() || !circuit_.nets()[s.net].input) throw std::invalid_argument("stimulus must drive a primary input");
        if (!unique.emplace(s.time,s.net).second) throw std::invalid_argument("duplicate stimulus for same net and time");
        if (s.time <= until) push({s.time,0,0,s.net,s.value,std::nullopt,0});
    }
    auto request = [&](GateId id, Tick time, std::uint32_t delta) {
        const auto& gate = circuit_.gates()[id]; auto& state = states[id];
        std::vector<Logic> values; values.reserve(gate.inputs.size());
        for (auto n : gate.inputs) values.push_back(trace.final_values[n]);
        Logic target;
        if (gate.op == Operation::dff) {
            const auto clock = values[1];
            const bool rising = state.clock == Logic::zero && clock == Logic::one;
            const bool uncertain_rising = (state.clock == Logic::zero && !known(clock)) ||
                                          (!known(state.clock) && clock == Logic::one);
            state.clock = clock;
            if (!rising && !uncertain_rising) return;
            target = rising && known(values[0]) ? values[0] : Logic::x;
        } else target = evaluate(gate.op, values);
        if (target == state.target) return; // Preserve an already pending identical transition.
        state.target = target;
        if (state.generation == std::numeric_limits<std::uint64_t>::max()) throw std::overflow_error("event generation overflow");
        ++state.generation;
        if (target == trace.final_values[gate.output]) return; // Cancel previous pending pulse.
        if (gate.delay > std::numeric_limits<Tick>::max() - time) throw std::overflow_error("simulation time overflow");
        const Tick when = time + gate.delay;
        const std::uint32_t next_delta = gate.delay == 0 ? delta + 1 : 0;
        if (gate.delay == 0 && (delta >= max_delta || delta == std::numeric_limits<std::uint32_t>::max()))
            throw std::runtime_error("delta-cycle budget exceeded: possible zero-delay oscillation");
        if (when <= until) push({when,next_delta,0,gate.output,target,id,state.generation});
    };
    for (std::size_t i = 0; i < circuit_.gates().size(); ++i) request(static_cast<GateId>(i),0,0);
    while (!queue.empty()) {
        const Tick time = queue.top().time; const auto delta = queue.top().delta;
        std::vector<GateId> affected;
        // Apply every transition in this batch before evaluating gates, so the result
        // is independent of input line order at equal physical time and delta.
        while (!queue.empty() && queue.top().time == time && queue.top().delta == delta) {
            const auto e = queue.top(); queue.pop();
            if (++trace.statistics.processed > max_events) throw std::runtime_error("processed-event budget exceeded");
            if (e.gate && e.generation != states[*e.gate].generation) { ++trace.statistics.stale; continue; }
            if (trace.final_values[e.net] == e.value) continue;
            trace.final_values[e.net] = e.value; trace.changes.push_back({time,delta,e.net,e.value});
            const auto& f = circuit_.fanout(e.net); affected.insert(affected.end(),f.begin(),f.end());
        }
        std::sort(affected.begin(),affected.end()); affected.erase(std::unique(affected.begin(),affected.end()),affected.end());
        for (auto id : affected) request(id,time,delta);
    }
    return trace;
}
Circuit parse(std::istream& input) {
    std::vector<Net> nets;
    struct Raw { std::string name, op, output; std::vector<std::string> inputs; Tick delay; };
    std::vector<Raw> raw; std::unordered_map<std::string,NetId> ids;
    std::string text; std::size_t line_no = 0;
    while (std::getline(input,text)) {
        ++line_no; text = text.substr(0,text.find('#')); std::istringstream line(text);
        std::string kind; if (!(line >> kind)) continue;
        try {
            if (kind == "input" || kind == "wire") {
                std::string name, extra;
                if (!(line >> name) || (line >> extra)) throw std::invalid_argument("expected input/wire NAME");
                if (nets.size() >= std::numeric_limits<NetId>::max()) throw std::invalid_argument("too many nets");
                if (!ids.emplace(name, static_cast<NetId>(nets.size())).second) throw std::invalid_argument("duplicate net");
                nets.push_back({name,kind == "input"});
            } else if (kind == "gate") {
                Raw r; std::string delay, name;
                if (!(line >> r.name >> r.op >> r.output >> delay)) throw std::invalid_argument("expected gate NAME OP OUTPUT DELAY INPUT...");
                r.delay = number(delay); while (line >> name) r.inputs.push_back(name); raw.push_back(std::move(r));
            } else throw std::invalid_argument("unknown directive: " + kind);
        } catch (const std::exception& e) { throw std::invalid_argument("line " + std::to_string(line_no) + ": " + e.what()); }
    }
    if (input.bad()) throw std::runtime_error("circuit read failed");
    std::vector<Gate> gates;
    auto find = [&](const std::string& name) {
        if (!ids.contains(name)) throw std::invalid_argument("undeclared net: " + name);
        return ids.at(name);
    };
    for (const auto& r : raw) {
        Gate g{r.name,operation(r.op),find(r.output),{},r.delay};
        for (const auto& n : r.inputs) g.inputs.push_back(find(n));
        gates.push_back(std::move(g));
    }
    return Circuit(std::move(nets),std::move(gates));
}
std::vector<Stimulus> parse_stimulus(std::istream& input, const Circuit& circuit) {
    std::vector<Stimulus> result; std::string text;
    while (std::getline(input,text)) {
        text = text.substr(0,text.find('#')); std::istringstream line(text);
        std::string time, name, value, extra;
        if (!(line >> time)) continue;
        if (!(line >> name >> value) || (line >> extra) || value.size() != 1) throw std::invalid_argument("expected TIME INPUT VALUE");
        result.push_back({number(time),circuit.find(name),parse_logic(value[0])});
    }
    if (input.bad()) throw std::runtime_error("stimulus read failed");
    return result;
}
void write_json(std::ostream& out, const Circuit& circuit, const Trace& trace) {
    out << "{\"processed_events\":" << trace.statistics.processed << ",\"stale_events\":" << trace.statistics.stale
        << ",\"peak_queue\":" << trace.statistics.peak_queue << ",\"final\":{";
    for (std::size_t i = 0; i < circuit.nets().size(); ++i) { if (i) out << ','; out << '"' << circuit.nets()[i].name << "\":\"" << symbol(trace.final_values.at(i)) << '"'; }
    out << "},\"transitions\":[";
    for (std::size_t i = 0; i < trace.changes.size(); ++i) {
        if (i) out << ',';
        const auto& t = trace.changes[i];
        out << "{\"time_ns\":" << t.time << ",\"delta\":" << t.delta << ",\"net\":\"" << circuit.nets()[t.net].name
            << "\",\"value\":\"" << symbol(t.value) << "\"}";
    }
    out << "]}\n";
    if (!out) throw std::runtime_error("JSON write failed");
}
void write_vcd(std::ostream& out, const Circuit& circuit, const Trace& trace) {
    out << "$version LogicScope 1.0 $end\n$timescale 1ns $end\n$scope module top $end\n";
    for (std::size_t i = 0; i < circuit.nets().size(); ++i) out << "$var wire 1 n" << i << ' ' << circuit.nets()[i].name << " $end\n";
    out << "$upscope $end\n$enddefinitions $end\n#0\n$dumpvars\n";
    for (std::size_t i = 0; i < circuit.nets().size(); ++i) out << "xn" << i << '\n';
    out << "$end\n"; Tick now = 0;
    for (const auto& t : trace.changes) {
        if (t.time != now) { now = t.time; out << '#' << now << '\n'; }
        out << symbol(t.value) << 'n' << t.net << '\n';
    }
    if (!out) throw std::runtime_error("VCD write failed");
}
} // namespace logicscope
