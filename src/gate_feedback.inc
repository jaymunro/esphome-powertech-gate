// Pure feedback model: C004 has no timestamp, so it never establishes state.
class GateFeedback {
 public:
  struct Profile {
    std::string model{"PS19001"}, pedestrian_status{"SINGLE STOPPED"};
    int full_open_min{95}, closed_position{0};
    bool supports_pedestrian{true};
    int closed_position_tolerance{0};
    bool opening_event_mode_hint{false};
    bool single_closed_after_tracked_closing{false};
  };
  GateFeedback() = default;
  explicit GateFeedback(Profile profile) : profile_(profile) {}
  void set_profile(Profile profile) { profile_ = profile; disconnected(); }
  struct Sample { std::string text; int position{-1}; };
  void connected(uint32_t now) {
    clear_event_hint(); baseline_armed_=false; last_phase_.clear(); sample_phase_.clear();
    linked_ = true; connected_at_ = last_event_ = now; synced_ = false;
    invalidate(); mode_.clear(); position_=-1;
  }
  void disconnected() { linked_ = false; synced_ = false; invalidate(); mode_.clear(); last_phase_.clear(); hint_pending_=false; position_=-1; baseline_armed_=false; clear_event_hint(); }
  void event(uint32_t now, const std::string &payload="") {
    const auto label=payload.substr(0,payload.find(';'));
    if(label.empty() || label=="Opening" || label=="PedOpening" || label=="Opened" ||
       label=="PedOpened" || label=="Stopped") clear_closing_track();
    const std::string event_mode=label=="Opening" ? "car" :
        (label=="PedOpening" && profile_.supports_pedestrian ? "pedestrian" : "");
    if(profile_.opening_event_mode_hint && !event_mode.empty()) {
      if((!event_mode_.empty() && event_mode_!=event_mode) ||
         (hint_pending_ && uint32_t(now-hint_at_)<8000 && hint_mode_!=event_mode)) {
        event_conflict_=true; event_mode_.clear(); hint_pending_=false;
      } else if(!event_conflict_ && event_mode_.empty() && linked_ && synced_ && valid_ &&
                baseline_armed_ && physical_=="closed" && uint32_t(now-last_status_)<=25000 &&
                (sample_phase_=="CLOSED" || sample_phase_=="OPENING")) {
        event_mode_=event_mode; event_at_=now;
      }
    }
    // Preserve only a pending candidate that the event agrees with. An event
    // cannot establish state, renew its age, or release the closed-write guard.
    // A mode event can arrive after the first solicited OPENING sample. Carry
    // that recent candidate forward, but publish nothing until another fresh,
    // progressing read confirms it. Conflicting/old events cannot reuse it.
    bool opening_candidate=false;
    if(pending_ && !event_mode.empty() && !event_conflict_ && event_mode_==event_mode &&
       sample_phase_=="OPENING" && uint32_t(now-last_sample_)<=1500 &&
       uint32_t(now-candidate_at_)<=1500 &&
       (candidate_=="open:" || candidate_=="open:"+event_mode)) {
      candidate_="open:"+event_mode; opening_candidate=true;
    }
    const bool compatible=pending_ &&
        (opening_candidate || (label=="Opened" && candidate_=="open:car" && (sample_phase_=="OPENED" || sample_phase_=="OPEN")) ||
         (label=="PedOpened" && candidate_=="open:pedestrian" && sample_phase_=="STOPPED") ||
         ((label=="Closed" || label=="PedClosed") && candidate_=="closed:closed" && sample_phase_=="CLOSED") ||
         (label=="Closing" && (sample_phase_=="CLOSING" || sample_phase_=="CLOSEING")));
    last_event_ = now; pending_=true; if(!compatible) candidate_.clear();
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
    sample_phase_=word;
    const bool closed = word == "CLOSED" && std::abs(s.position - profile_.closed_position) <= profile_.closed_position_tolerance;
    // A contradictory CLOSED position must never become fresh Open feedback.
    if (word == "CLOSED" && !closed) { invalidate(); return false; }
    const bool closing=word=="CLOSING" || word=="CLOSEING";
    const bool tracked_closed=profile_.single_closed_after_tracked_closing && closed && linked_ && synced_ && valid_ && physical_=="open" &&
        closing_progress_ && uint32_t(now-closing_last_at_)<=5000 &&
        std::abs(closing_last_position_-profile_.closed_position)<=profile_.closed_position_tolerance &&
        uint32_t(now-last_status_)<=25000;
    if(closing) {
      if(!closing_seen_ || s.position>closing_last_position_ || uint32_t(now-closing_last_at_)>5000) {
        closing_seen_=true; closing_progress_=false;
        closing_first_at_=now; closing_first_position_=s.position;
      }
      closing_last_at_=now; closing_last_position_=s.position;
      if(uint32_t(now-closing_first_at_)>=500 && s.position<closing_first_position_) closing_progress_=true;
    } else clear_closing_track();
    std::string next_mode = closed ? "closed" : mode_;
    bool event_hint=false;
    if(closed) clear_event_hint();  // Replayed idle events cannot survive a fresh CLOSED read.
    if (word == "OPENING") {
      if (last_phase_ != "OPENING") next_mode.clear();
      if (hint_pending_ && uint32_t(now-hint_at_) < 8000) next_mode=hint_mode_;
      else if(!event_conflict_ && !event_mode_.empty() && uint32_t(now-event_at_)<8000) {
        next_mode=event_mode_; event_hint=true;
      }
    }
    if (!closed && profile_.supports_pedestrian && s.text == profile_.pedestrian_status && s.position > profile_.closed_position) next_mode = "pedestrian";
    else if (!closed && (word == "OPENED" || word == "OPEN") && s.position >= profile_.full_open_min) next_mode = "car";
    // A previous closed mode says nothing about the next opening.
    if (!closed && next_mode == "closed") next_mode.clear();
    const std::string key = std::string(closed ? "closed" : "open") + ":" + next_mode;
    if (candidate_ != key) {
      candidate_ = key; candidate_at_ = now; candidate_position_=s.position; pending_=true;
      // Confirm a new candidate without publishing an intermediate Unknown.
      // Freshness still expires from the last confirmed status after 25s.
      if(!tracked_closed) return false;
    }
    // Unlike an explicit local command, an event is untimestamped. Demand
    // progressing physical OPENING reads before accepting its access-mode hint.
    if(event_hint && s.position<=candidate_position_) {
      if(s.position<candidate_position_) { event_mode_.clear(); event_conflict_=true; candidate_.clear(); }
      return false;
    }
    if (!tracked_closed && uint32_t(now-candidate_at_) < (closed ? 1000u : 500u)) return false;
    valid_ = synced_ = true; pending_=false; last_status_ = now; mode_ = next_mode;
    last_phase_=word;
    if (!closed && !next_mode.empty()) hint_pending_=false;
    physical_ = closed ? "closed" : "open";
    baseline_armed_=closed;
    if(!closed && !next_mode.empty()) event_mode_.clear();
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
  std::string event_mode_,sample_phase_;
  uint32_t event_at_{0}; int candidate_position_{-1};
  bool baseline_armed_{false},event_conflict_{false};
  bool closing_seen_{false},closing_progress_{false};
  uint32_t closing_first_at_{0},closing_last_at_{0};
  int closing_first_position_{-1},closing_last_position_{-1};
  void clear_closing_track() { closing_seen_=closing_progress_=false; }
  void clear_event_hint() { event_mode_.clear(); event_conflict_=false; }
  bool linked_{false},valid_{false},synced_{false},pending_{true};
  uint32_t connected_at_{0},last_event_{0},candidate_at_{0},last_status_{0};
  std::string candidate_,mode_,physical_{"unavailable"},car_{"unavailable"},pedestrian_{"unavailable"};
  void invalidate() { valid_=false; pending_=true; candidate_.clear(); clear_closing_track(); physical_=car_=pedestrian_="unavailable"; }
};
