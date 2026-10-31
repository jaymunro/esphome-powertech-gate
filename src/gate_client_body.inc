// Embedded in the private ESPHome on_boot lambda. No credentials in this file.
class EyeOpenClient : public esphome::ble_client::BLEClientNode {
 public:
  explicit EyeOpenClient(bool auto_settings=true) {
    auto_settings_=auto_settings; settings_due_=auto_settings;
    next_reconnect_=millis(); next_settings_refresh_=millis();
    this->node_state = esphome::esp32_ble_tracker::ClientState::IDLE;
    feedback_.set_profile({id(profile_model), id(profile_pedestrian_status),
      id(profile_full_open_min), id(profile_closed_position), id(profile_supports_pedestrian),
      id(profile_closed_tolerance)});
  }

  void maintenance() {
    // ESPHome disables its idle BLE-client loop. Keep our connection deadline
    // alive independently, including when a connection produced no GATT event.
    if(id(control_session)) loop();
    feedback_.tick(millis()); publish_feedback();
    if (id(monitor_enabled) && !id(control_session) && !auth_blocked_ && due(millis(), next_reconnect_)) {
      next_reconnect_ = millis() + reconnect_delay_;
      request("READ STATUS");  // Reconnection can only request status, never replay motion.
    }
  }
  void request_auto_close(int index) {
    if(index<0 || index>=int(GateSettings::fields()[8].options.size())) return;
    request_setting('9',GateSettings::fields()[8].options[index]);
  }
  void request_setting(char field,const std::string &option) {
    if(settings_uncertain_) {
      id(settings_result).publish_state("Not sent: refresh configuration to resolve the previous uncertain save"); return;
    }
    if(id(profile_model)!="PS19001" || !GateSettings::option_valid(field,option) || edit_stage_!=EDIT_IDLE ||
       !feedback_.confirmed_closed() || !authenticated_ ||
       (busy_ && !(background_poll_ && wire_command_=="READ STATUS" && user_command_.empty()))) {
      id(settings_result).publish_state("Not sent: wait for a confirmed closed gate and idle connection"); return;
    }
    desired_field_=field; desired_option_=option; edit_stage_=EDIT_BASELINE; settings_read_retries_=0;
    id(settings_result).publish_state("Reading fresh configuration before changing setting");
    request("READ FUNCTION");
  }
  void request(const std::string &command) {
    if(edit_stage_!=EDIT_IDLE && command!="READ FUNCTION" && command!="READ STATUS" && command!="STOP" && command!="DISCONNECT") {
      status("Settings change active; wait for verified readback"); return;
    }
    if (command == "READ FUNCTION" && id(profile_model) != "PS19001") {
      status("Settings read not validated for this controller profile"); return;
    }
    if (command == "DISCONNECT") {
      cancel_settings(settings_write_sent_ ? "Settings outcome uncertain after disconnect; refresh settings; not retried" : "Settings cancelled by disconnect; not retried");
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
    if(!id(control_session) && this->parent()->get_gattc_if()==ESP_GATT_IF_NONE) {
      status("Waiting for Bluetooth GATT registration; no command sent");
      next_reconnect_=millis()+1000; return;
    }
    auth_blocked_ = false;
    if (!allowed(command)) { status("Rejected: command is not allowlisted"); return; }
    if (busy_) {
      if (command == "STOP") {
        cancel_settings("Interrupted by Stop; settings outcome may be uncertain; refresh settings");
        urgent_stop_ = true;
        status("Stop queued with priority after the active Bluetooth operation");
      } else if (((background_poll_ && wire_command_ == "READ STATUS") ||
                  (wire_command_=="READ FUNCTION" && edit_stage_==EDIT_IDLE)) &&
                 user_command_.empty() && command != "READ STATUS") {
        user_command_=command;
        status("Command queued after the in-flight read");
      } else status("Busy; command was not queued or sent");
      return;
    }
    if(command=="READ FUNCTION") settings_read_retries_=0;
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
      if(auto_settings_ && due(now,next_settings_refresh_)) settings_due_=true;
      if(settings_due_ && edit_stage_==EDIT_IDLE && feedback_.confirmed_closed() &&
         uint32_t(now-last_poll_)>=500 && id(profile_model)=="PS19001") {
        settings_due_=false; settings_read_retries_=0;
        next_settings_refresh_=now+300000;
        busy_=true; background_poll_=false; wire_command_="READ FUNCTION"; write_command(); return;
      }
      if (!id(monitor_enabled) && uint32_t(now - last_user_request_) >= 120000) {
        status("Test session ended; disconnecting"); this->parent()->disconnect(); return;
      }
      if ((status_due_ && poll_failures_ == 0) || uint32_t(now - last_poll_) >= poll_delay(now)) {
        busy_ = true; background_poll_ = true; wire_command_ = "READ STATUS"; write_command();
      }
      return;
    }
    if (phase_ == SETTINGS_RETRY_WAIT && due(now,deadline_)) {
      if(urgent_stop_) next_command();
      else { wire_command_="READ FUNCTION"; write_command(); }
      return;
    }
    if (phase_ == AUTH_READ_WAIT && due(now, deadline_)) { read_auth(false); return; }
    if (phase_ == QUIET_WAIT && due(now,deadline_)) {
      if(urgent_stop_) next_command(); else write_command(); return;
    }
    if (phase_ == AUTH_VERIFY_WAIT && due(now, deadline_)) { read_auth(true); return; }
    if (phase_ == COMMAND_WAIT && due(now, deadline_)) { next_command(); return; }
    if(phase_==REPLY_WAIT && urgent_stop_ && wire_command_!="STOP") {
      wire_command_.clear(); next_command(); return;
    }
    if (phase_ == REPLY_WAIT && due(now, deadline_)) {
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
      const bool settings_active=edit_stage_!=EDIT_IDLE;
      cancel_settings(settings_write_sent_ ? "Settings outcome uncertain after Bluetooth loss; refresh settings; not retried" : "Bluetooth disconnected before settings write; not retried");
      settings_due_=auto_settings_;
      ESP_LOGW("gate_link", "Bluetooth closed: reason=%d phase=%d poll_failures=%u",
               int(param->close.reason), int(phase_), poll_failures_);
      id(bluetooth_connection).publish_state(false);
      if(settings_uncertain_) id(settings_result).publish_state("Settings outcome uncertain; refresh to verify; write not retried");
      else if(!settings_active) id(settings_result).publish_state("Bluetooth disconnected; values retained from last read");
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
        // This is only an access-mode hint; fresh status still proves movement.
        // Preserve it even if another client overwrites the UART acknowledgement.
        if (wire_command_=="PED OPEN" || wire_command_=="FULL OPEN")
          feedback_.commanded_open(wire_command_=="PED OPEN",millis());
        if (movement_command(wire_command_)) motion_ack_deadline_=millis()+2000;
        phase_ = REPLY_WAIT;
        deadline_ = millis() + (wire_command_=="READ FUNCTION" ? 600 : (movement_command(wire_command_) ? 0 : 200));
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
          last_uart_reply_=millis();
          const auto finished = wire_command_;
          if (finished == "READ STATUS") {
            last_poll_ = millis(); poll_interval_ = 1000; poll_failures_ = 0; reconnect_delay_ = 15000;
            GateFeedback::Sample sample;
            if (GateFeedback::parse_reply(reply, sample)) feedback_.accept(sample, millis());
            publish_feedback();
            id(poll_health).publish_state("Status reply received");
            id(status_successes).publish_state(++status_successes_);
          } else if (finished == "READ FUNCTION") {
            std::string snapshot;
            if (function_reply(reply, snapshot)) {
              id(settings_snapshot).publish_state(snapshot);
              accept_settings(snapshot);
            }
          } else if(edit_stage_==EDIT_WRITE && finished==settings_wire_) {
            edit_stage_=EDIT_VERIFY; settings_read_retries_=0;
            id(settings_result).publish_state("Write acknowledged; verifying full configuration readback");
          } else if(finished.rfind("WRITE FUNCTION,",0)==0) {
            id(settings_result).publish_state("Write acknowledged after interruption; refresh settings");
          } else {
            id(control_result).publish_state("Acknowledged: " + finished);
            if (finished == "PED OPEN" || finished == "FULL OPEN")
              feedback_.commanded_open(finished == "PED OPEN", millis());
            else { feedback_.clear_command_hint(); feedback_.event(millis()); }
            kick_poll(millis()); publish_feedback();
          }
          wire_command_.clear();
          status("Acknowledged: " + finished);
          if (finished=="READ STATUS" && !user_command_.empty() && !baseline_needed_) {
            next_command();  // The read completed; no extra queue delay is needed.
          } else if (urgent_stop_ || !user_command_.empty() || edit_stage_==EDIT_WRITE || edit_stage_==EDIT_VERIFY) { phase_ = COMMAND_WAIT; deadline_ = millis() + 50; }
          else { busy_ = false; phase_ = IDLE_CONNECTED; }
        } else if (urgent_stop_ && wire_command_ != "STOP") {
          wire_command_.clear(); phase_ = COMMAND_WAIT; deadline_ = millis() + 50;
        } else if (background_poll_ && wire_command_=="READ STATUS" && !user_command_.empty() &&
                   authenticated_ && feedback_.available() && !baseline_needed_) {
          // A read has completed with an unrelated reply. Do not make a queued
          // user wait through background-only rereads; never bypass reconnect
          // baseline validation or interrupt an outstanding GATT operation.
          wire_command_.clear(); next_command();
        } else if (movement_command(wire_command_) && write_confirmed_) {
          // Read again as soon as this completed read allows. No fixed pause,
          // no concurrent GATT reads, and no movement retransmission.
          if (++read_attempts_<12 && !due(millis(),motion_ack_deadline_)) {
            phase_=REPLY_WAIT; deadline_=millis();
          } else fail("No matching UART acknowledgement; command outcome uncertain; not retried");
        } else if (++read_attempts_ < (wire_command_=="READ FUNCTION" ? 5u : (wire_command_=="READ STATUS" ? 3u : 10u))) {
          if (wire_command_ == "READ STATUS") {
            id(poll_health).publish_state("Waiting for status; unrelated or incomplete reply ignored");
            id(unmatched_replies).publish_state(++unmatched_replies_);
          }
          phase_ = REPLY_WAIT; deadline_ = millis() + (wire_command_=="READ FUNCTION" ? 500 : 750);
        } else fail(wire_command_ == "READ STATUS" ? "No matching status reply" :
                    "No matching UART acknowledgement; command outcome uncertain; not retried");
      }
    }
  }

  static bool allowed(const std::string &c) {
    return c == "READ STATUS" || c == "READ FUNCTION" || c == "FULL OPEN" || c == "FULL CLOSE" ||
           c == "PED OPEN" || c == "PED CLOSE" || c == "STOP";
  }
  static bool movement_command(const std::string &command) {
    return allowed(command) && command!="READ STATUS" && command!="READ FUNCTION";
  }
  static bool matching_ack(const std::string &reply, const std::string &command) {
    if(command.rfind("WRITE FUNCTION,",0)==0) {
      bool matched=false;
      size_t start=0;
      while(start<reply.size()) {
        const auto end=reply.find_first_of("\r\n",start);
        auto line=reply.substr(start,end==std::string::npos?end:end-start);
        if(line.rfind("C=",0)==0) line.erase(0,2);
        line=line.substr(0,line.find(';'));
        if(line=="ACK FUNCTION" || line.rfind("ACK FUNCTION,",0)==0) {
          if(line!="ACK FUNCTION" && line!="ACK FUNCTION"+command.substr(14)) return false;
          matched=true;
        }
        if(end==std::string::npos) break;
        start=end+1;
      }
      return matched;
    }
    if (command == "READ FUNCTION") { std::string snapshot; GateSettings settings; return function_reply(reply, snapshot) && settings.decode(snapshot); }
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
  // Capture a complete legacy read response without interpreting or rewriting
  // its fields. Unknown fields and suffixes are deliberately preserved.
  static bool function_reply(const std::string &reply, std::string &out) {
    out.clear();
    if (reply.size() > 2048) return false;
    size_t start = 0;
    while (start < reply.size()) {
      const auto end = reply.find_first_of("\r\n", start);
      auto line = reply.substr(start, end == std::string::npos ? end : end-start);
      if (line.rfind("C=",0)==0) line.erase(0,2);
      if (line.rfind("ACK READ FUNCTION",0)==0 && line.size()>17 &&
          (line[17]==',' || line[17]==':')) {
        if (line.size() <= 18 || !std::all_of(line.begin(),line.end(),
            [](unsigned char c){return c>=32 && c<=126;})) { out.clear(); return false; }
        if (!out.empty() && out != line) { out.clear(); return false; }
        out=line;
      }
      if (end==std::string::npos) break;
      start=end+1;
    }
    return !out.empty();
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
    COMMAND_WRITE, REPLY_WAIT, REPLY_READ, IDLE_CONNECTED, QUIET_WAIT, SETTINGS_RETRY_WAIT };
  Phase phase_{IDLE};
  GateFeedback feedback_;
  enum EditStage { EDIT_IDLE, EDIT_BASELINE, EDIT_WRITE, EDIT_VERIFY };
  EditStage edit_stage_{EDIT_IDLE};
  char desired_field_{0};
  std::string desired_option_;
  unsigned settings_read_retries_{0};
  bool auto_settings_{true}, settings_due_{false}, settings_write_sent_{false}, settings_uncertain_{false};
  std::string settings_wire_;
  uint32_t last_uart_reply_{0}, next_settings_refresh_{0};
  GateSettings edit_baseline_;
  void cancel_settings(const std::string &message) {
    if(edit_stage_!=EDIT_IDLE && settings_write_sent_) settings_uncertain_=true;
    if(edit_stage_!=EDIT_IDLE) id(settings_result).publish_state(message);
    edit_stage_=EDIT_IDLE; desired_field_=0; desired_option_.clear(); settings_read_retries_=0; settings_wire_.clear(); settings_write_sent_=false;
  }
  void accept_settings(const std::string &snapshot) {
    settings_due_=false; next_settings_refresh_=millis()+300000;
    GateSettings settings;
    if(!settings.decode(snapshot)) {
      cancel_settings("Invalid settings snapshot; no further write scheduled");
      id(settings_result).publish_state("Unrecognized settings values; configuration entities not updated"); return;
    }
    settings_uncertain_=false; // A complete fresh read establishes actual values.
    // During a write, publish the selector only after verified readback.
    if(edit_stage_!=EDIT_BASELINE) settings.publish();
    if(edit_stage_==EDIT_BASELINE) {
      if(settings.value_for(desired_field_)==desired_option_) {
        settings.publish(); cancel_settings("Already set; no write needed"); return;
      }
      edit_baseline_=settings;
      settings_wire_=settings.change_command(desired_field_,desired_option_);
      if(settings_wire_.empty() || !feedback_.confirmed_closed()) {
        settings.publish(); cancel_settings("Not sent: incompatible option, dependent current threshold, unknown extension or gate no longer confirmed closed"); return;
      }
      edit_stage_=EDIT_WRITE; settings_read_retries_=0;
    } else if(edit_stage_==EDIT_VERIFY) {
      const bool matches=settings.matches_command(settings_wire_) && settings.same_metadata(edit_baseline_);
      cancel_settings(matches ? "Setting saved and complete readback verified" : "Readback mismatch; actual values shown; not retried");
      settings_uncertain_=false;
    } else id(settings_result).publish_state("Configuration read and validated");
  }
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
    const float position=feedback_.position();
    if (position >= 0) {
      if (id(gate_position).state != position) id(gate_position).publish_state(position);
    } else if (!std::isnan(id(gate_position).state)) id(gate_position).publish_state(NAN);
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
  uint32_t deadline_{0}, last_user_request_{0}, last_poll_{0}, motion_ack_deadline_{0};
  unsigned read_attempts_{0}, poll_failures_{0}, status_successes_{0}, unmatched_replies_{0};
  uint32_t poll_interval_{1000}, fast_until_{0};
  bool fast_window_{false}, status_due_{false};
  void kick_poll(uint32_t now) { fast_window_=true; fast_until_=now+30000; status_due_=true; }
  uint32_t poll_delay(uint32_t now) {
    if (poll_failures_) return poll_interval_;
    if (fast_window_ && due(now,fast_until_)) fast_window_=false;
    return (!feedback_.available() || feedback_.gate() != "closed" || fast_window_) ? 150 : 1000;
  }
  std::string status_text_;
  std::string user_command_, wire_command_, challenge_;
  static bool due(uint32_t now, uint32_t target) { return int32_t(now - target) >= 0; }
  void status(const std::string &message) { status_text_ = message; id(command_status).publish_state(message); }
  void fail(const std::string &message) {
    const bool reply_mismatch=phase_==REPLY_READ &&
      message=="No matching UART acknowledgement; command outcome uncertain; not retried";
    if(reply_mismatch && write_confirmed_ && authenticated_ && movement_command(wire_command_)) {
      id(control_result).publish_state("Acknowledgement missing for " + wire_command_ + "; reading status; command not retried");
      status("Movement acknowledgement uncertain; recovering with status only");
      wire_command_.clear(); busy_=false; phase_=IDLE_CONNECTED;
      kick_poll(millis()); publish_feedback(); return;
    }
    if(reply_mismatch && settings_write_sent_ && edit_stage_==EDIT_WRITE && wire_command_==settings_wire_) {
      settings_uncertain_=true; edit_stage_=EDIT_VERIFY; settings_read_retries_=0;
      id(settings_result).publish_state("Write acknowledgement lost; checking actual configuration; write not retried");
      wire_command_="READ FUNCTION"; phase_=SETTINGS_RETRY_WAIT; deadline_=millis()+500;
      return;
    }
    if(wire_command_=="READ FUNCTION" && reply_mismatch) {
      if(settings_read_retries_<2 && !urgent_stop_) {
        ++settings_read_retries_;
        ESP_LOGD("gate_settings", "Configuration reply interrupted; read-only retry %u/2", settings_read_retries_);
        if(settings_write_sent_)
          id(settings_result).publish_state("Readback interrupted; retrying read only; write not retried");
        else if(edit_stage_!=EDIT_IDLE || settings_read_retries_>=2)
          id(settings_result).publish_state("Waiting for configuration; retrying read only");
        // Different delays avoid repeatedly colliding with a periodic cloud
        // client. Three sends maximum, five GATT reads per send.
        phase_=SETTINGS_RETRY_WAIT;
        deadline_=millis()+400*settings_read_retries_+millis()%211;
        return;
      }
      const bool submitted=settings_write_sent_;
      cancel_settings(submitted ? "Settings outcome uncertain; refresh to verify; write not retried" : "Settings read did not match; no configuration write sent");
      id(settings_result).publish_state(submitted ? "Settings outcome uncertain; refresh to verify; write not retried" : "Settings read did not match; refresh settings to try again");
      settings_due_=false; next_settings_refresh_=millis()+30000;
      wire_command_.clear(); busy_=false; phase_=IDLE_CONNECTED;
      if(urgent_stop_ || !user_command_.empty()) { busy_=true; phase_=COMMAND_WAIT; deadline_=millis()+50; }
      return;
    }
    cancel_settings(settings_write_sent_ ? "Settings outcome uncertain; refresh settings; not retried" : "Settings change failed before write; not retried");
    if (message.find("Authentication was not confirmed") != std::string::npos ||
        message.find("result 14") != std::string::npos || message.find("result 19") != std::string::npos)
      auth_blocked_ = true;  // No unattended repeated PIN attempts after rejection.
    // An unrelated UART response on a successful GATT read is not link loss.
    // Cloud/app traffic can overwrite the shared UART result characteristic.
    if ((background_poll_ || user_command_.empty()) && wire_command_ == "READ STATUS" && authenticated_ &&
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
    // A rejected early connect may never emit CLOSE_EVT. Release only our
    // session here; the SDK still guards its own connecting/disconnecting link.
    authenticated_=false; id(control_session)=false;
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
    if (urgent_stop_) { cancel_settings("Interrupted by Stop; refresh settings"); wire_command_ = "STOP"; urgent_stop_ = false; user_command_.clear(); }
    else if(edit_stage_==EDIT_WRITE) {
      if(!feedback_.confirmed_closed() || settings_wire_.empty()) {
        cancel_settings("Not sent: gate no longer confirmed closed"); busy_=false; phase_=IDLE_CONNECTED; return;
      }
      wire_command_=settings_wire_;
    } else if(edit_stage_==EDIT_VERIFY) wire_command_="READ FUNCTION";
    else if (baseline_needed_ && user_command_ != "READ STATUS") {
      wire_command_ = "READ STATUS"; baseline_needed_ = false;
    } else { wire_command_ = user_command_; user_command_.clear(); }
    if (!allowed(wire_command_) && !(edit_stage_==EDIT_WRITE && wire_command_==settings_wire_)) { fail("Invalid command; nothing sent"); return; }
    if ((wire_command_ == "PED OPEN" || wire_command_ == "FULL OPEN") &&
        id(require_gate_unlock) && id(gate_lock_state).state != "unlocked") {
      id(control_result).publish_state("Not sent: gate lock changed while command was queued");
      wire_command_.clear(); busy_=false; phase_=IDLE_CONNECTED; return;
    }
    write_command();
  }
  void write_command() {
    if((wire_command_=="READ FUNCTION" || wire_command_.rfind("WRITE FUNCTION,",0)==0) && uint32_t(millis()-last_uart_reply_)<250) {
      phase_=QUIET_WAIT; deadline_=last_uart_reply_+250; return;
    }
    if(wire_command_.rfind("WRITE FUNCTION",0)==0) {
      if(edit_stage_!=EDIT_WRITE || settings_write_sent_ || wire_command_!=settings_wire_ ||
         !feedback_.confirmed_closed()) {
        fail("Settings write rejected by transaction guard"); return;
      }
      settings_write_sent_=true;
      id(settings_result).publish_state("Saving setting; waiting for acknowledgement");
    }
    phase_ = COMMAND_WRITE; deadline_ = millis() + 4000; write_confirmed_ = false;
    status("Sending: " + wire_command_);
    if (wire_command_ == "READ STATUS") { status_due_=false; id(poll_health).publish_state("Requesting status"); }
    else id(control_result).publish_state("Sending: " + wire_command_);
    if (write(command_handle_, "C=" + wire_command_ + ";src=P9999999") != ESP_OK)
      fail("Could not submit command; no retry scheduled");
  }
};
