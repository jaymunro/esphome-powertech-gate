// Pure feedback model: C004 has no timestamp, so it never establishes state.
class GateFeedback {
 public:
  struct Profile {
    std::string model{"PS19001"}, pedestrian_status{"SINGLE STOPPED"};
    int full_open_min{95}, closed_position{0};
    bool supports_pedestrian{true};
    int closed_position_tolerance{0};
    std::string car_opening_status{}, pedestrian_opening_status{};
  };
  GateFeedback() = default;
  explicit GateFeedback(Profile profile) : profile_(profile) {}
  void set_profile(Profile profile) { profile_ = profile; disconnected(); }
  struct Sample { std::string text; int position{-1}; };
  void connected(uint32_t now) {
    linked_ = true; connected_at_ = last_event_ = now; synced_ = false;
    invalidate(); mode_.clear(); position_=-1;
  }
  void disconnected() { linked_ = false; synced_ = false; invalidate(); mode_.clear(); last_phase_.clear(); hint_pending_=false; position_=-1; }
  void event(uint32_t now) {
    last_event_ = now; pending_=true; candidate_.clear();
    // Untimestamped notifications only request confirmation. Preserve a fresh
    // confirmed state; they neither establish movement nor refresh its age.
    if(!valid_) invalidate();
  }
  void commanded_open(bool pedestrian, uint32_t now) {
    hint_mode_ = pedestrian ? "pedestrian" : "car"; hint_at_=now; hint_pending_=true;
    event(now);  // A command never establishes movement or an open state.
  }
  void clear_command_hint() { hint_pending_=false; }
  void tick(uint32_t now) {
    if (position_ >= 0 && uint32_t(now-last_sample_) > 25000) position_=-1;
    if (valid_ && uint32_t(now - last_status_) > 25000) { invalidate(); mode_.clear(); }
  }
  bool accept(const Sample &s, uint32_t now) {
    if (!linked_) return false;
    position_=s.position; last_sample_=now;
    if (uint32_t(now-connected_at_) < 15000 ||
        (!synced_ && uint32_t(now-last_event_) < 3000)) return false;
    const auto word = s.text.substr(s.text.find_last_of(' ')+1);
    const bool closed = word == "CLOSED" && std::abs(s.position - profile_.closed_position) <= profile_.closed_position_tolerance;
    // A contradictory CLOSED position must never become fresh Open feedback.
    if (word == "CLOSED" && !closed) { invalidate(); return false; }
    std::string next_mode = closed ? "closed" : mode_;
    if (word == "OPENING") {
      if (last_phase_ != "OPENING") next_mode.clear();
      if (hint_pending_ && uint32_t(now-hint_at_) < 8000) next_mode=hint_mode_;
    }
    // Only model-validated explicit opening signatures override a local hint.
    if (word == "OPENING" && !profile_.car_opening_status.empty() && s.text == profile_.car_opening_status) next_mode="car";
    else if (word == "OPENING" && profile_.supports_pedestrian && !profile_.pedestrian_opening_status.empty() && s.text == profile_.pedestrian_opening_status) next_mode="pedestrian";
    if (!closed && profile_.supports_pedestrian && s.text == profile_.pedestrian_status && s.position > profile_.closed_position) next_mode = "pedestrian";
    else if (!closed && (word == "OPENED" || word == "OPEN") && s.position >= profile_.full_open_min) next_mode = "car";
    // A previous closed mode says nothing about the next opening.
    if (!closed && next_mode == "closed") next_mode.clear();
    const std::string key = std::string(closed ? "closed" : "open") + ":" + next_mode;
    if (candidate_ != key) {
      candidate_ = key; candidate_at_ = now; pending_=true;
      // Confirm a new candidate without publishing an intermediate Unknown.
      // Freshness still expires from the last confirmed status after 25s.
      return false;
    }
    if (uint32_t(now-candidate_at_) < (closed ? 1000u : 500u)) return false;
    valid_ = synced_ = true; pending_=false; last_status_ = now; mode_ = next_mode;
    last_phase_=word;
    if (!closed && !next_mode.empty()) hint_pending_=false;
    physical_ = closed ? "closed" : "open";
    car_ = pedestrian_ = "unavailable";
    if (closed) car_ = pedestrian_ = "closed";
    else if (mode_ == "car" || mode_ == "pedestrian") {
      const std::string phase = word == "OPENING" ? "opening" :
          ((word == "CLOSING" || word == "CLOSEING") ? "closing" : "open");
      // Covers are mutually exclusive access modes, not independent leaf positions.
      car_ = mode_ == "car" ? phase : "closed";
      pedestrian_ = mode_ == "pedestrian" ? phase : "closed";
    }
    return true;
  }
  int position() const { return position_; }
  bool available() const { return valid_; }
  bool confirmed_closed() const { return valid_ && !pending_ && physical_=="closed"; }
  const std::string &gate() const { return physical_; }
  const std::string &car() const { return car_; }
  const std::string &pedestrian() const { return pedestrian_; }
  static bool parse_reply(const std::string &reply, Sample &out) {
    bool found = false;
    size_t start = 0;
    while (start < reply.size()) {
      auto end = reply.find_first_of("\r\n", start);
      auto line = reply.substr(start, end == std::string::npos ? end : end-start);
      if (line.rfind("C=",0)==0) line.erase(0,2);
      size_t offset = line.rfind("ACK STATUS:",0)==0 ? 11 :
          (line.rfind("ACK READ STATUS:",0)==0 ? 16 : 0);
      if (offset) {
        auto payload=line.substr(offset); payload=payload.substr(0,payload.find(';'));
        const auto comma=payload.find(',');
        if (comma==std::string::npos || payload.find(',',comma+1)!=std::string::npos) return false;
        const auto text=payload.substr(0,comma), number=payload.substr(comma+1);
        if (number.empty() || number.size()>3 || !std::all_of(number.begin(),number.end(),
            [](char c){return c>='0' && c<='9';})) return false;
        const int pos=atoi(number.c_str()); if(pos>100) return false;
        const auto space=text.find_last_of(' ');
        const auto prefix=space==std::string::npos ? "" : text.substr(0,space);
        const auto word=space==std::string::npos ? text : text.substr(space+1);
        if (!(prefix.empty() || prefix=="PED" || prefix=="SINGLE" || prefix=="FULL") ||
            !(word=="OPEN" || word=="OPENED" || word=="OPENING" || word=="CLOSED" ||
              word=="CLOSING" || word=="CLOSEING" || word=="STOPPED")) return false;
        if (found && (out.text!=text || out.position!=pos)) {
          // The module can concatenate successive replies. Accept only the same
          // movement phase/prefix with positions progressing in its direction.
          const bool opening=word=="OPENING" && pos>=out.position;
          const bool closing=(word=="CLOSING" || word=="CLOSEING") && pos<=out.position;
          if (out.text!=text || !(opening || closing)) return false;
        }
        out={text,pos}; found=true;
      }
      if(end==std::string::npos) break;
      start=end+1;
    }
    return found;
  }
 private:
  Profile profile_;
  int position_{-1}; uint32_t last_sample_{0};
  std::string hint_mode_,last_phase_;
  uint32_t hint_at_{0}; bool hint_pending_{false};
  bool linked_{false},valid_{false},synced_{false},pending_{true};
  uint32_t connected_at_{0},last_event_{0},candidate_at_{0},last_status_{0};
  std::string candidate_,mode_,physical_{"unavailable"},car_{"unavailable"},pedestrian_{"unavailable"};
  void invalidate() { valid_=false; pending_=true; candidate_.clear(); physical_=car_=pedestrian_="unavailable"; }
};
