// Generated field options come from settings/ps19001.json.
class GateSettings {
 public:
  struct Field { char label; int offset; bool decimal; std::vector<std::string> options; };
  struct Record { char label; std::string raw; };
  static const std::vector<Field> &fields() { static const std::vector<Field> definitions = {
    {'1', 1, false, {"Overcurrent", "Limit Switch", "Hall Sensor"}},
    {'2', 1, true, {"2 A", "3 A", "4 A", "5 A"}},
    {'3', 1, true, {"2 A", "3 A", "4 A", "5 A"}},
    {'4', 1, false, {"40 %", "50 %", "75 %", "100 %"}},
    {'5', 1, false, {"40 %", "50 %", "75 %", "100 %"}},
    {'6', 1, false, {"40 %", "50 %", "60 %", "70 %"}},
    {'7', 0, false, {"0 sec", "2 sec", "5 sec", "10 sec", "15 sec", "20 sec", "25 sec", "35 sec", "45 sec", "55 sec"}},
    {'8', 0, false, {"0 sec", "2 sec", "5 sec", "10 sec", "15 sec", "20 sec", "25 sec", "35 sec", "45 sec", "55 sec"}},
    {'9', 0, false, {"Function OFF", "3 sec", "10 sec", "20 sec", "40 sec", "60 sec", "120 sec", "180 sec", "300 sec"}},
    {'A', 1, false, {"Mode 1", "Mode 2", "Mode 3", "Mode 4"}},
    {'B', 0, false, {"Function OFF", "Function ON"}},
    {'C', 0, false, {"Blinks whenstarts moving", "Blinks 3 seconds before starts moving"}},
    {'D', 0, false, {"Function OFF", "Function ON"}},
    {'E', 0, false, {"Function OFF", "Function ON"}},
    {'F', 0, false, {"Function OFF", "Function ON"}},
    {'G', 0, false, {"Standard Gate Opening", "Release Gate Tension before Opening (Gate Reversing for 0.25s)"}},
    {'H', 0, false, {"When Terminal Block is at Top", "When Terminal Block is at Bottom"}},
    {'I', 1, false, {"Single Gate", "Dual Gate"}},
    {'J', 0, false, {"Function OFF", "0.1 sec", "0.2 sec", "0.3 sec", "0.4 sec", "0.5 sec", "0.6 sec"}},
  }; return definitions; }
  static const Field &hall_current() {
    static const Field field{'2', 5, true, {"0.5 A", "0.6 A", "0.7 A", "0.8 A", "0.9 A", "1 A", "1.1 A", "1.2 A", "1.3 A", "1.4 A", "1.5 A", "1.6 A", "1.7 A", "1.8 A", "1.9 A", "2 A", "2.1 A", "2.2 A", "2.3 A", "2.4 A", "2.5 A", "2.6 A", "2.7 A", "2.8 A", "2.9 A", "3 A", "3.1 A", "3.2 A", "3.3 A", "3.4 A", "3.5 A", "3.6 A", "3.7 A", "3.8 A", "3.9 A", "4 A"}}; return field;
  }
  static const Field *definition(char label, bool hall=false) {
    if(hall && (label=='2' || label=='3')) return &hall_current();
    for(const auto &f:fields()) if(f.label==label) return &f;
    return nullptr;
  }
  static bool option_valid(char label, const std::string &option) {
    const auto *f=definition(label);
    if(f && std::find(f->options.begin(),f->options.end(),option)!=f->options.end()) return true;
    return (label=='2' || label=='3') && std::find(hall_current().options.begin(),hall_current().options.end(),option)!=hall_current().options.end();
  }
  bool decode(const std::string &input) {
    valid_=false; records_.clear(); values_.clear(); suffix_.clear();
    std::string raw=input.rfind("C=",0)==0 ? input.substr(2) : input;
    if(raw.size()<19 || raw.size()>2048 || raw.substr(0,17)!="ACK READ FUNCTION" ||
       (raw[17]!=',' && raw[17]!=':') || !std::all_of(raw.begin(),raw.end(),[](unsigned char c){return c>=32 && c<=126;})) return false;
    const auto semicolon=raw.find(';');
    if(semicolon!=std::string::npos) suffix_=raw.substr(semicolon);
    const std::string body=raw.substr(18,semicolon==std::string::npos ? semicolon : semicolon-18);
    size_t start=0;
    while(start<body.size()) {
      const auto end=body.find(',',start);
      const auto token=body.substr(start,end==std::string::npos ? end : end-start);
      if(token.size()<3 || token.size()>10 || token[1]!=':' ||
         (!(token[0]>='0' && token[0]<='9') && !(token[0]>='A' && token[0]<='Z'))) return false;
      const auto value=token.substr(2);
      if(!std::all_of(value.begin(),value.end(),[](char c){return (c>='0'&&c<='9')||(c>='A'&&c<='F')||(c>='a'&&c<='f');})) return false;
      for(const auto &record:records_) if(record.label==token[0]) return false;
      records_.push_back({token[0],value});
      if(records_.size()>36) return false;
      if(end==std::string::npos) break;
      start=end+1; if(start==body.size()) return false;
    }
    const bool hall=std::any_of(records_.begin(),records_.end(),[](const Record &r){return r.label=='1' && r.raw=="3";});
    for(const auto &base:fields()) {
      const auto &field=*definition(base.label,hall);
      const Record *record=nullptr;
      for(const auto &r:records_) if(r.label==base.label) record=&r;
      if(!record || record->raw.size()!=(field.decimal?2u:1u)) return false;
      if(field.decimal && !std::all_of(record->raw.begin(),record->raw.end(),[](char c){return c>='0'&&c<='9';})) return false;
      const int index=int(strtoul(record->raw.c_str(),nullptr,field.decimal?10:16))-field.offset;
      if(index<0 || size_t(index)>=field.options.size()) return false;
      values_.push_back(index);
    }
    valid_=true; return true;
  }
  bool valid() const { return valid_; }
  int auto_close_index() const { return valid_ ? values_[8] : -1; }
  bool hall() const { return valid_ && values_[0]==2; }
  std::string value(size_t index) const { return valid_ && index<fields().size() ? definition(fields()[index].label,hall())->options[values_[index]] : "Unknown"; }
  std::string value_for(char label) const {
    for(size_t i=0;i<fields().size();++i) if(fields()[i].label==label) return value(i);
    return "Unknown";
  }
  void publish() const {
    if(!valid_) return;
    id(setting_1).publish_state(value(0));
    id(setting_2).publish_state(value(1));
    id(setting_3).publish_state(value(2));
    id(setting_4).publish_state(value(3));
    id(setting_5).publish_state(value(4));
    id(setting_6).publish_state(value(5));
    id(setting_7).publish_state(value(6));
    id(setting_8).publish_state(value(7));
    id(setting_9).publish_state(value(8));
    id(setting_a).publish_state(value(9));
    id(setting_b).publish_state(value(10));
    id(setting_c).publish_state(value(11));
    id(setting_d).publish_state(value(12));
    id(setting_e).publish_state(value(13));
    id(setting_f).publish_state(value(14));
    id(setting_g).publish_state(value(15));
    id(setting_h).publish_state(value(16));
    id(setting_i).publish_state(value(17));
    id(setting_j).publish_state(value(18));
  }
  std::string change_command(char label,const std::string &option) const {
    if(!valid_ || !option_valid(label,option) || (!suffix_.empty() && suffix_!=";src=P9999999")) return "";
    const bool target_hall=label=='1' ? option=="Hall Sensor" : hall();
    std::string out="WRITE FUNCTION";
    for(const auto &r:records_) {
      std::string raw=r.raw;
      if(r.label==label || (label=='1' && target_hall!=hall() && (r.label=='2' || r.label=='3'))) {
        const auto *f=definition(r.label,target_hall);
        const std::string desired=r.label==label ? option : value_for(r.label);
        const auto pos=std::find(f->options.begin(),f->options.end(),desired);
        // Motor-type changes preserve the physical current thresholds; never
        // silently substitute the app's defaults or an arbitrary nearest value.
        if(pos==f->options.end()) return "";
        const int wire=int(pos-f->options.begin())+f->offset;
        char encoded[12]; snprintf(encoded,sizeof(encoded),f->decimal?"%02d":"%X",wire); raw=encoded;
      }
      out+=","+std::string(1,r.label)+":"+raw;
    }
    return out.size()<=400 ? out : "";
  }
  bool same_metadata(const GateSettings &other) const { return suffix_==other.suffix_; }
  bool matches_command(const std::string &command) const {
    if(!valid_ || command.rfind("WRITE FUNCTION,",0)!=0) return false;
    std::string body="WRITE FUNCTION";
    for(const auto &r:records_) body+=","+std::string(1,r.label)+":"+r.raw;
    return body==command;
  }
  // Compatibility for the auto-close transaction fixtures.
  std::string auto_close_command(int index) const {
    return index>=0 && index<int(fields()[8].options.size()) ? change_command('9',fields()[8].options[index]) : "";
  }
  bool matches_auto_close_change(const GateSettings &before,int desired) const {
    return suffix_==before.suffix_ && matches_command(before.auto_close_command(desired));
  }
 private:
  bool valid_{false};
  std::vector<Record> records_;
  std::vector<int> values_;
  std::string suffix_;
};
