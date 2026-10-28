#include "electric3d/core.hpp"
#include <cassert>
#ifdef NDEBUG
#undef assert
#define assert(expr) do { if (!(expr)) { std::cerr << "FAILED: " << #expr << " at line " << __LINE__ << "\n"; std::abort(); } } while(0)
#endif
#include <cmath>
#include <iostream>
using namespace e3d;
int main(){
  auto p=starter(); assert(p.parts.size()==5);assert(p.wires.size()==7);
  auto loaded=Project::fromJson(p.toJson()); assert(loaded.parts.size()==5);assert(loaded.wires.size()==7);assert(loaded.controllerCode==p.controllerCode);
  auto base=simulate(p,{{1,0,0,0}});assert(base.poweredController);
  assert(base.parts[3].active); // LED via powered MOSFET
  auto off=simulate(p,{{0,0,0,0}});assert(off.poweredController);assert(!off.parts[3].active);
  auto b=loaded.add("npn",0,3);assert(kindOf(b.kind)->pins.size()==3);
  auto m=loaded.add("nmos",0,5);assert(kindOf(m.kind)->pins.size()==3);
  assert(!loaded.connect(b.id,"XX",m.id,"G"));
  assert(loaded.connect(b.id,"B",m.id,"G"));assert(!loaded.connect(b.id,"B",m.id,"G"));
  assert(loaded.removeWire((int)loaded.wires.size()-1));
  std::string old=R"({"format":"electric3d","version":2,"name":"legacy","parts":[{"id":"old1","kind":"battery","x":-1,"y":2,"value":9,"angle":0,"closed":false,"blown":false},{"id":"old2","kind":"led","x":3,"y":2,"value":2}],"wires":[{"start_id":"old1","start_port":"B","end_id":"old2","end_port":"A"}]})";
  auto legacy=Project::fromJson(old);assert(legacy.board.width==60);assert(legacy.wires.size()==1);assert(legacy.parts[1].angle==0);
  bool rejected=false;try{Project::fromJson("{\"format\":\"electric3d\",\"version\":99}");}catch(...){rejected=true;}assert(rejected);
  rejected=false;try{Project::fromJson("{\"format\":\"electric3d\",\"version\":3,\"parts\":[],\"wires\":[],\"board\":{\"width\":-8,\"height\":10}}");}catch(...){rejected=true;}assert(rejected);
  auto q=Project::fromJson(loaded.toJson());assert(q.parts.size()==7);assert(q.wires.size()==7);
  auto simple=Project{};auto bat=simple.add("battery",-2,0).id;auto r=simple.add("resistor",1,0).id;
  simple.connect(bat,"B",r,"A");simple.connect(r,"B",bat,"A");
  auto calc=simulate(simple,{{0,0,0,0}});assert(calc.parts[1].active);assert(calc.parts[1].current>.03&&calc.parts[1].current<.05);
  auto example=Project::load(std::string(ELECTRIC3D_SOURCE_DIR)+"/examples/starter-board.elctrd");
  assert(example.parts.size()==5&&example.wires.size()==7);
  auto exampleSim=simulate(example,{{1,0,0,0}});assert(exampleSim.poweredController&&exampleSim.parts[3].active);
  auto prev=Project::load(std::string(ELECTRIC3D_SOURCE_DIR)+"/examples/legacy-v2.elctrd");
  assert(!prev.parts.empty());
  auto weird=Project::fromJson(R"({"format":"electric3d","version":3,"name":"\u041f\u043b\u0430\u0442\u0430 \"1\"","board":{"width":20,"height":16},"parts":[],"wires":[]})");
  assert(weird.name.find("1")!=std::string::npos);
  auto roundtrip=Project::fromJson(weird.toJson()); assert(roundtrip.name==weird.name);
  std::cout<<"Electric3D core: all tests passed\n";
}
