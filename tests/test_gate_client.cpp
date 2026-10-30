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
enum esp_gattc_cb_event_t { ESP_GATTC_CLOSE_EVT, ESP_GATTC_SEARCH_CMPL_EVT,
  ESP_GATTC_REG_FOR_NOTIFY_EVT, ESP_GATTC_NOTIFY_EVT, ESP_GATTC_WRITE_CHAR_EVT, ESP_GATTC_READ_CHAR_EVT };
struct esp_ble_gattc_cb_param_t {
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
int profile_full_open_min=95,profile_closed_position=0; bool profile_supports_pedestrian=true;
Text control_result,poll_health; Number status_successes,unmatched_replies;
Text command_status,status_reply,protocol_result,c004_log_1,c004_log_2,c004_log_3,c004_last,ble_capture_status;
Number c004_packets; bool control_session=false,gate_listening=false;
uint32_t c004_count=0,capture_started_ms=0; std::vector<std::string> capture_records;
std::string gate_pin="0000";  // Synthetic fixture only.
#define id(x) x
namespace esphome {
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
int main() {
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
  reset(); esphome::ble_client::Parent parent; EyeOpenClient client; client.p=&parent;
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
  reset(); esphome::ble_client::Parent p2; EyeOpenClient c2; c2.p=&p2;
  c2.request("FULL OPEN"); discover(c2);
  event(c2,ESP_GATTC_READ_CHAR_EVT,46,"M=1;C=example");
  assert(writes.size()==1 && writes.back().handle==46);
  event(c2,ESP_GATTC_WRITE_CHAR_EVT,46); tick(c2,1000);
  event(c2,ESP_GATTC_READ_CHAR_EVT,46,"M=1;C=example");
  assert(writes.size()==1 && p2.disconnections==1);

  // A stop requested while authenticating replaces the unsent open request.
  reset(); esphome::ble_client::Parent p3; EyeOpenClient c3; c3.p=&p3;
  c3.request("FULL OPEN"); c3.request("STOP"); discover(c3); unlock_without_challenge(c3);
  assert(writes.size()==1 && writes.back().payload=="C=STOP;src=P9999999");
  acknowledge(c3,"C=ACK STOP"); tick(c3,120000); assert(p3.disconnections==1);

  // A mismatching old ACK never releases a waiting movement command.
  reset(); esphome::ble_client::Parent p4; EyeOpenClient c4; c4.p=&p4;
  c4.request("PED OPEN"); discover(c4); unlock_without_challenge(c4);
  acknowledge(c4,"C=ACK FULL OPEN");
  assert(writes.size()==1 && writes.back().payload=="C=READ STATUS;src=P9999999");
  for(int i=0;i<3;++i) { tick(c4,750); event(c4,ESP_GATTC_READ_CHAR_EVT,106,"C=ACK FULL OPEN"); }
  assert(writes.size()==1 && p4.disconnections>0);
  // A poll ignores unrelated lines until a matching status arrives; no C000 resend.
  reset(); esphome::ble_client::Parent p5; EyeOpenClient c5; c5.p=&p5;
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
  assert(p5.disconnections==0 && poll_health.state.find("failed 1/3")!=std::string::npos);
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

  // Three consecutive reply failures disconnect; no motion is queued or replayed.
  reset(); esphome::ble_client::Parent p6; EyeOpenClient c6; c6.p=&p6;
  c6.request("READ STATUS"); discover(c6); unlock_without_challenge(c6);
  acknowledge(c6,"C=ACK STATUS:PED CLOSED,0");
  for(int failure=0;failure<3;++failure) {
    tick(c6,failure==0?10000:(failure==1?1000:2000));
    acknowledge(c6,"C=unrelated");
    for(int i=0;i<2;++i) { tick(c6,750); event(c6,ESP_GATTC_READ_CHAR_EVT,106,"C=unrelated"); }
  }
  assert(p6.disconnections==1 && writes.size()==4);
  assert(control_result.state.empty());
  for(const auto &w:writes) assert(w.payload=="C=READ STATUS;src=P9999999");
  // Stop cannot get stranded behind a background poll whose GATT read times out.
  reset(); esphome::ble_client::Parent p7; EyeOpenClient c7; c7.p=&p7;
  c7.request("READ STATUS"); discover(c7); unlock_without_challenge(c7);
  acknowledge(c7,"C=ACK STATUS:PED CLOSED,0");
  tick(c7,10000); event(c7,ESP_GATTC_WRITE_CHAR_EVT,106); tick(c7,200);
  c7.request("STOP"); tick(c7,3000); tick(c7,50);
  assert(writes.size()==3 && writes.back().payload=="C=STOP;src=P9999999");
  acknowledge(c7,"C=ACK STOP"); assert(control_result.state=="Acknowledged: STOP");
  // Gate locking is independent of Bluetooth authentication; close and Stop remain allowed.
  reset(); esphome::ble_client::Parent p8; EyeOpenClient c8; c8.p=&p8;
  gate_lock_state.state="locked"; c8.request("PED OPEN");
  assert(writes.empty() && p8.connections==0 && control_result.state.find("Not sent")!=std::string::npos);
  gate_lock_state.state="unavailable"; c8.request("FULL OPEN"); assert(p8.connections==0);
  // Automatic reconnect can only issue READ STATUS, never an old movement command.
  reset(); esphome::ble_client::Parent p9; EyeOpenClient c9; c9.p=&p9;
  monitor_enabled=true; c9.maintenance(); assert(p9.connections==1);
  discover(c9); unlock_without_challenge(c9); acknowledge(c9,"C=ACK STATUS:PED CLOSED,0");
  event(c9,ESP_GATTC_CLOSE_EVT); assert(gate_state_source.state=="unavailable");
  clock_ms+=15000; c9.maintenance(); assert(p9.connections==2);
  discover(c9); unlock_without_challenge(c9); acknowledge(c9,"C=ACK STATUS:PED CLOSED,0");
  for(const auto &w:writes) assert(w.payload=="C=READ STATUS;src=P9999999");
  c9.request("DISCONNECT"); assert(!monitor_enabled);
  reset(); esphome::ble_client::Parent p10; EyeOpenClient c10; c10.p=&p10;
  profile_supports_pedestrian=false; c10.request("PED OPEN"); assert(p10.connections==0 && writes.empty());
  puts("Protocol and transaction checks passed, including unrelated replies, bounded polling/backoff/recovery and preserved movement results (mock Bluetooth/hash). ");
}
