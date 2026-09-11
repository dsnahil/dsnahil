#include "logicscope/simulator.hpp"
#include <algorithm>
#include <array>
#include <functional>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
using namespace logicscope;
namespace {
int count=0;
void check(bool b,const char* s) { if(!b) throw std::runtime_error(s); }
template<class F> void rejects(F f) { bool caught=false; try {f();} catch(const std::exception&){caught=true;} check(caught,"expected exception"); }
Circuit circuit(const std::string& s) { std::istringstream in(s); return parse(in); }
std::vector<Stimulus> stimuli(const std::string& s,const Circuit& c) { std::istringstream in(s); return parse_stimulus(in,c); }
void test(const char* name,const std::function<void()>& f) {f();++count;std::cout<<"PASS "<<name<<'\n';}
}
int main() {
    try {
        test("four-state controlling values", [] {
            std::array<Logic,2> v{Logic::zero,Logic::x}; check(evaluate(Operation::land,v)==Logic::zero,"0 AND X");
            v={Logic::one,Logic::z}; check(evaluate(Operation::lor,v)==Logic::one,"1 OR Z");
            check(evaluate(Operation::lxor,v)==Logic::x,"1 XOR Z");
        });
        test("resolver and tri-state", [] {
            std::array<Logic,2> v{Logic::zero,Logic::one}; check(evaluate(Operation::resolve,v)==Logic::x,"contention");
            v={Logic::z,Logic::one}; check(evaluate(Operation::resolve,v)==Logic::one,"Z resolution");
            v={Logic::one,Logic::zero}; check(evaluate(Operation::tri,v)==Logic::z,"disabled tri");
            v={Logic::one,Logic::x}; check(evaluate(Operation::tri,v)==Logic::x,"unknown enable");
        });
        test("narrow pulses are rejected", [] {
            auto c=circuit("input a\nwire y\ngate b BUF y 5 a\n");
            auto t=Simulator(c).run(stimuli("0 a 0\n10 a 1\n12 a 0\n",c),30);
            check(t.final_values[c.find("y")]==Logic::zero,"pulse final");
            check(std::none_of(t.changes.begin(),t.changes.end(),[&](auto x){return x.net==c.find("y")&&x.value==Logic::one;}),"pulse leaked");
            check(t.statistics.stale==1,"expected canceled event");
        });
        test("pulse equal to delay passes", [] {
            auto c=circuit("input a\nwire y\ngate b BUF y 5 a\n");auto t=Simulator(c).run(stimuli("0 a 0\n10 a 1\n15 a 0\n",c),30);
            check(std::any_of(t.changes.begin(),t.changes.end(),[&](auto x){return x.net==c.find("y")&&x.time==15&&x.value==Logic::one;}),"equal width pulse");
        });
        test("simultaneous input order leaves outputs invariant", [] {
            auto c=circuit("input a\ninput b\nwire y\ngate x XOR y 0 a b\n");
            auto a=Simulator(c).run(stimuli("0 a 0\n0 b 0\n10 a 1\n10 b 1\n",c),20);
            auto b=Simulator(c).run(stimuli("0 b 0\n0 a 0\n10 b 1\n10 a 1\n",c),20);
            check(a.final_values==b.final_values,"order dependence");
            check(std::none_of(a.changes.begin(),a.changes.end(),[&](auto x){return x.net==c.find("y")&&x.time==10;}),"spurious XOR glitch");
        });
        test("zero delay delta propagation", [] {
            auto c=circuit("input a\nwire n\nwire y\ngate b BUF n 0 a\ngate i NOT y 0 n\n");
            auto t=Simulator(c).run(stimuli("0 a 1\n",c),0);
            check(t.final_values[c.find("y")]==Logic::zero,"delta settle");check(t.changes.back().delta==2,"wrong delta");
        });
        test("DFF captures positive edge and holds", [] {
            auto c=circuit("input d\ninput clk\nwire q\ngate f DFF q 1 d clk\n");
            auto t=Simulator(c).run(stimuli("0 d 1\n0 clk 0\n5 clk 1\n7 d 0\n10 clk 0\n",c),20);
            check(t.final_values[c.find("q")]==Logic::one,"DFF capture/hold");
            check(std::any_of(t.changes.begin(),t.changes.end(),[&](auto x){return x.net==c.find("q")&&x.time==6;}),"DFF delay");
        });
        test("ambiguous clock edge produces unknown", [] {
            auto c=circuit("input d\ninput clk\nwire q\ngate f DFF q 0 d clk\n");
            auto t=Simulator(c).run(stimuli("0 d 1\n0 clk 0\n5 clk 1\n10 clk 0\n15 clk x\n",c),20);
            check(t.final_values[c.find("q")]==Logic::x,"uncertain edge");
        });
        test("zero delay oscillation is bounded", [] {
            auto c=circuit("input en\nwire loop\ngate osc NAND loop 0 en loop\n");
            rejects([&]{Simulator(c).run(stimuli("0 en 0\n5 en 1\n",c),10,1000,20);});
        });
        test("processed event budget", [] {
            auto c=circuit("input a\nwire y\ngate b BUF y 0 a\n");rejects([&]{Simulator(c).run(stimuli("0 a 1\n",c),10,1);});
        });
        test("time overflow guarded", [] {
            auto c=circuit("input a\nwire y\ngate b BUF y 5 a\n"); std::vector<Stimulus> s{{std::numeric_limits<Tick>::max()-1,0,Logic::one}};
            rejects([&]{Simulator(c).run(s,std::numeric_limits<Tick>::max());});
        });
        test("duplicate input at a time rejected", [] {
            auto c=circuit("input a\n");rejects([&]{Simulator(c).run(stimuli("0 a 0\n0 a 1\n",c),10);});
        });
        test("undriven and multiple-driven nets rejected", [] {
            rejects([]{circuit("wire a\n");});
            rejects([]{circuit("input a\nwire y\ngate b BUF y 0 a\ngate c BUF y 0 a\n");});
        });
        test("invalid arity and unknown names rejected", [] {
            rejects([]{circuit("input a\nwire y\ngate b AND y 0 a\n");});
            rejects([]{circuit("input a\nwire y\ngate b BUF y 0 nope\n");});
        });
        test("negative time and bad symbols rejected", [] {
            auto c=circuit("input a\n"); rejects([&]{stimuli("-1 a 1",c);}); rejects([&]{stimuli("1 a 2",c);});
        });
        test("VCD and JSON written", [] {
            auto c=circuit("input a\n");auto t=Simulator(c).run(stimuli("5 a 1",c),10);std::ostringstream v,j;
            write_vcd(v,c,t);write_json(j,c,t); check(v.str().find("#5\n1n0")!=std::string::npos,"VCD missing transition");
            check(j.str().find("\"a\":\"1\"")!=std::string::npos,"JSON missing final");
        });
        test("simulation may be replayed", [] { auto c=circuit("input a\n");Simulator s(c);auto v=stimuli("0 a 1",c);check(s.run(v,0).final_values==s.run(v,0).final_values,"replay state leaked"); });
        std::cout<<count<<" logic test groups passed\n";return 0;
    } catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
