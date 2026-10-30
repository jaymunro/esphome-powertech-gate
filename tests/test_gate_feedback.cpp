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
 // A buffered event never reports open; it only invalidates until status confirms.
 f.event(18000); assert(!f.available() && f.gate()=="unavailable");
 assert(!f.accept({"PED CLOSED",0},19000)); assert(f.accept({"PED CLOSED",0},21000));
 assert(f.gate()=="closed");
 f.event(22000); assert(!f.accept({"SINGLE STOPPED",25},23000));
 assert(f.accept({"SINGLE STOPPED",25},25000));
 assert(f.gate()=="open" && f.car()=="closed" && f.pedestrian()=="open");
 f.event(26000); assert(!f.accept({"PED CLOSING",15},27000)); assert(f.accept({"PED CLOSING",10},29000));
 assert(f.pedestrian()=="closing" && f.car()=="closed");
 assert(!f.accept({"PED CLOSED",0},31000));
 assert(f.gate()=="unavailable" && f.car()=="unavailable" && f.pedestrian()=="unavailable");
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
 puts("Feedback tests passed: parsing, replay quarantine, two samples, exclusive modes, stale/disconnect, reconnect and wraparound.");
}
