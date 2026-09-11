#include "timingforge/timing.hpp"
#include <charconv>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    try {
        if (argc < 2 || std::string(argv[1]) == "--help") {
            std::cout << "Usage: timingforge NETLIST [--eco FILE] [--corners SCALE ...] [--workers N]\n"
                         "Writes JSON to stdout. Times are nanoseconds. ECO and corners cannot be combined.\n";
            return argc < 2 ? 2 : 0;
        }
        std::string eco; std::vector<double> scales; std::size_t workers = 1;
        for (int i = 2; i < argc; ++i) {
            const std::string option = argv[i];
            if (option == "--eco" && i + 1 < argc && eco.empty()) eco = argv[++i];
            else if (option == "--workers" && i + 1 < argc) {
                const std::string s = argv[++i];
                const auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), workers);
                if (ec != std::errc{} || p != s.data() + s.size() || workers == 0 || workers > 256)
                    throw std::invalid_argument("invalid worker count");
            } else if (option == "--corners" && i + 1 < argc && scales.empty()) {
                while (i + 1 < argc && std::string(argv[i + 1]).rfind("--", 0) != 0) {
                    const std::string s = argv[++i]; std::size_t end = 0;
                    const double value = std::stod(s, &end);
                    if (end != s.size() || !std::isfinite(value) || value <= 0) throw std::invalid_argument("invalid corner scale");
                    scales.push_back(value);
                }
                if (scales.empty()) throw std::invalid_argument("missing corner scale");
            } else throw std::invalid_argument("unknown or incomplete option: " + option);
        }
        if (!eco.empty() && !scales.empty()) throw std::invalid_argument("ECO and corners cannot be combined");
        std::ifstream file(argv[1]); if (!file) throw std::runtime_error("cannot open netlist");
        auto graph = timingforge::parse(file);
        if (!scales.empty()) {
            const auto results = timingforge::analyze_corners(graph, scales, workers);
            std::cout << "{\"corners\":[";
            for (std::size_t i = 0; i < results.size(); ++i) {
                if (i) std::cout << ',';
                std::cout << "{\"scale\":" << scales[i] << ",\"result\":";
                timingforge::write_json(std::cout, *graph, results[i]); std::cout << '}';
            }
            std::cout << "]}\n";
        } else {
            timingforge::Engine engine(graph); auto result = engine.analyze();
            if (!eco.empty()) {
                std::ifstream updates(eco); if (!updates) throw std::runtime_error("cannot open ECO file");
                result = engine.update(timingforge::parse_updates(updates));
            }
            timingforge::write_json(std::cout, *graph, result); std::cout << '\n';
        }
        if (!std::cout) throw std::runtime_error("stdout write failed");
        return 0;
    } catch (const std::exception& e) { std::cerr << "timingforge: " << e.what() << '\n'; return 2; }
}
