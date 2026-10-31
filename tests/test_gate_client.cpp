// Host-side protocol/state-machine checks. Bluetooth and hashing are test doubles;
// the real PIN/hash exchange is tested separately on the ATOM, never in this file.
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>
using esp_err_t = int; using esp_gatt_if_t = int;
constexpr int ESP_OK=0, ESP_GATT_OK=0, ESP_GATT_AUTH_REQ_NONE=0, ESP_GATT_WRITE_TYPE_RSP=0;
enum esp_gattc_cb_event_t { ESP_GATTC_CONNECT_EVT, ESP_GATTC_CLOSE_EVT, ESP_GATTC_SEARCH_CMPL_EVT,
  ESP_GATTC_REG_FOR_NOTIFY_EVT, ESP_GATTC_NOTIFY_EVT, ESP_GATTC_WRITE_CHAR_EVT, ESP_GATTC_READ_CHAR_EVT };
void test_log(const char *, const char *, ...) {}
#define ESP_LOGW test_log
struct esp_ble_gattc_cb_param_t {
  struct { int reason=0; } close;
  struct { int handle=0, status=0; } reg_for_notify;
  struct { int handle=0; uint8_t *value=nullptr; size_t value_len=0; } notify;
  struct { int handle=0, conn_id=0, status=0; } write;
  struct { int handle=0, conn_id=0, status=0; uint8_t *value=nullptr; size_t value_len=0; } read;
};
uint32_t clock_ms=0; uint32_t millis() { return clock_ms; }
struct Write { uint16_t handle; std::string payload; };
std::vector<Write> writes; std::vector<int> reads;
esp_err_t esp_ble_gattc_write_char(int,int,uint16_t h,size_t n,uint8_t *p,int,int) {
  writes.push_back({h,std::string(reinterpret_cast<char *>(p),n)}); return ESP_OK;
}
esp_err_t esp_ble_gattc_read_char(int,int,uint16_t h,int) { reads.push_back(h); return ESP_OK; }
struct Text { std::string state; void publish_state(const std::string &s) { state=s; } };
struct Number { float state=0; void publish_state(float s) { state=s; } };
Text gate_state_source,car_state_source,pedestrian_state_source,gate_lock_state;
bool monitor_enabled=false,require_gate_unlock=true;
std::string gate_device_name="WBT01-TEST",profile_model="PS19001",profile_pedestrian_status="SINGLE STOPPED";
int profile_full_open_min=95,profile_closed_position=0,profile_closed_tolerance=2; bool profile_supports_pedestrian=true;
Text control_result,poll_health,settings_snapshot,settings_result;
Text setting_1,setting_2,setting_3,setting_4,setting_5,setting_6,setting_7,setting_8,setting_9,setting_a,setting_b,setting_c,setting_d,setting_e,setting_f,setting_g,setting_h,setting_i,setting_j; Number status_successes,unmatched_replies;
Text command_status,status_reply,protocol_result,c004_log_1,c004_log_2,c004_log_3,c004_last,ble_capture_status;
Number c004_packets; bool control_session=false,gate_listening=false;
uint32_t c004_count=0,capture_started_ms=0; std::vector<std::string> capture_records;
std::string gate_pin="0000";  // Synthetic fixture only.
#define id(x) x
namespace esphome {
namespace cover {
enum CoverOperation { COVER_OPERATION_IDLE, COVER_OPERATION_OPENING, COVER_OPERATION_CLOSING };
constexpr float COVER_CLOSED=0, COVER_OPEN=1;
}

namespace esp32_ble_tracker { enum class ClientState { IDLE,ESTABLISHED }; }
namespace md5 { struct MD5Digest { void init() {} void add(const char *,size_t) {} void calculate() {}
  void get_bytes(uint8_t *p) { std::fill(p,p+16,0); } }; }
std::string base64_encode(const uint8_t *,size_t) { return "AAAAAAAAAAAAAAAAAAAAAA=="; }
namespace ble_client {
struct Characteristic { uint16_t handle; };
struct Parent {
  Characteristic auth{46},command{106},result{42}; int connections=0,disconnections=0;
  void connect() { ++connections; } void disconnect() { ++disconnections; }
  int get_gattc_if() { return 1; } int get_conn_id() { return 0; }
  int register_for_notify(uint16_t) { return ESP_OK; }
  Characteristic *get_characteristic(int,int c) { return c==0xB001?&auth:(c==0xC000?&command:&result); }
};
class BLEClientNode { public: Parent *p=nullptr; esp32_ble_tracker::ClientState node_state;
  Parent *parent() { return p; } virtual void loop() {}
  virtual void gattc_event_handler(esp_gattc_cb_event_t,int,esp_ble_gattc_cb_param_t *) {} };
}}
#include "../src/gate_feedback.inc"
namespace esphome { namespace cover { struct Cover {
 float position=0; esphome::cover::CoverOperation current_operation=esphome::cover::COVER_OPERATION_IDLE;
 unsigned publications=0;
 void publish_state(bool save) { assert(!save); ++publications; }
}; }}
using FakeCover=esphome::cover::Cover;
FakeCover car_cover,ped_cover; FakeCover *car_gate_cover=&car_cover,*pedestrian_gate_cover=&ped_cover;
struct FakeBinarySensor {
 bool valid=false,state=false;
 void invalidate_state() { valid=false; }
 void publish_state(bool value) { valid=true; state=value; }
} gate_binary;
FakeBinarySensor &gate_state=gate_binary;
FakeBinarySensor bluetooth_connection;
#include "../src/gate_settings_generated.inc"
#include "../src/gate_client_body.inc"

void event(EyeOpenClient &c,esp_gattc_cb_event_t e,int handle=0,const std::string &s="",int status=0) {
  esp_ble_gattc_cb_param_t p;
  p.reg_for_notify.handle=handle; p.reg_for_notify.status=status;
  p.read.handle=handle; p.read.status=status; p.read.value=(uint8_t *)s.data(); p.read.value_len=s.size();
  p.write.handle=handle; p.write.status=status;
  p.notify.handle=handle; p.notify.value=(uint8_t *)s.data(); p.notify.value_len=s.size();
  c.gattc_event_handler(e,1,&p);
}
void tick(EyeOpenClient &c,uint32_t ms) { clock_ms+=ms; c.loop(); }
void reset() { clock_ms=0; require_gate_unlock=true; profile_supports_pedestrian=true; monitor_enabled=false; gate_lock_state.state="unlocked"; control_result.state.clear(); poll_health.state.clear(); writes.clear(); reads.clear(); control_session=false; gate_listening=false; }
void discover(EyeOpenClient &c) {
  event(c,ESP_GATTC_SEARCH_CMPL_EVT); event(c,ESP_GATTC_REG_FOR_NOTIFY_EVT,42); tick(c,200);
}
void unlock_without_challenge(EyeOpenClient &c) { event(c,ESP_GATTC_READ_CHAR_EVT,46,"M=0;C=fixture"); tick(c,100); }
void acknowledge(EyeOpenClient &c,const std::string &reply) {
  event(c,ESP_GATTC_WRITE_CHAR_EVT,106); tick(c,200); event(c,ESP_GATTC_READ_CHAR_EVT,106,reply);
}
void ready(EyeOpenClient &c) {
  c.request("READ STATUS"); discover(c); unlock_without_challenge(c);
  acknowledge(c,"ACK STATUS:PED CLOSED,0");
  clock_ms=16000; c.request("READ STATUS"); acknowledge(c,"ACK STATUS:PED CLOSED,0");
  clock_ms+=1200; c.request("READ STATUS"); acknowledge(c,"ACK STATUS:PED CLOSED,0");
  assert(gate_binary.valid && !gate_binary.state);
}
size_t settings_write_count() {
  return std::count_if(writes.begin(),writes.end(),[](const Write &w){return w.payload.rfind("C=WRITE FUNCTION,",0)==0;});
}
int main() {
  assert(EyeOpenClient::allowed("READ FUNCTION"));
  assert(!EyeOpenClient::allowed("WRITE FUNCTION 1:1"));
  assert(!EyeOpenClient::allowed("RP,1"));
  assert(!EyeOpenClient::allowed("WP,1:1"));
  std::string snapshot;
  assert(EyeOpenClient::function_reply("C=ACK READ FUNCTION,1:1,J:1;src=fixture",snapshot));
  assert(snapshot=="ACK READ FUNCTION,1:1,J:1;src=fixture");
  assert(!EyeOpenClient::function_reply("ACK READ FUNCTION,",snapshot));
  assert(EyeOpenClient::function_reply("ERROR:READ LIGHT STATUS\r\nC=ACK READ FUNCTION:1:1,J:1,Z:AB;extra=keep",snapshot));
  assert(snapshot=="ACK READ FUNCTION:1:1,J:1,Z:AB;extra=keep");
  assert(!EyeOpenClient::function_reply("ACK READ FUNCTION:",snapshot));
  assert(!EyeOpenClient::function_reply("ACK READ FUNCTIONBAD:1:1",snapshot));
  assert(!EyeOpenClient::function_reply("ACK READ FUNCTION:1:1\nACK READ FUNCTION:1:2",snapshot));
  assert(!EyeOpenClient::function_reply("ACK READ FUNCTION:"+std::string(2048,'1'),snapshot));
  assert(!EyeOpenClient::function_reply(std::string("ACK READ FUNCTION:1:1")+char(0),snapshot));
  assert(EyeOpenClient::allowed("PED OPEN"));
  assert(!EyeOpenClient::allowed("FACTORY DEFAULT"));
  assert(!EyeOpenClient::allowed("PED OPEN;FULL OPEN"));
  assert(EyeOpenClient::matching_ack("ACK STATUS:PED CLOSED,0","READ STATUS"));
  assert(EyeOpenClient::matching_ack("ACK READ STATUS:PED CLOSED,0","READ STATUS"));
  assert(!EyeOpenClient::matching_ack("ACK READ STATUSBAD","READ STATUS"));
  assert(!EyeOpenClient::matching_ack("ACK PED OPENED","PED OPEN"));
  assert(!EyeOpenClient::matching_ack("ERROR:PED OPEN","PED OPEN"));
  assert(EyeOpenClient::matching_ack("ERROR:READ LIGHT STATUS;src=other\r\nACK STATUS:PED CLOSED,0","READ STATUS"));
  std::string out;
  assert(EyeOpenClient::url_decode("A%2BB%2FC+D",out) && out=="A+B/C D");
  assert(!EyeOpenClient::url_decode("bad%2",out));
  assert(EyeOpenClient::integer_field("XC=1;C=45057;R=0","C")==45057);
  assert(EyeOpenClient::integer_field("C=99999999999999","C")==-1);

  // Motion cannot be written before authentication AND a baseline status reply.
  reset(); esphome::ble_client::Parent parent; EyeOpenClient client(false); client.p=&parent;
  client.request("PED OPEN"); assert(writes.empty()); discover(client); assert(writes.empty());
  unlock_without_challenge(client); assert(writes.size()==1 && writes.back().payload=="C=READ STATUS;src=P9999999");
  acknowledge(client,"C=ACK STATUS:PED CLOSED,0"); tick(client,50);
  assert(writes.size()==2 && writes.back().payload=="C=PED OPEN;src=P9999999");
  // Stop waits for the active write's acknowledgement, then bypasses its reply wait.
  client.request("STOP"); assert(writes.size()==2);
  event(client,ESP_GATTC_WRITE_CHAR_EVT,106); tick(client,200);
  assert(writes.size()==3 && writes.back().payload=="C=STOP;src=P9999999");
  acknowledge(client,"C=ACK STOP");
  // An invalid command never changes the completed transaction into a write.
  const auto count=writes.size(); client.request("WRITE FUNCTION00"); assert(writes.size()==count);
  // Failed transport writes are not automatically retried or replayed after close.
  client.request("FULL CLOSE"); assert(writes.back().payload=="C=FULL CLOSE;src=P9999999");
  event(client,ESP_GATTC_WRITE_CHAR_EVT,106,"",1); const auto failed_count=writes.size();
  tick(client,100000); assert(writes.size()==failed_count);
  event(client,ESP_GATTC_CLOSE_EVT); tick(client,100000); assert(writes.size()==failed_count);
  assert(parent.disconnections>0 && !control_session);

  // Wrong authentication confirmation prevents every C000 command.
  reset(); esphome::ble_client::Parent p2; EyeOpenClient c2(false); c2.p=&p2;
  c2.request("FULL OPEN"); discover(c2);
  event(c2,ESP_GATTC_READ_CHAR_EVT,46,"M=1;C=example");
  assert(writes.size()==1 && writes.back().handle==46);
  event(c2,ESP_GATTC_WRITE_CHAR_EVT,46); tick(c2,1000);
  event(c2,ESP_GATTC_READ_CHAR_EVT,46,"M=1;C=example");
  assert(writes.size()==1 && p2.disconnections==1);

  // A stop requested while authenticating replaces the unsent open request.
  reset(); esphome::ble_client::Parent p3; EyeOpenClient c3(false); c3.p=&p3;
  c3.request("FULL OPEN"); c3.request("STOP"); discover(c3); unlock_without_challenge(c3);
  assert(writes.size()==1 && writes.back().payload=="C=STOP;src=P9999999");
  acknowledge(c3,"C=ACK STOP"); tick(c3,120000); assert(p3.disconnections==1);

  // A mismatching old ACK never releases a waiting movement command.
  reset(); esphome::ble_client::Parent p4; EyeOpenClient c4(false); c4.p=&p4;
  c4.request("PED OPEN"); discover(c4); unlock_without_challenge(c4);
  acknowledge(c4,"C=ACK FULL OPEN");
  assert(writes.size()==1 && writes.back().payload=="C=READ STATUS;src=P9999999");
  for(int i=0;i<3;++i) { tick(c4,750); event(c4,ESP_GATTC_READ_CHAR_EVT,106,"C=ACK FULL OPEN"); }
  assert(writes.size()==1 && p4.disconnections>0);
  // A poll ignores unrelated lines until a matching status arrives; no C000 resend.
  reset(); esphome::ble_client::Parent p5; EyeOpenClient c5(false); c5.p=&p5;
  c5.request("READ STATUS"); discover(c5); unlock_without_challenge(c5);
  acknowledge(c5,"C=ACK STATUS:PED CLOSED,0");
  c5.request("PED OPEN"); acknowledge(c5,"C=ACK PED OPEN");
  assert(control_result.state=="Acknowledged: PED OPEN");
  tick(c5,10000); const auto before_poll_reads=writes.size();
  acknowledge(c5,"C=ERROR:READ LIGHT STATUS;src=other");
  assert(control_result.state=="Acknowledged: PED OPEN" && p5.disconnections==0);
  tick(c5,750); event(c5,ESP_GATTC_READ_CHAR_EVT,106,"C=ACK STATUS:SINGLE STOPPED,25");
  assert(writes.size()==before_poll_reads && poll_health.state=="Status reply received");
  assert(control_result.state=="Acknowledged: PED OPEN");

  // A wholly mismatching background reply has bounded reads and backs off,
  // retaining the successful motion result. Only READ STATUS is issued later.
  tick(c5,10000); acknowledge(c5,"C=ERROR:READ LIGHT STATUS;src=other");
  for(int i=0;i<2;++i) { tick(c5,750); event(c5,ESP_GATTC_READ_CHAR_EVT,106,"C=unrelated"); }
  assert(p5.disconnections==0 && poll_health.state.find("mismatch 1")!=std::string::npos);
  assert(control_result.state=="Acknowledged: PED OPEN");
  const auto failed_poll_writes=writes.size(); tick(c5,999);
  assert(writes.size()==failed_poll_writes); tick(c5,1);
  assert(writes.size()==failed_poll_writes+1 && writes.back().payload=="C=READ STATUS;src=P9999999");
  acknowledge(c5,"C=ACK STATUS:PED CLOSED,0");
  assert(poll_health.state=="Status reply received" && control_result.state=="Acknowledged: PED OPEN");

  // A transport failure of a status poll must not label the previous motion uncertain.
  tick(c5,10000); event(c5,ESP_GATTC_WRITE_CHAR_EVT,106,"",1);
  assert(p5.disconnections==1 && control_result.state=="Acknowledged: PED OPEN");
  event(c5,ESP_GATTC_CLOSE_EVT);
  assert(control_result.state=="Acknowledged: PED OPEN");

  // Repeated UART mismatches retain the GATT link, retry only status, then recover.
  reset(); esphome::ble_client::Parent p6; EyeOpenClient c6(false); c6.p=&p6;
  c6.request("READ STATUS"); discover(c6); unlock_without_challenge(c6);
  acknowledge(c6,"C=ACK STATUS:PED CLOSED,0");
  for(int failure=0;failure<4;++failure) {
    tick(c6,failure==0?10000:(failure==1?1000:(failure==2?2000:5000)));
    acknowledge(c6,"C=unrelated");
    for(int i=0;i<2;++i) { tick(c6,750); event(c6,ESP_GATTC_READ_CHAR_EVT,106,"C=unrelated"); }
  }
  assert(p6.disconnections==0 && writes.size()==5);
  tick(c6,5000); acknowledge(c6,"C=ACK STATUS:PED CLOSED,1");
  assert(p6.disconnections==0 && poll_health.state=="Status reply received");
  assert(control_result.state.empty());
  for(const auto &w:writes) assert(w.payload=="C=READ STATUS;src=P9999999");
  // Stop cannot get stranded behind a background poll whose GATT read times out.
  reset(); esphome::ble_client::Parent p7; EyeOpenClient c7(false); c7.p=&p7;
  c7.request("READ STATUS"); discover(c7); unlock_without_challenge(c7);
  acknowledge(c7,"C=ACK STATUS:PED CLOSED,0");
  tick(c7,10000); event(c7,ESP_GATTC_WRITE_CHAR_EVT,106); tick(c7,200);
  c7.request("STOP"); tick(c7,3000); tick(c7,50);
  assert(writes.size()==3 && writes.back().payload=="C=STOP;src=P9999999");
  acknowledge(c7,"C=ACK STOP"); assert(control_result.state=="Acknowledged: STOP");
  // Gate locking is independent of Bluetooth authentication; close and Stop remain allowed.
  reset(); esphome::ble_client::Parent p8; EyeOpenClient c8(false); c8.p=&p8;
  gate_lock_state.state="locked"; c8.request("PED OPEN");
  assert(writes.empty() && p8.connections==0 && control_result.state.find("Not sent")!=std::string::npos);
  gate_lock_state.state="unavailable"; c8.request("FULL OPEN"); assert(p8.connections==0);
  // Automatic reconnect can only issue READ STATUS, never an old movement command.
  reset(); esphome::ble_client::Parent p9; EyeOpenClient c9(false); c9.p=&p9;
  monitor_enabled=true; c9.maintenance(); assert(p9.connections==1);
  discover(c9); unlock_without_challenge(c9); acknowledge(c9,"C=ACK STATUS:PED CLOSED,0");
  event(c9,ESP_GATTC_CLOSE_EVT); assert(gate_state_source.state=="unavailable");
  clock_ms+=15000; c9.maintenance(); assert(p9.connections==2);
  discover(c9); unlock_without_challenge(c9); acknowledge(c9,"C=ACK STATUS:PED CLOSED,0");
  for(const auto &w:writes) assert(w.payload=="C=READ STATUS;src=P9999999");
  c9.request("DISCONNECT"); assert(!monitor_enabled);
  reset(); esphome::ble_client::Parent p10; EyeOpenClient c10(false); c10.p=&p10;
  profile_supports_pedestrian=false; c10.request("PED OPEN"); assert(p10.connections==0 && writes.empty());
  // Native covers follow confirmed feedback, never command acknowledgements.
  reset(); car_cover={}; ped_cover={}; esphome::ble_client::Parent p11; EyeOpenClient c11(false); c11.p=&p11;
  c11.request("READ STATUS"); discover(c11); unlock_without_challenge(c11);
  event(c11,ESP_GATTC_CONNECT_EVT); assert(bluetooth_connection.valid && bluetooth_connection.state);
  acknowledge(c11,"ACK STATUS:PED CLOSED,0");
  clock_ms=16000; c11.request("READ STATUS"); acknowledge(c11,"ACK STATUS:PED CLOSED,0");
  clock_ms+=1200; c11.request("READ STATUS"); acknowledge(c11,"ACK STATUS:PED CLOSED,0");
  assert(gate_state_source.state=="closed");
  assert(gate_binary.valid && !gate_binary.state);
  c11.request("PED OPEN"); acknowledge(c11,"ACK PED OPEN");
  assert(car_cover.position==0 && ped_cover.position==0);
  clock_ms+=1200; c11.request("READ STATUS"); acknowledge(c11,"ACK STATUS:SINGLE STOPPED,25");
  assert(ped_cover.position==0);  // Candidate alone is insufficient.
  clock_ms+=1200; c11.request("READ STATUS"); acknowledge(c11,"ACK STATUS:SINGLE STOPPED,25");
  assert(car_cover.position==0 && ped_cover.position==1);
  assert(gate_binary.valid && gate_binary.state);
  clock_ms+=26000; c11.maintenance();
  assert(!gate_binary.valid && bluetooth_connection.state && ped_cover.position==1);
  event(c11,ESP_GATTC_CLOSE_EVT);
  assert(gate_state_source.state=="unavailable" && ped_cover.position==1);
  assert(!gate_binary.valid);
  assert(!bluetooth_connection.state);
  assert(ped_cover.current_operation==esphome::cover::COVER_OPERATION_IDLE);
  // A failed GATT read still tears down the link; only text mismatches are retained.
  reset(); esphome::ble_client::Parent p13; EyeOpenClient c13(false); c13.p=&p13;
  c13.request("READ STATUS"); discover(c13); unlock_without_challenge(c13);
  acknowledge(c13,"ACK STATUS:PED CLOSED,0");
  tick(c13,10000); event(c13,ESP_GATTC_WRITE_CHAR_EVT,106); tick(c13,200);
  event(c13,ESP_GATTC_READ_CHAR_EVT,106,"",1); assert(p13.disconnections==1);
  // An explicit CLOSED,1 reply must close both native access covers too.
  reset(); car_cover.position=1; ped_cover.position=1;
  esphome::ble_client::Parent p12; EyeOpenClient c12(false); c12.p=&p12;
  c12.request("READ STATUS"); discover(c12); unlock_without_challenge(c12);
  acknowledge(c12,"ACK STATUS:PED CLOSED,1");
  clock_ms=16000; c12.request("READ STATUS"); acknowledge(c12,"ACK STATUS:PED CLOSED,1");
  clock_ms+=1200; c12.request("READ STATUS"); acknowledge(c12,"ACK STATUS:PED CLOSED,1");
  assert(gate_binary.valid && !gate_binary.state && car_cover.position==0 && ped_cover.position==0);
  // Closed polling is 1s; notifications prompt a read without overlapping GATT requests.
  reset(); esphome::ble_client::Parent p14; EyeOpenClient c14(false); c14.p=&p14;
  c14.request("READ STATUS"); discover(c14); unlock_without_challenge(c14); acknowledge(c14,"ACK STATUS:PED CLOSED,0");
  clock_ms=16000; c14.request("READ STATUS"); acknowledge(c14,"ACK STATUS:PED CLOSED,0");
  clock_ms+=1200; c14.request("READ STATUS"); acknowledge(c14,"ACK STATUS:PED CLOSED,0");
  const auto idle_writes=writes.size(); tick(c14,999); assert(writes.size()==idle_writes);
  tick(c14,1); assert(writes.size()==idle_writes+1);
  event(c14,ESP_GATTC_NOTIFY_EVT,42,"PedOpening"); tick(c14,0); assert(writes.size()==idle_writes+1);
  acknowledge(c14,"ACK STATUS:PED CLOSED,0"); tick(c14,0);
  assert(writes.size()==idle_writes+2 && writes.back().payload=="C=READ STATUS;src=P9999999");
  acknowledge(c14,"ACK STATUS:PED CLOSED,0");
  const auto fast_writes=writes.size(); tick(c14,499); assert(writes.size()==fast_writes);
  tick(c14,1); assert(writes.size()==fast_writes+1);
  // A single user command can wait behind that poll, rather than being lost.
  c14.request("PED OPEN"); c14.request("FULL OPEN"); assert(writes.size()==fast_writes+1);
  acknowledge(c14,"ACK STATUS:PED CLOSED,0"); tick(c14,50);
  assert(writes.back().payload=="C=PED OPEN;src=P9999999");
  acknowledge(c14,"ACK PED OPEN"); const auto after_open=writes.size(); tick(c14,0);
  assert(writes.size()==after_open+1 && writes.back().payload=="C=READ STATUS;src=P9999999");
  for(const auto &w:writes) assert(w.payload!="C=FULL OPEN;src=P9999999");
  // Simulated complete pedestrian travel publishes real Opening/Closing phases.
  acknowledge(c14,"ACK STATUS:PED OPENING,10");
  tick(c14,500); acknowledge(c14,"ACK STATUS:PED OPENING,30");
  assert(ped_cover.current_operation==esphome::cover::COVER_OPERATION_OPENING && car_cover.position==0);
  // Untimestamped notifications invalidate feedback, not the last confirmed
  // native phase. Resetting motion to idle here falsely displayed Closed.
  const auto opening_publications=ped_cover.publications;
  event(c14,ESP_GATTC_NOTIFY_EVT,42,"PedOpened");
  assert(!gate_binary.valid && ped_cover.position==0);
  assert(ped_cover.current_operation==esphome::cover::COVER_OPERATION_OPENING);
  assert(ped_cover.publications==opening_publications);
  tick(c14,0); acknowledge(c14,"ACK STATUS:PED OPENING,30");
  tick(c14,500); acknowledge(c14,"ACK STATUS:PED OPENING,40");
  tick(c14,500); acknowledge(c14,"ACK STATUS:SINGLE STOPPED,25");
  assert(ped_cover.position==1 && ped_cover.current_operation==esphome::cover::COVER_OPERATION_IDLE);
  tick(c14,500); acknowledge(c14,"ACK STATUS:PED CLOSING,10");
  assert(ped_cover.current_operation==esphome::cover::COVER_OPERATION_CLOSING);
  const auto closing_publications=ped_cover.publications;
  event(c14,ESP_GATTC_NOTIFY_EVT,42,"Closing");
  assert(!gate_binary.valid && ped_cover.position==1);
  assert(ped_cover.current_operation==esphome::cover::COVER_OPERATION_CLOSING);
  assert(ped_cover.publications==closing_publications);
  tick(c14,0); acknowledge(c14,"ACK STATUS:PED CLOSING,10");
  tick(c14,500); acknowledge(c14,"ACK STATUS:PED CLOSING,5");
  assert(gate_binary.valid);
  // Expiry also cannot invent an endpoint. Consumers must check Gate state.
  clock_ms+=26000; c14.maintenance();
  assert(!gate_binary.valid && ped_cover.current_operation==esphome::cover::COVER_OPERATION_CLOSING);
  tick(c14,500); acknowledge(c14,"ACK STATUS:PED CLOSED,1");
  assert(!gate_binary.valid);  // A single closed reply is still insufficient.
  assert(ped_cover.current_operation==esphome::cover::COVER_OPERATION_CLOSING);
  tick(c14,500); acknowledge(c14,"ACK STATUS:PED CLOSED,1"); assert(!gate_binary.valid);
  tick(c14,500); acknowledge(c14,"ACK STATUS:PED CLOSED,1");
  assert(gate_binary.valid && !gate_binary.state && ped_cover.position==0 && car_cover.position==0);

  // The software lock is checked again after a command queued behind a poll.
  reset(); esphome::ble_client::Parent p15; EyeOpenClient c15(false); c15.p=&p15;
  c15.request("READ STATUS"); discover(c15); unlock_without_challenge(c15); acknowledge(c15,"ACK STATUS:PED CLOSED,0");
  tick(c15,10000); c15.request("PED OPEN"); gate_lock_state.state="locked";
  acknowledge(c15,"ACK STATUS:PED CLOSED,0"); tick(c15,50);
  for(const auto &w:writes) assert(w.payload=="C=READ STATUS;src=P9999999");
  assert(control_result.state.find("lock changed")!=std::string::npos);
  // Manual settings reads share the serialized transport but do not act like
  // movement ACKs, invalidate good feedback, or publish truncated snapshots.
  reset(); settings_snapshot.state.clear();
  esphome::ble_client::Parent p16; EyeOpenClient c16(false); c16.p=&p16;
  c16.request("READ STATUS"); discover(c16); unlock_without_challenge(c16);
  acknowledge(c16,"ACK STATUS:PED CLOSED,0");
  clock_ms=16000; c16.request("READ STATUS"); acknowledge(c16,"ACK STATUS:PED CLOSED,0");
  clock_ms+=1200; c16.request("READ STATUS"); acknowledge(c16,"ACK STATUS:PED CLOSED,0");
  assert(gate_binary.valid && !gate_binary.state);
  gate_lock_state.state="locked"; c16.request("READ FUNCTION"); tick(c16,250);
  assert(writes.back().payload=="C=READ FUNCTION;src=P9999999");
  const std::string complete="ACK READ FUNCTION,1:1,J:1,Z:"+std::string(300,'A');
  acknowledge(c16,complete);
  assert(settings_snapshot.state==complete && gate_binary.valid && !gate_binary.state);
  const auto after_settings=writes.size(); tick(c16,1000);
  for(size_t i=after_settings;i<writes.size();++i) assert(writes[i].payload=="C=READ STATUS;src=P9999999");
  event(c16,ESP_GATTC_CLOSE_EVT); tick(c16,100000);
  for(const auto &w:writes) assert(w.payload.find("WRITE FUNCTION")==std::string::npos);
  // Synthetic complete configuration, not a captured household payload.
  const std::string base="ACK READ FUNCTION,1:1,2:01,3:01,4:1,5:1,6:1,7:0,8:0,9:0,A:1,B:0,C:0,D:0,E:0,F:0,G:0,H:0,I:1,J:0,Z:00aF;src=P9999999";
  auto changed=base; changed.replace(changed.find("9:0"),3,"9:2");
  GateSettings settings; assert(settings.decode(base));
  assert(settings.auto_close_command(2)=="WRITE FUNCTION,"+changed.substr(18,changed.find(';')-18));
  assert(settings.auto_close_command(-1).empty() && settings.auto_close_command(9).empty());
  assert(settings.matches_auto_close_change(settings,0));
  assert(settings.decode(base+";future=keep") && settings.auto_close_command(2).empty());
  for(const auto &bad:std::vector<std::string>{base.substr(0,base.find(';'))+",",base.substr(0,base.find(",J:")),"ACK READ FUNCTION,1:3",base+char(0)}) {
    assert(!settings.decode(bad));
  }
  // Successful save: exactly one guarded write, followed by full readback.
  reset(); esphome::ble_client::Parent p17; EyeOpenClient c17(false); c17.p=&p17; ready(c17);
  c17.request_auto_close(2); tick(c17,250); assert(writes.back().payload=="C=READ FUNCTION;src=P9999999");
  acknowledge(c17,base); assert(settings_write_count()==0); tick(c17,250);
  assert(settings_write_count()==1 && writes.back().payload=="C=WRITE FUNCTION,"+changed.substr(18));
  assert(setting_9.state!="10 sec"); // No optimistic selection.
  const auto echoed="ACK FUNCTION,"+changed.substr(18);
  GateSettings expected_write; assert(expected_write.decode(base));
  assert(EyeOpenClient::matching_ack(echoed,expected_write.auto_close_command(2)));
  assert(!EyeOpenClient::matching_ack("ACK FUNCTION,"+base.substr(18),expected_write.auto_close_command(2)));
  acknowledge(c17,echoed); tick(c17,250);
  assert(writes.back().payload=="C=READ FUNCTION;src=P9999999");
  acknowledge(c17,changed);
  assert(setting_9.state=="10 sec" && settings_result.state.find("verified")!=std::string::npos);
  assert(settings_write_count()==1 && gate_binary.valid && !gate_binary.state);
  // Same-value selection does not write the board at all.
  c17.request_auto_close(2); tick(c17,250); acknowledge(c17,changed);
  assert(settings_write_count()==1 && settings_result.state.find("no write needed")!=std::string::npos);
  // Mismatched readback reports actual values and never retries a write.
  reset(); esphome::ble_client::Parent p18; EyeOpenClient c18(false); c18.p=&p18; ready(c18);
  c18.request_auto_close(2); tick(c18,250); acknowledge(c18,base); tick(c18,250);
  acknowledge(c18,"ACK FUNCTION"); tick(c18,250); acknowledge(c18,base);
  assert(settings_write_count()==1 && setting_9.state=="Function OFF");
  assert(settings_result.state.find("Readback mismatch")!=std::string::npos);
  // Disconnect after submission cannot replay settings after recovery.
  reset(); esphome::ble_client::Parent p19; EyeOpenClient c19(false); c19.p=&p19; ready(c19);
  c19.request_auto_close(2); tick(c19,250); acknowledge(c19,base); tick(c19,250);
  event(c19,ESP_GATTC_CLOSE_EVT); clock_ms+=100000; c19.maintenance();
  assert(settings_write_count()==1);
  assert(settings_result.state.find("uncertain")!=std::string::npos);
  // A Stop or movement event before submission cancels the pending write.
  for(bool stop:{false,true}) {
    reset(); esphome::ble_client::Parent p20; EyeOpenClient c20(false); c20.p=&p20; ready(c20);
    c20.request_auto_close(2); tick(c20,250); acknowledge(c20,base);
    if(stop) c20.request("STOP"); else event(c20,ESP_GATTC_NOTIFY_EVT,42,"Opening");
    tick(c20,250); assert(settings_write_count()==0);
    if(stop) assert(writes.back().payload=="C=STOP;src=P9999999");
  }
  // Unknown metadata is readable, but cannot be silently lost by an edit.
  reset(); esphome::ble_client::Parent p21; EyeOpenClient c21(false); c21.p=&p21; ready(c21);
  c21.request_auto_close(2); tick(c21,250); acknowledge(c21,base+";future=keep"); tick(c21,250);
  assert(settings_write_count()==0 && settings_result.state.find("unknown extension")!=std::string::npos);
  // Automatic post-connect refresh is read-only and follows confirmed status.
  reset(); esphome::ble_client::Parent p22; EyeOpenClient c22; c22.p=&p22; ready(c22);
  tick(c22,500); assert(writes.back().payload=="C=READ FUNCTION;src=P9999999");
  acknowledge(c22,base); assert(setting_1.state=="Overcurrent" && setting_9.state=="Function OFF");
  assert(settings_write_count()==0);
  // Status/configuration turnaround is paced, rather than writing immediately
  // into the module's previous UART transaction.
  reset(); esphome::ble_client::Parent p23; EyeOpenClient c23(false); c23.p=&p23; ready(c23);
  const auto before_quiet=writes.size(); c23.request_auto_close(2);
  tick(c23,249); assert(writes.size()==before_quiet);
  tick(c23,1); assert(writes.size()==before_quiet+1);
  // A stale status result during a settings read must not drop healthy BLE.
  acknowledge(c23,"ACK STATUS:PED CLOSED,0");
  for(int i=0;i<2;++i) { tick(c23,750); event(c23,ESP_GATTC_READ_CHAR_EVT,106,"ACK STATUS:PED CLOSED,0"); }
  assert(p23.disconnections==0 && settings_write_count()==0 && gate_binary.valid);
  assert(settings_result.state.find("read did not match")!=std::string::npos);
  tick(c23,1000); assert(writes.back().payload=="C=READ STATUS;src=P9999999");
  // Lost readback after a successful write remains explicitly uncertain,
  // without resending the write or tearing down the GATT connection.
  reset(); esphome::ble_client::Parent p24; EyeOpenClient c24(false); c24.p=&p24; ready(c24);
  c24.request_auto_close(2); tick(c24,250); acknowledge(c24,base); tick(c24,250);
  acknowledge(c24,"ACK FUNCTION"); tick(c24,250); acknowledge(c24,"ACK FUNCTION");
  for(int i=0;i<2;++i) { tick(c24,750); event(c24,ESP_GATTC_READ_CHAR_EVT,106,"ACK FUNCTION"); }
  assert(p24.disconnections==0 && settings_write_count()==1);
  assert(settings_result.state.find("uncertain")!=std::string::npos);
  event(c24,ESP_GATTC_CLOSE_EVT);
  assert(settings_result.state.find("uncertain")!=std::string::npos);
  puts("Protocol, native cover and guarded settings transaction checks passed (mock Bluetooth/hash). ");
}
