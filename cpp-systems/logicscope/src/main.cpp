#include "logicscope/simulator.hpp"
#include <charconv>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

int main(int argc, char** argv) {
    try {
        if (argc < 3 || std::string(argv[1]) == "--help") {
            std::cout << "Usage: logicscope CIRCUIT STIMULUS [--until N] [--vcd FILE] [--max-events N] [--max-delta N]\n";
            return argc >= 2 && std::string(argv[1]) == "--help" ? 0 : 2;
        }
        logicscope::Tick until = 1000; std::size_t max_events = 1'000'000; std::uint32_t max_delta = 10'000;
        std::string vcd;
        for (int i = 3; i < argc; ++i) {
            const std::string option = argv[i];
            if (i + 1 >= argc) throw std::invalid_argument("missing option argument");
            const std::string value = argv[++i];
            if (option == "--vcd" && vcd.empty()) { vcd = value; continue; }
            std::uint64_t n; const auto [p, ec] = std::from_chars(value.data(), value.data() + value.size(), n);
            if (ec != std::errc{} || p != value.data() + value.size()) throw std::invalid_argument("invalid unsigned option");
            if (option == "--until") until = n;
            else if (option == "--max-events" && n <= std::numeric_limits<std::size_t>::max()) max_events = static_cast<std::size_t>(n);
            else if (option == "--max-delta" && n <= std::numeric_limits<std::uint32_t>::max()) max_delta = static_cast<std::uint32_t>(n);
            else throw std::invalid_argument("unknown or out-of-range option: " + option);
        }
        std::ifstream netlist(argv[1]), stimulus(argv[2]);
        if (!netlist || !stimulus) throw std::runtime_error("cannot open circuit or stimulus");
        const auto circuit = logicscope::parse(netlist);
        const auto inputs = logicscope::parse_stimulus(stimulus,circuit);
        const auto trace = logicscope::Simulator(circuit).run(inputs,until,max_events,max_delta);
        if (!vcd.empty()) {
            std::ofstream file(vcd); if (!file) throw std::runtime_error("cannot open VCD output");
            logicscope::write_vcd(file,circuit,trace); file.close();
            if (!file) throw std::runtime_error("VCD flush failed");
        }
        logicscope::write_json(std::cout,circuit,trace);
        return 0;
    } catch (const std::exception& e) { std::cerr << "logicscope: " << e.what() << '\n'; return 2; }
}
