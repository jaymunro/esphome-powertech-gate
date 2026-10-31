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
    for(const auto &field:fields()) {
      const Record *record=nullptr;
      for(const auto &r:records_) if(r.label==field.label) record=&r;
      if(!record || record->raw.size()!=(field.decimal?2u:1u)) return false;
      if(field.decimal && !std::all_of(record->raw.begin(),record->raw.end(),[](char c){return c>='0'&&c<='9';})) return false;
      const int index=int(strtoul(record->raw.c_str(),nullptr,field.decimal?10:16))-field.offset;
      if(index<0 || size_t(index)>=field.options.size()) return false;
      if(field.label=='1' && index==2) return false; // Hall current schema is different.
      values_.push_back(index);
    }
    valid_=true; return true;
  }
  bool valid() const { return valid_; }
  int auto_close_index() const { return valid_ ? values_[8] : -1; }
  std::string value(size_t index) const { return valid_ ? fields()[index].options[values_[index]] : "Unknown"; }
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
  std::string auto_close_command(int index) const {
    // Preserve every record exactly except 9. Unknown metadata may be an
    // extension: refuse a write rather than silently discard it.
    if(!valid_ || index<0 || index>8 || (!suffix_.empty() && suffix_!=";src=P9999999")) return "";
    std::string out="WRITE FUNCTION";
    for(const auto &r:records_) out+=","+std::string(1,r.label)+":"+(r.label=='9'?std::string(1,char('0'+index)):r.raw);
    return out.size()<=400 ? out : "";
  }
  bool matches_auto_close_change(const GateSettings &before,int desired) const {
    if(!valid_ || !before.valid_ || auto_close_index()!=desired || records_.size()!=before.records_.size() || suffix_!=before.suffix_) return false;
    for(size_t i=0;i<records_.size();++i)
      if(records_[i].label!=before.records_[i].label || (records_[i].label!='9' && records_[i].raw!=before.records_[i].raw)) return false;
    return true;
  }
 private:
  bool valid_{false};
  std::vector<Record> records_;
  std::vector<int> values_;
  std::string suffix_;
};
