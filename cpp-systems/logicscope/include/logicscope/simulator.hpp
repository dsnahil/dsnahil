#pragma once
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <optional>
#include <queue>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace logicscope {
enum class Logic : std::uint8_t { zero, one, x, z };
enum class Operation { buf, inv, land, lor, lxor, nand, nor, xnor, tri, resolve, dff };
using NetId = std::uint32_t;
using GateId = std::uint32_t;
using Tick = std::uint64_t;
Logic parse_logic(char c);
char symbol(Logic value) noexcept;
Logic evaluate(Operation op, std::span<const Logic> inputs);
struct Net { std::string name; bool input = false; };
struct Gate { std::string name; Operation op; NetId output; std::vector<NetId> inputs; Tick delay; };
struct Stimulus { Tick time; NetId net; Logic value; };
class Circuit {
public:
    Circuit(std::vector<Net> nets, std::vector<Gate> gates);
    [[nodiscard]] const std::vector<Net>& nets() const noexcept { return nets_; }
    [[nodiscard]] const std::vector<Gate>& gates() const noexcept { return gates_; }
    [[nodiscard]] const std::vector<GateId>& fanout(NetId net) const { return fanout_.at(net); }
    [[nodiscard]] NetId find(const std::string& name) const;
private:
    std::vector<Net> nets_;
    std::vector<Gate> gates_;
    std::vector<std::vector<GateId>> fanout_;
    std::unordered_map<std::string, NetId> names_;
};
struct Transition { Tick time; std::uint32_t delta; NetId net; Logic value; };
struct Statistics { std::size_t processed = 0, stale = 0, peak_queue = 0; };
struct Trace { std::vector<Transition> changes; std::vector<Logic> final_values; Statistics statistics; };

// A run owns all mutable state; a Circuit may be reused across independent runs.
// Inertial output events are invalidated by generation, never removed from the heap.
class Simulator {
public:
    explicit Simulator(const Circuit& circuit) : circuit_(circuit) {}
    Simulator(Circuit&&) = delete;
    Trace run(std::span<const Stimulus> inputs, Tick until,
              std::size_t max_events = 1'000'000, std::uint32_t max_delta = 10'000) const;
private:
    const Circuit& circuit_; // Caller keeps the immutable circuit alive through run().
};
Circuit parse(std::istream& input);
std::vector<Stimulus> parse_stimulus(std::istream& input, const Circuit& circuit);
void write_json(std::ostream& out, const Circuit& circuit, const Trace& trace);
void write_vcd(std::ostream& out, const Circuit& circuit, const Trace& trace);
} // namespace logicscope
