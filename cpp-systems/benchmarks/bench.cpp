#include "timingforge/timing.hpp"
#include "logicscope/simulator.hpp"
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
namespace {
using Clock=std::chrono::steady_clock;
template<class F> double measure(F f) {
    const auto start=Clock::now();f();
    return std::chrono::duration<double,std::milli>(Clock::now()-start).count();
}
std::size_t number(const char* arg) {
    const std::string s=arg;std::size_t n;
    const auto [p,ec]=std::from_chars(s.data(),s.data()+s.size(),n);
    if(ec!=std::errc{}||p!=s.data()+s.size()||n<100||n>1'000'000) throw std::invalid_argument("node count must be [100,1000000]");
    return n;
}
}
int main(int argc,char** argv) {
    try {
        const std::size_t n=argc>1?number(argv[1]):50000;
        std::vector<timingforge::Node> nodes;std::vector<timingforge::Arc> arcs;
        nodes.reserve(n);arcs.reserve(3*n);
        for(std::size_t i=0;i<n;++i) {
            timingforge::Node node{"n"+std::to_string(i),{}, {}};
            if(i==0)node.input=timingforge::Window{0,0};
            if(i==n-1)node.output=timingforge::Window{1,static_cast<double>(n)};
            nodes.push_back(std::move(node));
            // Reconvergent chain plus skip arcs. Values are exactly representable.
            for(auto stride:{1U,7U,31U})if(i>=stride)
                arcs.push_back({static_cast<timingforge::NodeId>(i-stride),static_cast<timingforge::NodeId>(i),{0.25,0.5}});
        }
        const auto arc_count=arcs.size();
        auto graph=std::make_shared<const timingforge::Graph>(std::move(nodes),std::move(arcs));
        timingforge::Engine engine(graph);engine.analyze();
        timingforge::Result full,eco;
        double full_ms=measure([&]{full=engine.analyze();});
        // Local ECO near a sink: copies and full reverse pass remain in this timing.
        const std::vector<timingforge::Update> update{{static_cast<timingforge::ArcId>(arc_count-3),{0.25,0.75}}};
        double eco_ms=measure([&]{eco=engine.update(update);});
        const std::vector<double> scales{0.8,1,1.1,1.25};
        std::vector<timingforge::Result> parallel;
        double corners_ms=measure([&]{parallel=timingforge::analyze_corners(graph,scales,4);});

        // 256 independent 32-stage buffer chains; sparse and dense transitions.
        std::vector<logicscope::Net> nets;std::vector<logicscope::Gate> gates;std::vector<logicscope::Stimulus> stimulus;
        constexpr std::size_t chains=256,depth=32,cycles=100;
        for(std::size_t c=0;c<chains;++c) {
            const auto base=static_cast<logicscope::NetId>(nets.size());
            nets.push_back({"in"+std::to_string(c),true});
            for(std::size_t d=0;d<depth;++d) {
                const auto out=static_cast<logicscope::NetId>(nets.size());
                nets.push_back({"w"+std::to_string(out),false});
                gates.push_back({"b"+std::to_string(out),logicscope::Operation::buf,out,{out-1},1});
            }
            for(std::size_t t=0;t<cycles;++t)stimulus.push_back({static_cast<logicscope::Tick>(t*100),base,t%2?logicscope::Logic::one:logicscope::Logic::zero});
        }
        const logicscope::Circuit circuit(std::move(nets),std::move(gates));
        logicscope::Trace trace;
        const double logic_ms=measure([&]{trace=logicscope::Simulator(circuit).run(stimulus,cycles*100,2'000'000);});
        if(trace.statistics.processed!=chains*(depth+1)*cycles)throw std::runtime_error("benchmark event count mismatch");
        if(full.worst_setup_slack!=static_cast<double>(n)-(static_cast<double>(n)-1)*0.5)
            throw std::runtime_error("benchmark timing result mismatch");
        std::cout<<std::setprecision(10)<<"{\"timing_nodes\":"<<n<<",\"timing_arcs\":"<<arc_count
            <<",\"timing_full_ms\":"<<full_ms<<",\"timing_eco_ms\":"<<eco_ms
            <<",\"eco_forward_nodes_visited\":"<<eco.forward_nodes_visited
            <<",\"four_corners_four_workers_ms\":"<<corners_ms
            <<",\"logic_gates\":"<<circuit.gates().size()<<",\"logic_processed_events\":"<<trace.statistics.processed
            <<",\"logic_transitions\":"<<trace.changes.size()<<",\"logic_peak_queue\":"<<trace.statistics.peak_queue
            <<",\"logic_run_ms\":"<<logic_ms<<",\"logic_events_per_second\":"<<trace.statistics.processed/(logic_ms/1000)<<"}\n";
        return 0;
    } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}
}
