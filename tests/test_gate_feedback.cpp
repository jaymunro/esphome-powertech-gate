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
 // Concatenated same-direction motion is one usable sample, not two confirmations.
 assert(GateFeedback::parse_reply("ACK STATUS:FULL OPENING,77\nACK STATUS:FULL OPENING,78",s));
 assert(s.text=="FULL OPENING" && s.position==78);
 assert(GateFeedback::parse_reply("ACK STATUS:PED CLOSING,20\r\nACK STATUS:PED CLOSING,19",s));
 assert(s.position==19);
 for (auto bad : {"ACK STATUS:FULL OPENING,78\nACK STATUS:FULL OPENING,77",
                  "ACK STATUS:PED CLOSING,19\nACK STATUS:PED CLOSING,20",
                  "ACK STATUS:FULL OPENING,77\nACK STATUS:PED OPENING,78",
                  "ACK STATUS:FULL OPENING,77\nACK STATUS:FULL CLOSED,0",
                  "ACK STATUS:FULL CLOSED,0\nACK STATUS:FULL CLOSED,1",
                  "ACK STATUS:FULL OPENED,98\nACK STATUS:FULL OPENED,99"})
   assert(!GateFeedback::parse_reply(bad,s));
 // Hardware counterexample: car travel can retain PED OPENING after a PED
 // command. Prefixes alone must not classify external access mode.
 GateFeedback sticky({"PS19001","SINGLE STOPPED",95,0,true,5});sticky.connected(0);
 sticky.accept({"PED CLOSED",1},16000);sticky.accept({"PED CLOSED",1},18000);
 sticky.accept({"PED OPENING",5},20000);sticky.accept({"PED OPENING",14},21000);
 assert(sticky.car()=="unavailable" && sticky.pedestrian()=="unavailable" && sticky.gate()=="open");
 sticky.accept({"PED OPENING",98},22000);assert(sticky.pedestrian()=="unavailable");
 sticky.accept({"PED OPENED",99},23000);assert(sticky.accept({"PED OPENED",99},24000));
 assert(sticky.car()=="open" && sticky.pedestrian()=="closed");
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
 // C004 labels, unlike sticky UART prefixes, can hint an external opening.
 for(bool ped : {false,true}) {
  GateFeedback events({"PS19001","SINGLE STOPPED",95,0,true,5,true});events.connected(0);
  events.accept({"PED CLOSED",1},16000);events.accept({"PED CLOSED",1},18000);
  events.event(19000,ped ? "PedOpening;src=fixture" : "Opening;src=fixture");
  assert(events.gate()=="closed" && !events.confirmed_closed());
  assert(!events.accept({"PED OPENING",14},19500));
  assert(!events.accept({"PED OPENING",14},20100)); // A stale repeated position is insufficient.
  assert(events.accept({"PED OPENING",24},20200));
  assert((ped ? events.pedestrian() : events.car())=="opening");
  assert((ped ? events.car() : events.pedestrian())=="closed");
  events.event(20300,ped ? "Opening" : "PedOpening"); // A late opposite event cannot relabel the cycle.
  events.accept({"PED OPENING",40},20900);events.accept({"PED OPENING",50},21500);
  assert((ped ? events.pedestrian() : events.car())=="opening");
 }
 auto closed_events=[](){GateFeedback g({"PS19001","SINGLE STOPPED",95,0,true,5,true});
   g.connected(0);g.accept({"PED CLOSED",1},16000);g.accept({"PED CLOSED",1},18000);return g;};
 // Replayed idle event is consumed by CLOSED and cannot label a later opening.
 auto idle_event=closed_events();idle_event.event(19000,"PedOpening");
 idle_event.accept({"PED CLOSED",1},19500);idle_event.accept({"PED CLOSED",1},20700);
 idle_event.accept({"PED OPENING",14},22000);idle_event.accept({"PED OPENING",24},22600);
 assert(idle_event.car()=="unavailable" && idle_event.pedestrian()=="unavailable");
 // Conflicting, expired, malformed and reversed-motion hints fail unresolved.
 for(int scenario=0;scenario<4;++scenario) {
  auto g=closed_events();g.event(19000,scenario==2 ? "NotPedOpening" : "PedOpening");
  if(scenario==0)g.event(19100,"Opening");
  const uint32_t at=scenario==1 ? 28000 : 19500;
  g.accept({"PED OPENING",20},at);g.accept({"PED OPENING",scenario==3 ? 10 : 30},at+600);
  g.accept({"PED OPENING",40},at+1200);g.accept({"PED OPENING",50},at+1800);
  assert(g.car()=="unavailable" && g.pedestrian()=="unavailable");
 }
 auto reconnect=closed_events();reconnect.disconnected();reconnect.connected(19000);
 reconnect.event(20000,"PedOpening");reconnect.accept({"PED OPENING",14},35000);
 reconnect.accept({"PED OPENING",24},35600);assert(reconnect.pedestrian()=="unavailable");
 // A compatible event preserves pending fresh confirmation, not an old stable candidate.
 auto endpoint=closed_events();endpoint.accept({"PED OPENED",100},20000);
 endpoint.event(20400,"Opened;src=fixture");assert(endpoint.accept({"PED OPENED",100},20600));
 assert(endpoint.car()=="open");
 endpoint.accept({"PED CLOSED",1},21000);endpoint.event(21400,"PedClosed");
 assert(!endpoint.accept({"PED CLOSED",1},21600));assert(endpoint.accept({"PED CLOSED",1},22000));
 endpoint.event(23000,"PedClosed");assert(!endpoint.confirmed_closed());
 assert(!endpoint.accept({"PED CLOSED",1},23100));assert(endpoint.accept({"PED CLOSED",1},24200));
 // Sanitized replay of the 11:26 wired car capture. Sticky PED prefix must
 // classify car from corroborated Opening, then keep explicit CLOSED timing.
 GateFeedback recorded({"PS19001","SINGLE STOPPED",95,0,true,5,true});recorded.connected(0);
 recorded.accept({"PED CLOSED",1},16000);recorded.accept({"PED CLOSED",1},20628);
 recorded.event(23392,"Opening;src=fixture");
 recorded.accept({"PED OPENING",40},25736);
 assert(recorded.accept({"PED OPENING",52},26556));assert(recorded.car()=="opening");
 assert(recorded.accept({"PED OPENED",100},31889));assert(recorded.car()=="open");
 recorded.event(32402,"Opened");recorded.accept({"PED OPENED",100},32714);
 recorded.accept({"PED OPENED",100},33431);
 recorded.event(41403,"Closing");recorded.accept({"PED CLOSING",95},41638);
 recorded.accept({"PED CLOSING",87},42334);assert(recorded.car()=="closing");
 recorded.accept({"PED CLOSING",0},49840);assert(recorded.gate()=="open");
 recorded.event(50116,"PedClosed");recorded.accept({"PED CLOSED",1},50559);
 assert(!recorded.accept({"PED CLOSED",1},51348));
 assert(recorded.accept({"PED CLOSED",1},52168));assert(recorded.confirmed_closed());
 // The next external pedestrian cycle is independent of the previous car mode.
 recorded.event(53000,"PedOpening");recorded.accept({"FULL OPENING",4},53500);
 recorded.accept({"FULL OPENING",10},54100);assert(recorded.pedestrian()=="opening" && recorded.car()=="closed");
 // Live alpha9 counterexample: UART contention made a valid Closed baseline
 // 7.27 seconds old. Use the established 25s physical freshness limit, not a
 // second 5s cutoff that discards a correctly corroborated external opening.
 GateFeedback contention({"PS19001","SINGLE STOPPED",95,0,true,5,true});contention.connected(0);
 contention.accept({"PED CLOSED",1},15000);contention.accept({"PED CLOSED",1},16739);
 contention.event(24009,"Opening;src=fixture");
 contention.accept({"PED OPENING",35},25959);
 assert(contention.accept({"PED OPENING",45},26666));assert(contention.car()=="opening");
 // An expired Closed baseline cannot lend credibility to a replayed event.
 auto expired_baseline=closed_events();expired_baseline.event(44001,"Opening");
 expired_baseline.accept({"PED OPENING",14},44500);expired_baseline.accept({"PED OPENING",24},45100);
 assert(expired_baseline.car()=="unavailable" && expired_baseline.pedestrian()=="unavailable");
 puts("Feedback tests passed: parsing, replay quarantine, two samples, exclusive modes, stale/disconnect, reconnect and guarded C004 hints.");
}
