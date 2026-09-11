#include "timingforge/timing.hpp"
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace timingforge;
namespace {
int count = 0;
void check(bool b, const char* message) { if (!b) throw std::runtime_error(message); }
void near(double a, double b) { check(std::abs(a-b) < 1e-10,"timing mismatch"); }
template<class F> void rejects(F fn) { bool caught = false; try { fn(); } catch (const std::exception&) { caught = true; } check(caught,"expected exception"); }
std::shared_ptr<const Graph> graph(const std::string& text) { std::istringstream in(text); return parse(in); }
const std::string diamond = "input a 0 0\ninput b 1 2\nnode n\noutput y 2 10\narc a n 1 3\narc b n 2 4\narc n y 1 2\n";
void test(const char* name, const std::function<void()>& body) { body(); ++count; std::cout << "PASS " << name << '\n'; }
}
int main() {
    try {
        test("diamond arrival and constraints", [] {
            auto g = graph(diamond); Engine e(g); const auto r=e.analyze();
            near(r.endpoints[0].earliest,2); near(r.endpoints[0].latest,8);
            near(r.worst_setup_slack,2); near(r.worst_hold_slack,0);
            near(e.required()[g->find("a")].late,5); near(e.required()[g->find("a")].early,0);
            check(r.endpoints[0].late_path == std::vector<NodeId>({1,2,3}),"wrong critical path");
        });
        test("ECO changes winning path and matches rebuild", [] {
            auto g=graph(diamond); Engine e(g); e.analyze(); std::vector<Update> u{{0,{1,8}}};
            const auto r=e.update(u); near(r.endpoints[0].latest,10); near(r.worst_setup_slack,0);
            check(r.endpoints[0].late_path == std::vector<NodeId>({0,2,3}),"ECO predecessor stale");
            check(r.forward_nodes_visited==2,"ECO should visit only n and y");
            auto arcs=g->arcs(); arcs[0].delay=u[0].delay;
            Engine rebuilt(std::make_shared<const Graph>(g->nodes(),arcs)); const auto rr=rebuilt.analyze();
            near(rr.worst_setup_slack,r.worst_setup_slack);
            for (std::size_t i=0;i<g->nodes().size();++i) { near(e.required()[i].early,rebuilt.required()[i].early); near(e.required()[i].late,rebuilt.required()[i].late); }
        });
        test("delay decrease propagates", [] { Engine e(graph(diamond)); e.analyze(); std::vector<Update> u{{1,{0,0}}}; auto r=e.update(u); near(r.endpoints[0].latest,5); });
        test("no-op update stops at first node", [] { Engine e(graph(diamond)); e.analyze(); std::vector<Update> u{{0,{1,3}}}; check(e.update(u).forward_nodes_visited==1,"no-op traversed fanout"); });
        test("invalid batch is atomic", [] {
            Engine e(graph(diamond)); e.analyze(); std::vector<Update> u{{0,{0,50}},{99,{0,1}}}; rejects([&]{ e.update(u); }); near(e.update({}).worst_setup_slack,2);
        });
        test("overflow rolls back ECO", [] {
            Engine e(graph(diamond)); e.analyze(); const double big=std::numeric_limits<double>::max();
            std::vector<Update> u{{0,{big,big}},{2,{big,big}}}; rejects([&]{ e.update(u); }); near(e.update({}).worst_setup_slack,2);
        });
        test("duplicate ECO arc rejected", [] { Engine e(graph(diamond)); std::vector<Update> u{{0,{1,1}},{0,{2,2}}}; rejects([&]{e.update(u);}); });
        test("parallel corners equal serial", [] {
            auto g=graph(diamond); std::vector<double> scales{0.7,1,1.1,1.5}; auto a=analyze_corners(g,scales,1),b=analyze_corners(g,scales,4);
            for (std::size_t i=0;i<scales.size();++i) { near(a[i].worst_setup_slack,b[i].worst_setup_slack); near(a[i].worst_hold_slack,b[i].worst_hold_slack); }
            rejects([&]{analyze_corners(g,scales,0);});
        });
        test("parallel worker exceptions propagate", [] { std::vector<double> s{1e308}; rejects([&]{ analyze_corners(graph(diamond),s,2); }); });
        test("deterministic tie break", [] {
            auto g=graph("input a 0 0\ninput b 0 0\noutput y 0 10\narc b y 1 2\narc a y 1 2\n");
            auto r=Engine(g).analyze(); check(r.endpoints[0].late_path.front()==g->find("b"),"arc order tie break failed");
        });
        test("cycle rejected", [] { rejects([]{graph("input a 0 0\nnode n\noutput y 0 10\narc a n 1 1\narc n y 1 1\narc y n 1 1\n");}); });
        test("undeclared node rejected", [] { rejects([]{graph("input a 0 0\noutput y 0 10\narc nope y 1 1\n");}); });
        test("duplicate names and arcs rejected", [] {
            rejects([]{graph("input a 0 0\noutput a 0 1\n");});
            rejects([]{graph("input a 0 0\noutput y 0 1\narc a y 0 1\narc a y 0 1\n");});
        });
        test("negative delay rejected", [] { rejects([]{graph("input a 0 0\noutput y 0 1\narc a y -1 1\n");}); });
        test("malformed input rejected", [] { rejects([]{graph("input a 0 0 garbage\n");}); rejects([]{graph("output y 2 1\n");}); });
        test("unconstrained sources and incoming input rejected", [] {
            rejects([]{graph("node a\noutput y 0 1\narc a y 0 1\n");});
            rejects([]{graph("input a 0 0\ninput b 0 0\noutput y 0 1\narc a b 0 1\narc b y 0 1\n");});
        });
        test("multiple endpoints and negative slack", [] {
            auto g=graph("input a 0 0\noutput x 4 5\noutput y 0 2\narc a x 1 2\narc a y 2 3\n");
            const auto r=Engine(g).analyze(); near(r.worst_hold_slack,-3); near(r.worst_setup_slack,-1);
        });
        test("comments and ECO parsing", [] {
            std::istringstream s("# changes\nset 0 0.5 2 # comment\n"); auto u=parse_updates(s); check(u.size()==1,"ECO parse");
            std::istringstream bad("set -1 0 2"); rejects([&]{parse_updates(bad);});
        });
        std::cout << count << " timing test groups passed\n"; return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
