#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <cstdio>
#include "../src/gate_feedback.inc"
int main() {
 GateFeedback::Sample s;
 assert(GateFeedback::parse_reply("ERROR:READ LIGHT STATUS\r\nACK STATUS:PED CLOSED,0;src=fixture",s));
 assert(s.text=="PED CLOSED" && s.position==0);
 for(auto bad:{"ACK STATUS","ACK STATUS:NONSENSE,0","ACK STATUS:PED CLOSED,-1", "ACK STATUS:PED CLOSED,101", "ACK STATUS:PED CLOSED,0,5", "ACK STATUS:PED CLOSED,0\nACK STATUS:SINGLE STOPPED,25"})
  assert(!GateFeedback::parse_reply(bad,s));
 GateFeedback f; f.connected(0); assert(!f.available());
 f.event(6000); assert(!f.accept({"PED CLOSED",0},8000));
 assert(!f.accept({"PED CLOSED",0},15000)); assert(f.accept({"PED CLOSED",0},17000));
 assert(f.gate()=="closed" && f.car()=="closed" && f.pedestrian()=="closed");
 // A buffered event never reports movement or discards fresh confirmed state.
 f.event(18000); assert(f.available() && f.gate()=="closed" && !f.confirmed_closed());
 assert(!f.accept({"PED CLOSED",0},19000)); assert(f.accept({"PED CLOSED",0},21000));
 assert(f.gate()=="closed");
 f.event(22000); assert(!f.accept({"SINGLE STOPPED",25},23000));
 assert(f.accept({"SINGLE STOPPED",25},25000));
 assert(f.gate()=="open" && f.car()=="closed" && f.pedestrian()=="open");
 f.event(26000); assert(!f.accept({"PED CLOSING",15},27000)); assert(f.accept({"PED CLOSING",10},29000));
 assert(f.pedestrian()=="closing" && f.car()=="closed");
 assert(!f.accept({"PED CLOSED",0},31000));
 assert(f.gate()=="open" && f.pedestrian()=="closing");
 assert(f.accept({"PED CLOSED",0},33000));
 // Unknown opening mode cannot produce two misleading open access covers.
 f.event(34000); assert(!f.accept({"PED OPENING",20},35000)); assert(f.accept({"PED OPENING",40},37000));
 assert(f.gate()=="open" && f.car()=="unavailable" && f.pedestrian()=="unavailable");
 assert(!f.accept({"PED OPENED",100},39000)); assert(f.accept({"PED OPENED",100},41000));
 assert(f.car()=="open" && f.pedestrian()=="closed");
 f.tick(66001); assert(!f.available() && f.gate()=="unavailable");
 f.disconnected(); assert(!f.accept({"PED CLOSED",0},70000));
 f.connected(71000); assert(!f.accept({"PED CLOSED",0},72000));
 assert(!f.accept({"PED CLOSED",0},87000)); assert(f.accept({"PED CLOSED",0},89000));
 // Integer wraparound preserves age checks.
 f.connected(UINT32_MAX-10000); assert(!f.accept({"PED CLOSED",0},6000));
 assert(f.accept({"PED CLOSED",0},8000));
 GateFeedback single({"FIXTURE", "PED STOPPED", 90, 0, false}); single.connected(0);
 assert(!single.accept({"SINGLE STOPPED",25},16000)); assert(single.accept({"SINGLE STOPPED",25},18000));
 assert(single.gate()=="open" && single.pedestrian()=="unavailable");
 assert(!single.accept({"PED OPENED",90},20000)); assert(single.accept({"PED OPENED",90},22000));
 assert(single.car()=="open");
 // Regression: the board reports CLOSED,1 after a small closing reversal.
 // Only this model's explicit tolerance is broadened; position alone cannot close.
 GateFeedback tolerant({"PS19001", "SINGLE STOPPED", 95, 0, true, 1});
 tolerant.connected(0);
 assert(!tolerant.accept({"PED CLOSED",1},16000));
 assert(tolerant.accept({"PED CLOSED",1},18000));
 assert(tolerant.gate()=="closed" && tolerant.car()=="closed" && tolerant.pedestrian()=="closed");
 assert(tolerant.accept({"PED CLOSED",0},20000));
 assert(!tolerant.accept({"PED CLOSING",1},22000));
 assert(tolerant.accept({"PED CLOSING",1},24000));
 assert(tolerant.gate()=="open");
 tolerant.accept({"PED CLOSED",2},26000);
 assert(!tolerant.accept({"PED CLOSED",2},28000)); assert(!tolerant.available());
 tolerant.tick(53001); assert(!tolerant.available());
 // P3.6 also returned CLOSED,2 with both leaves visually confirmed shut.
 GateFeedback confirmed_two({"PS19001", "SINGLE STOPPED", 95, 0, true, 2});
 confirmed_two.connected(0);
 assert(!confirmed_two.accept({"PED CLOSED",2},16000));
 assert(confirmed_two.accept({"PED CLOSED",2},18000));
 assert(confirmed_two.gate()=="closed");
 confirmed_two.accept({"PED CLOSING",0},20000);
 assert(confirmed_two.accept({"PED CLOSING",0},22000) && confirmed_two.gate()=="open");
 confirmed_two.accept({"PED CLOSED",3},24000);
 assert(!confirmed_two.accept({"PED CLOSED",3},26000) && !confirmed_two.available());
 // Reproduced after a car cycle: CLOSED,3 is physically shut on PS19001 P3.6.
 // Exercise both previously known access modes; require explicit CLOSED twice.
 for (bool pedestrian : {false,true}) {
  GateFeedback three({"PS19001", "SINGLE STOPPED", 95, 0, true, 3});
  three.connected(0);
  three.accept({pedestrian ? "SINGLE STOPPED" : "PED OPENED",pedestrian ? 25 : 100},16000);
  assert(three.accept({pedestrian ? "SINGLE STOPPED" : "PED OPENED",pedestrian ? 25 : 100},18000));
  three.accept({"PED CLOSING",3},20000);three.accept({"PED CLOSING",3},21000);
  assert(three.gate()=="open" && !three.confirmed_closed());
  assert(!three.accept({"PED CLOSED",3},22000));
  assert(!three.confirmed_closed() && three.position()==3);
  assert(three.accept({"PED CLOSED",3},23000));
  assert(three.confirmed_closed() && three.car()=="closed" && three.pedestrian()=="closed");
  // Beyond the verified range is Unknown, never fresh Open or secure Closed.
  assert(!three.accept({"PED CLOSED",4},24000));
  assert(!three.available() && !three.confirmed_closed() && three.position()==4);
  three.tick(49001);assert(three.position()==-1);
  three.accept({"PED OPENING",9},50000);assert(three.position()==9);
  three.disconnected();assert(three.position()==-1);
 }
 // User-selected five-unit tolerance: still require explicit CLOSED twice.
 for (bool pedestrian : {false,true}) {
  GateFeedback five({"PS19001", "SINGLE STOPPED", 95, 0, true, 5});five.connected(0);
  five.accept({pedestrian ? "SINGLE STOPPED" : "PED OPENED",pedestrian ? 25 : 100},16000);
  five.accept({pedestrian ? "SINGLE STOPPED" : "PED OPENED",pedestrian ? 25 : 100},18000);
  for (int position : {4,5}) {
   five.accept({"PED CLOSING",position},20000);five.accept({"PED CLOSING",position},21000);
   assert(five.gate()=="open" && !five.confirmed_closed());
   assert(!five.accept({"PED CLOSED",position},22000));
   assert(!five.confirmed_closed());
   assert(five.accept({"PED CLOSED",position},23000));
   assert(five.confirmed_closed() && five.car()=="closed" && five.pedestrian()=="closed");
  }
  assert(!five.accept({"PED CLOSED",6},24000));assert(!five.available());
 }
 // A bounded local command hint identifies the access mode only after movement.
 GateFeedback hinted; hinted.connected(0);
 hinted.accept({"PED CLOSED",0},16000); assert(hinted.accept({"PED CLOSED",0},18000));
 hinted.commanded_open(true,19000); assert(hinted.available() && !hinted.confirmed_closed());
 assert(!hinted.accept({"PED OPENING",10},19200));
 assert(hinted.accept({"PED OPENING",30},19800));
 assert(hinted.pedestrian()=="opening" && hinted.car()=="closed");
 assert(hinted.accept({"PED OPENING",50},20400)); assert(hinted.pedestrian()=="opening");
 assert(hinted.accept({"SINGLE STOPPED",25},21000)); assert(hinted.pedestrian()=="open");
 assert(hinted.accept({"PED CLOSING",10},21600)); assert(hinted.pedestrian()=="closing");
 assert(!hinted.accept({"PED CLOSED",0},22000)); assert(!hinted.accept({"PED CLOSED",0},22600));
 assert(hinted.accept({"PED CLOSED",0},23200));
 hinted.commanded_open(true,24000);
 hinted.accept({"PED OPENING",10},33000); assert(hinted.accept({"PED OPENING",30},33600));
 assert(hinted.pedestrian()=="unavailable");  // Expired hints cannot classify transmitter movement.
 hinted.commanded_open(true,34000); hinted.disconnected(); hinted.connected(35000);
 hinted.accept({"PED OPENING",10},51000); assert(hinted.accept({"PED OPENING",30},51600));
 assert(hinted.car()=="unavailable" && hinted.pedestrian()=="unavailable");
 // Notifications cannot extend TTL or keep stale Closed alive indefinitely.
 GateFeedback noisy; noisy.connected(0);
 noisy.accept({"PED CLOSED",0},16000);assert(noisy.accept({"PED CLOSED",0},18000));
 for(uint32_t t=19000;t<44000;t+=1000) { noisy.event(t);noisy.tick(t); }
 assert(noisy.available() && !noisy.confirmed_closed());
 noisy.event(44001);noisy.tick(44001);assert(!noisy.available());
 // Access-mode reclassification cannot make a known physical Open Unknown.
 GateFeedback mode;mode.connected(0);
 mode.accept({"PED OPENING",10},16000);mode.accept({"PED OPENING",30},17000);
 assert(mode.available() && mode.gate()=="open");
 assert(!mode.accept({"PED OPENED",100},18000));
 assert(mode.available() && mode.gate()=="open");
 assert(mode.accept({"PED OPENED",100},19000) && mode.car()=="open");
 puts("Feedback tests passed: parsing, replay quarantine, two samples, exclusive modes, stale/disconnect, reconnect and wraparound.");
}
