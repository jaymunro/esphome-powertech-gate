// Embedded in the private ESPHome on_boot lambda. No credentials in this file.
class EyeOpenClient : public esphome::ble_client::BLEClientNode {
 public:
  EyeOpenClient() {
    this->node_state = esphome::esp32_ble_tracker::ClientState::IDLE;
    feedback_.set_profile({id(profile_model), id(profile_pedestrian_status),
      id(profile_full_open_min), id(profile_closed_position), id(profile_supports_pedestrian),
      id(profile_closed_tolerance)});
  }

  void maintenance() {
    feedback_.tick(millis()); publish_feedback();
    if (id(monitor_enabled) && !id(control_session) && !auth_blocked_ && due(millis(), next_reconnect_)) {
      next_reconnect_ = millis() + reconnect_delay_;
      request("READ STATUS");  // Reconnection can only request status, never replay motion.
    }
  }
  void request(const std::string &command) {
    if (command == "DISCONNECT") {
      id(monitor_enabled) = false;
      feedback_.disconnected(); publish_feedback();
      user_command_.clear(); urgent_stop_ = false;
      this->parent()->disconnect();
      return;
    }
    if ((command == "PED OPEN" || command == "PED CLOSE") && !id(profile_supports_pedestrian)) {
      id(control_result).publish_state("Not sent: controller profile has no pedestrian mode"); return;
    }
    if ((command == "FULL OPEN" || command == "PED OPEN") && id(require_gate_unlock) && id(gate_lock_state).state != "unlocked") {
      id(control_result).publish_state("Not sent: Home Assistant gate lock is locked or unavailable"); return;
    }
    auth_blocked_ = false;
    if (!allowed(command)) { status("Rejected: command is not allowlisted"); return; }
    if (busy_) {
      if (command == "STOP") {
        urgent_stop_ = true;
        status("Stop queued with priority after the active Bluetooth operation");
      } else if (background_poll_ && wire_command_ == "READ STATUS" &&
                 user_command_.empty() && command != "READ STATUS") {
        user_command_=command;
        status("Command queued after the in-flight status read");
      } else status("Busy; command was not queued or sent");
      return;
    }
    background_poll_ = false;
    last_user_request_ = millis();
    user_command_ = command; busy_ = true;
    if (authenticated_ && id(control_session)) { next_command(); return; }
    id(control_session) = true;
    id(gate_listening) = true;
    id(c004_count) = 0; id(c004_packets).publish_state(0);
    id(capture_started_ms) = millis(); id(capture_records).clear();
    id(c004_log_1).publish_state("No packets");
    id(c004_log_2).publish_state(""); id(c004_log_3).publish_state("");
    id(c004_last).publish_state("No notification received");
    id(ble_capture_status).publish_state("Connecting control and feedback session");
    baseline_needed_ = command != "READ STATUS";
    phase_ = CONNECTING; deadline_ = millis() + 12000;
    status("Connecting for " + command);
    this->parent()->connect();
  }

  void loop() override {
    if (!id(control_session)) return;
    const uint32_t now = millis();
    feedback_.tick(now); publish_feedback();
    if (phase_ == IDLE_CONNECTED) {
      if (!id(monitor_enabled) && uint32_t(now - last_user_request_) >= 120000) {
        status("Test session ended; disconnecting"); this->parent()->disconnect(); return;
      }
      if ((status_due_ && poll_failures_ == 0) || uint32_t(now - last_poll_) >= poll_delay(now)) {
        busy_ = true; background_poll_ = true; wire_command_ = "READ STATUS"; write_command();
      }
      return;
    }
    if (phase_ == AUTH_READ_WAIT && due(now, deadline_)) { read_auth(false); return; }
    if (phase_ == AUTH_VERIFY_WAIT && due(now, deadline_)) { read_auth(true); return; }
    if (phase_ == COMMAND_WAIT && due(now, deadline_)) { next_command(); return; }
    if (phase_ == REPLY_WAIT && due(now, deadline_)) {
      if (urgent_stop_ && wire_command_ != "STOP") {
        wire_command_.clear(); next_command(); return;
      }
      phase_ = REPLY_READ; deadline_ = now + 3000;
      const auto err = esp_ble_gattc_read_char(this->parent()->get_gattc_if(),
          this->parent()->get_conn_id(), command_handle_, ESP_GATT_AUTH_REQ_NONE);
      if (err != ESP_OK) fail("Could not request UART reply; command not retried");
      return;
    }
    if (phase_ != IDLE && due(now, deadline_)) fail("Bluetooth operation timed out; command not retried");
  }

  void gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t /*iface*/,
                           esp_ble_gattc_cb_param_t *param) override {
    if (event == ESP_GATTC_CONNECT_EVT) { id(bluetooth_connection).publish_state(true); }
    if (event == ESP_GATTC_CLOSE_EVT) {
      ESP_LOGW("gate_link", "Bluetooth closed: reason=%d phase=%d poll_failures=%u",
               int(param->close.reason), int(phase_), poll_failures_);
      id(bluetooth_connection).publish_state(false);
      const bool uncertain = busy_ && wire_command_ != "READ STATUS" && (phase_ == COMMAND_WRITE || phase_ == REPLY_READ || phase_ == REPLY_WAIT);
      feedback_.disconnected(); publish_feedback();
      next_reconnect_ = millis() + reconnect_delay_;
      authenticated_ = false; busy_ = false; urgent_stop_ = false;
      user_command_.clear(); wire_command_.clear(); challenge_.clear();
      phase_ = IDLE; auth_handle_ = command_handle_ = result_handle_ = 0;
      id(control_session) = false;
      this->node_state = esphome::esp32_ble_tracker::ClientState::IDLE;
      if (uncertain) {
        status("Disconnected; command outcome uncertain; no replay scheduled");
        id(control_result).publish_state(status_text_);
      }
      id(poll_health).publish_state("Disconnected; status freshness is not maintained");
      return;
    }
    if (event == ESP_GATTC_SEARCH_CMPL_EVT) {
      auto *auth = this->parent()->get_characteristic(0xA001, 0xB001);
      auto *command = this->parent()->get_characteristic(0xA001, 0xC000);
      auto *result = this->parent()->get_characteristic(0xA001, 0xB000);
      auto *events = this->parent()->get_characteristic(0xA001, 0xC004);
      event_handle_ = events ? events->handle : 0;
      feedback_.connected(millis()); publish_feedback();
      auth_handle_ = auth ? auth->handle : 0;
      command_handle_ = command ? command->handle : 0;
      result_handle_ = result ? result->handle : 0;
      if (!id(control_session)) {
        this->node_state = esphome::esp32_ble_tracker::ClientState::ESTABLISHED; return;
      }
      if (!auth_handle_ || !command_handle_ || !result_handle_) { fail("Required gate service is missing"); return; }
      phase_ = SUBSCRIBING; deadline_ = millis() + 5000;
      if (this->parent()->register_for_notify(result_handle_) != ESP_OK) fail("Cannot subscribe to module results");
      return;
    }
    if (!id(control_session)) return;
    if (event == ESP_GATTC_NOTIFY_EVT && event_handle_ && param->notify.handle == event_handle_) {
      feedback_.event(millis()); kick_poll(millis()); publish_feedback();
      return;  // Native C004 receiver retains the diagnostic bytes separately.
    }
    if (event == ESP_GATTC_REG_FOR_NOTIFY_EVT && param->reg_for_notify.handle == result_handle_) {
      if (param->reg_for_notify.status != ESP_GATT_OK) { fail("Module result subscription failed"); return; }
      this->node_state = esphome::esp32_ble_tracker::ClientState::ESTABLISHED;
      phase_ = AUTH_READ_WAIT; deadline_ = millis() + 200;
      return;
    }
    if (event == ESP_GATTC_NOTIFY_EVT && param->notify.handle == result_handle_) {
      const std::string value(reinterpret_cast<const char *>(param->notify.value), param->notify.value_len);
      const int c = integer_field(value, "C"), a = integer_field(value, "A"), r = integer_field(value, "R");
      char safe[80]; snprintf(safe, sizeof(safe), "characteristic=%d action=%d result=%d", c, a, r);
      id(protocol_result).publish_state(safe);
      if (r >= 10 && ((c == 0xB001 && (phase_ == AUTH_WRITE || phase_ == AUTH_VERIFY_WAIT || phase_ == AUTH_VERIFY_READ)) ||
          (c == 0xC000 && (phase_ == COMMAND_WRITE || phase_ == REPLY_WAIT || phase_ == REPLY_READ)))) fail("Module rejected the operation (result " + std::to_string(r) + ")");
      return;
    }
    if (event == ESP_GATTC_WRITE_CHAR_EVT && param->write.conn_id == this->parent()->get_conn_id()) {
      if (param->write.handle == auth_handle_ && phase_ == AUTH_WRITE) {
        if (param->write.status != ESP_GATT_OK) { fail("Authentication write failed"); return; }
        phase_ = AUTH_VERIFY_WAIT; deadline_ = millis() + 1000;
      } else if (param->write.handle == command_handle_ && phase_ == COMMAND_WRITE) {
        if (param->write.status != ESP_GATT_OK) { fail("Command write failed; no retry scheduled"); return; }
        write_confirmed_ = true; read_attempts_ = 0;
        phase_ = REPLY_WAIT; deadline_ = millis() + 200;
      }
      return;
    }
    if (event == ESP_GATTC_READ_CHAR_EVT && param->read.conn_id == this->parent()->get_conn_id()) {
      if (param->read.status != ESP_GATT_OK) { fail("Bluetooth characteristic read failed"); return; }
      const std::string value(reinterpret_cast<const char *>(param->read.value), param->read.value_len);
      if (param->read.handle == auth_handle_ && (phase_ == AUTH_READ || phase_ == AUTH_VERIFY_READ)) {
        const int mode = integer_field(value, "M");
        if (mode == 0) {
          authenticated_ = true; challenge_.clear();
          phase_ = COMMAND_WAIT; deadline_ = millis() + 100;
        } else if (mode == 1 && phase_ == AUTH_READ) {
          if (!url_decode(field(value, "C"), challenge_) || challenge_.empty()) { fail("Invalid authentication challenge"); return; }
          const std::string input = id(gate_device_name).substr(0, 10) + id(gate_pin) + challenge_;
          esphome::md5::MD5Digest digest; digest.init(); digest.add(input.data(), input.size()); digest.calculate();
          uint8_t bytes[16]; digest.get_bytes(bytes);
          const auto key = esphome::base64_encode(bytes, sizeof(bytes)).substr(0, 16);
          const std::string response = "M=0;K=" + key;
          challenge_.clear(); phase_ = AUTH_WRITE; deadline_ = millis() + 3000;
          status("Authenticating with existing PIN");
          if (write(auth_handle_, response) != ESP_OK) fail("Could not send authentication response");
        } else fail("Authentication was not confirmed; no gate command sent");
      } else if (param->read.handle == command_handle_ && phase_ == REPLY_READ) {
        const auto reply = value.rfind("C=", 0) == 0 ? value.substr(2) : value;
        id(status_reply).publish_state(reply.substr(0, 250));
        if (write_confirmed_ && matching_ack(reply, wire_command_)) {
          const auto finished = wire_command_;
          if (finished == "READ STATUS") {
            last_poll_ = millis(); poll_interval_ = 2000; poll_failures_ = 0; reconnect_delay_ = 15000;
            GateFeedback::Sample sample;
            if (GateFeedback::parse_reply(reply, sample)) feedback_.accept(sample, millis());
            publish_feedback();
            id(poll_health).publish_state("Status reply received");
            id(status_successes).publish_state(++status_successes_);
          } else {
            id(control_result).publish_state("Acknowledged: " + finished);
            if (finished == "PED OPEN" || finished == "FULL OPEN")
              feedback_.commanded_open(finished == "PED OPEN", millis());
            else { feedback_.clear_command_hint(); feedback_.event(millis()); }
            kick_poll(millis()); publish_feedback();
          }
          wire_command_.clear();
          status("Acknowledged: " + finished);
          if (urgent_stop_ || !user_command_.empty()) { phase_ = COMMAND_WAIT; deadline_ = millis() + 50; }
          else { busy_ = false; phase_ = IDLE_CONNECTED; }
        } else if (urgent_stop_ && wire_command_ != "STOP") {
          wire_command_.clear(); phase_ = COMMAND_WAIT; deadline_ = millis() + 50;
        } else if (++read_attempts_ < (wire_command_ == "READ STATUS" ? 3u : 10u)) {
          if (wire_command_ == "READ STATUS") {
            id(poll_health).publish_state("Waiting for status; unrelated or incomplete reply ignored");
            id(unmatched_replies).publish_state(++unmatched_replies_);
          }
          phase_ = REPLY_WAIT; deadline_ = millis() + 750;
        } else fail(wire_command_ == "READ STATUS" ? "No matching status reply" :
                    "No matching UART acknowledgement; command outcome uncertain; not retried");
      }
    }
  }

  static bool allowed(const std::string &c) {
    return c == "READ STATUS" || c == "FULL OPEN" || c == "FULL CLOSE" ||
           c == "PED OPEN" || c == "PED CLOSE" || c == "STOP";
  }
  static bool matching_ack(const std::string &reply, const std::string &command) {
    if (command == "READ STATUS") {
      GateFeedback::Sample sample; return GateFeedback::parse_reply(reply, sample);
    }
    const std::string prefix = "ACK " + command;
    size_t start = 0;
    while (start < reply.size()) {
      const auto end = reply.find_first_of("\r\n", start);
      const auto line = reply.substr(start, end == std::string::npos ? end : end - start);
      if (line.rfind(prefix, 0) == 0 && (line.size() == prefix.size() ||
          line[prefix.size()] == ':' || line[prefix.size()] == ' ' || line[prefix.size()] == ';')) return true;
      if (command == "READ STATUS" && line.rfind("ACK READ STATUS", 0) == 0 &&
          (line.size() == 15 || line[15] == ':' || line[15] == ' ' || line[15] == ';')) return true;
      if (end == std::string::npos) break;
      start = end + 1;
    }
    return false;
  }
  static std::string field(const std::string &value, const std::string &key) {
    size_t start = 0;
    while (start < value.size()) {
      const auto end = value.find(';', start);
      const auto part = value.substr(start, end == std::string::npos ? end : end - start);
      if (part.rfind(key + "=", 0) == 0) return part.substr(key.size() + 1);
      if (end == std::string::npos) break;
      start = end + 1;
    }
    return "";
  }
  static int integer_field(const std::string &value, const std::string &key) {
    const auto text = field(value, key);
    if (text.empty() || text.size() > 9 || !std::all_of(text.begin(), text.end(), [](char c) { return c >= '0' && c <= '9'; })) return -1;
    return atoi(text.c_str());
  }
  static bool url_decode(const std::string &in, std::string &out) {
    out.clear();
    auto digit = [](char c) -> int {
      if (c >= '0' && c <= '9') return c - '0';
      if (c >= 'a' && c <= 'f') return c - 'a' + 10;
      if (c >= 'A' && c <= 'F') return c - 'A' + 10;
      return -1;
    };
    for (size_t i = 0; i < in.size(); ++i) {
      if (in[i] == '%') {
        if (i + 2 >= in.size()) return false;
        const int a = digit(in[i + 1]), b = digit(in[i + 2]);
        if (a < 0 || b < 0) return false;
        out += char(a * 16 + b); i += 2;
      } else out += in[i] == '+' ? ' ' : in[i];
    }
    return true;
  }

 private:
  enum Phase { IDLE, CONNECTING, SUBSCRIBING, AUTH_READ_WAIT, AUTH_READ,
    AUTH_WRITE, AUTH_VERIFY_WAIT, AUTH_VERIFY_READ, COMMAND_WAIT,
    COMMAND_WRITE, REPLY_WAIT, REPLY_READ, IDLE_CONNECTED };
  Phase phase_{IDLE};
  GateFeedback feedback_;
  bool auth_blocked_{false};
  uint16_t event_handle_{0};
  uint32_t next_reconnect_{0}, reconnect_delay_{15000};
  static void publish_cover(esphome::cover::Cover *cover, const std::string &state) {
    // Retain the entire last confirmed state while feedback is unconfirmed.
    // Clearing motion here would turn Opening at position 0 into Closed,
    // or Closing at position 1 into Open. Gate state is the validity guard.
    if (state == "unavailable") return;
    auto operation = esphome::cover::COVER_OPERATION_IDLE;
    float position = cover->position;
    if (state == "closed") position = esphome::cover::COVER_CLOSED;
    else if (state == "open") position = esphome::cover::COVER_OPEN;
    else if (state == "opening") operation = esphome::cover::COVER_OPERATION_OPENING;
    else if (state == "closing") operation = esphome::cover::COVER_OPERATION_CLOSING;
    if (cover->position != position || cover->current_operation != operation) {
      cover->position = position; cover->current_operation = operation;
      cover->publish_state(false);  // Do not persist presumed gate position.
    }
  }
  void publish_feedback() {
    publish_cover(id(car_gate_cover), feedback_.car());
    publish_cover(id(pedestrian_gate_cover), feedback_.pedestrian());
    if (feedback_.gate() == "unavailable") id(gate_state).invalidate_state();
    else id(gate_state).publish_state(feedback_.gate() == "open");
    if (id(gate_state_source).state != feedback_.gate()) id(gate_state_source).publish_state(feedback_.gate());
    if (id(car_state_source).state != feedback_.car()) id(car_state_source).publish_state(feedback_.car());
    if (id(pedestrian_state_source).state != feedback_.pedestrian()) id(pedestrian_state_source).publish_state(feedback_.pedestrian());
  }
  bool authenticated_{false}, busy_{false}, urgent_stop_{false};
  bool baseline_needed_{false}, write_confirmed_{false}, background_poll_{false};
  uint16_t auth_handle_{0}, command_handle_{0}, result_handle_{0};
  uint32_t deadline_{0}, last_user_request_{0}, last_poll_{0};
  unsigned read_attempts_{0}, poll_failures_{0}, status_successes_{0}, unmatched_replies_{0};
  uint32_t poll_interval_{2000}, fast_until_{0};
  bool fast_window_{false}, status_due_{false};
  void kick_poll(uint32_t now) { fast_window_=true; fast_until_=now+30000; status_due_=true; }
  uint32_t poll_delay(uint32_t now) {
    if (poll_failures_) return poll_interval_;
    if (fast_window_ && due(now,fast_until_)) fast_window_=false;
    return (!feedback_.available() || feedback_.gate() != "closed" || fast_window_) ? 500 : 2000;
  }
  std::string status_text_;
  std::string user_command_, wire_command_, challenge_;
  static bool due(uint32_t now, uint32_t target) { return int32_t(now - target) >= 0; }
  void status(const std::string &message) { status_text_ = message; id(command_status).publish_state(message); }
  void fail(const std::string &message) {
    if (message.find("Authentication was not confirmed") != std::string::npos ||
        message.find("result 14") != std::string::npos || message.find("result 19") != std::string::npos)
      auth_blocked_ = true;  // No unattended repeated PIN attempts after rejection.
    // An unrelated UART response on a successful GATT read is not link loss.
    // Cloud/app traffic can overwrite the shared UART result characteristic.
    if (background_poll_ && wire_command_ == "READ STATUS" && authenticated_ &&
        phase_ == REPLY_READ && (message == "No matching status reply" ||
          (urgent_stop_ && message == "Bluetooth operation timed out; command not retried"))) {
      ++poll_failures_; last_poll_ = millis();
      poll_interval_ = poll_failures_ == 1 ? 1000 : (poll_failures_ == 2 ? 2000 : 5000);
      id(poll_health).publish_state("Status reply mismatch " + std::to_string(poll_failures_) +
          ": retaining Bluetooth; retrying status only");
      status("Status reply mismatch; no movement command was retried");
      wire_command_.clear(); busy_ = false;
      if (urgent_stop_ || !user_command_.empty()) {
        background_poll_ = false; busy_ = true;
        phase_ = COMMAND_WAIT; deadline_ = millis() + 50; return;
      }
      phase_ = IDLE_CONNECTED; return;
    }
    ESP_LOGW("gate_link", "Operation failed: %s (phase=%d)", message.c_str(), int(phase_));
    next_reconnect_ = millis() + reconnect_delay_;
    reconnect_delay_ = std::min(uint32_t(120000), reconnect_delay_ * 2);
    if (wire_command_ == "READ STATUS") {
      id(poll_health).publish_state("Status check failed: " + message);
      if (!user_command_.empty() && user_command_ != "READ STATUS")
        id(control_result).publish_state("Not sent: baseline status check failed");
    } else if (!wire_command_.empty() && allowed(wire_command_)) {
      id(control_result).publish_state(message);
    }
    status(message); busy_ = false; user_command_.clear(); urgent_stop_ = false;
    feedback_.disconnected(); publish_feedback();
    phase_ = IDLE; this->parent()->disconnect();
  }
  esp_err_t write(uint16_t handle, const std::string &value) {
    return esp_ble_gattc_write_char(this->parent()->get_gattc_if(), this->parent()->get_conn_id(),
      handle, value.size(), reinterpret_cast<uint8_t *>(const_cast<char *>(value.data())),
      ESP_GATT_WRITE_TYPE_RSP, ESP_GATT_AUTH_REQ_NONE);
  }
  void read_auth(bool verify) {
    phase_ = verify ? AUTH_VERIFY_READ : AUTH_READ; deadline_ = millis() + 4000;
    if (esp_ble_gattc_read_char(this->parent()->get_gattc_if(), this->parent()->get_conn_id(),
        auth_handle_, ESP_GATT_AUTH_REQ_NONE) != ESP_OK) fail("Could not read authentication state");
  }
  void next_command() {
    if (!authenticated_) { fail("Authentication required; no command sent"); return; }
    if (urgent_stop_) { wire_command_ = "STOP"; urgent_stop_ = false; user_command_.clear(); }
    else if (baseline_needed_ && user_command_ != "READ STATUS") {
      wire_command_ = "READ STATUS"; baseline_needed_ = false;
    } else { wire_command_ = user_command_; user_command_.clear(); }
    if (!allowed(wire_command_)) { fail("Invalid command; nothing sent"); return; }
    if ((wire_command_ == "PED OPEN" || wire_command_ == "FULL OPEN") &&
        id(require_gate_unlock) && id(gate_lock_state).state != "unlocked") {
      id(control_result).publish_state("Not sent: gate lock changed while command was queued");
      wire_command_.clear(); busy_=false; phase_=IDLE_CONNECTED; return;
    }
    write_command();
  }
  void write_command() {
    phase_ = COMMAND_WRITE; deadline_ = millis() + 4000; write_confirmed_ = false;
    status("Sending: " + wire_command_);
    if (wire_command_ == "READ STATUS") { status_due_=false; id(poll_health).publish_state("Requesting status"); }
    else id(control_result).publish_state("Sending: " + wire_command_);
    if (write(command_handle_, "C=" + wire_command_ + ";src=P9999999") != ESP_OK)
      fail("Could not submit command; no retry scheduled");
  }
};
