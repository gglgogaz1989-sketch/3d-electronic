#include "electric3d/core.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <filesystem>
#include <iomanip>
#include <map>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace e3d {
static std::vector<Kind> buildCatalog() {
    auto two=[](){return std::vector<Pin>{{"A",-.78,0},{"B",.78,0}};};
    return {
        {"battery","Battery (A- B+)",two(),9},
        {"resistor","Resistor",two(),220},
        {"led","LED (A anode)",two(),2},
        {"bulb","Light bulb",two(),90},
        {"switch","Switch",two(),0},
        {"capacitor","Capacitor",two(),100},
        {"diode","Diode",two(),.7},
        {"motor","Motor",two(),90},
        {"buzzer","Buzzer",two(),120},
        {"fuse","Fuse",two(),1},
        {"ammeter","Ammeter",two(),0},
        {"voltmeter","Voltmeter",two(),0},
        {"npn","NPN (B,C,E)",{{"B",-.78,-.6},{"C",.78,-.6},{"E",0,.8}},0},
        {"pnp","PNP (B,C,E)",{{"B",-.78,-.6},{"C",.78,-.6},{"E",0,.8}},0},
        {"nmos","N-MOSFET (G,D,S)",{{"G",-.78,-.6},{"D",.78,-.6},{"S",0,.8}},0},
        {"button","Push button",two(),0},
        {"pot","Potentiometer",{{"A",-.78,-.55},{"W",0,.72},{"B",.78,-.55}},1000},
        {"ldr","Photoresistor",two(),200},
        {"relay","Relay",{{"CP",-.8,-.65},{"CN",-.8,.65},{"COM",.8,-.65},{"NO",.8,.65}},0},
        {"header","Board connector",two(),0},
        {"controller","C++ controller",{{"GND",-1,-.8},{"VCC",-1,.8},{"D0",.9,-1.2},{"D1",.9,-.4},{"D2",.9,.4},{"D3",.9,1.2}},0},
    };
}
const std::vector<Kind>& catalog(){static auto c=buildCatalog();return c;}
const Kind* kindOf(const std::string& k) { for (const auto& item:catalog()) if(item.key==k)return &item; return nullptr; }
static void checkCoords(const Project& p,double x,double y) {
    if(!std::isfinite(x)||!std::isfinite(y)||std::abs(x)>p.board.width*.5||std::abs(y)>p.board.height*.5) throw std::runtime_error("Position outside board");
}
Part& Project::add(const std::string& kind,double x,double y) {
    if(!kindOf(kind)) throw std::runtime_error("Unknown part type");
    if(parts.size()>=90)throw std::runtime_error("Maximum 90 parts");
    checkCoords(*this,x,y);
    Part p; p.id="p"+std::to_string(nextId++);p.kind=kind;p.x=x;p.y=y;p.value=kindOf(kind)->defaultValue;
    parts.push_back(p);return parts.back();
}
Part* Project::find(const std::string& id){for(auto& p:parts)if(p.id==id)return &p;return nullptr;}
const Part* Project::find(const std::string& id) const{for(const auto& p:parts)if(p.id==id)return &p;return nullptr;}
static bool hasPin(const Part& p,const std::string& pin){for(const auto& x:kindOf(p.kind)->pins)if(x.key==pin)return true;return false;}
bool Project::connect(const std::string& a,const std::string& pa,const std::string& b,const std::string& pb){
    const Part* x=find(a),*y=find(b);
    if(!x||!y||!hasPin(*x,pa)||!hasPin(*y,pb)||a==b||wires.size()>=300)return false;
    for(const auto& w:wires){ if((w.startId==a&&w.startPort==pa&&w.endId==b&&w.endPort==pb)||(w.startId==b&&w.startPort==pb&&w.endId==a&&w.endPort==pa))return false; }
    wires.push_back({a,pa,b,pb});return true;
}
void Project::removePart(const std::string& id){parts.erase(std::remove_if(parts.begin(),parts.end(),[&](const Part& p){return p.id==id;}),parts.end());wires.erase(std::remove_if(wires.begin(),wires.end(),[&](const Wire& w){return w.startId==id||w.endId==id;}),wires.end());}
bool Project::removeWire(int i){if(i<0||i>=int(wires.size()))return false;wires.erase(wires.begin()+i);return true;}

// A small, self-contained, depth- and size-limited JSON reader so that neither
// the Windows application nor the GitHub workflow needs Python or a JSON DLL.
struct J {
    enum Type {Null, Bool, Num, Str, Arr, Obj} type=Null;
    double number=0; bool boolean=false;std::string str;
    std::vector<J> array;std::map<std::string,J> object;
    const J& get(const std::string& key)const{static const J none;auto it=object.find(key);return it==object.end()?none:it->second;}
    std::string s(const std::string& fallback="")const{return type==Str?str:fallback;}
    double n(double fallback=0)const{return type==Num?number:fallback;}
    bool b(bool fallback=false)const{return type==Bool?boolean:fallback;}
};
struct Parser {
    const std::string& s;size_t i=0;
    explicit Parser(const std::string& str):s(str){}
    void ws(){while(i<s.size()&&(s[i]==' '||s[i]=='\n'||s[i]=='\r'||s[i]=='\t'))++i;}
    bool eat(char c){ws();if(i<s.size()&&s[i]==c){++i;return true;}return false;}
    [[noreturn]] void bad(){throw std::runtime_error("Invalid .elctrd JSON");}
    unsigned hex4(){unsigned n=0;for(int k=0;k<4;k++){if(i>=s.size())bad();char c=s[i++];n*=16;if(c>='0'&&c<='9')n+=c-'0';else if(c>='a'&&c<='f')n+=c-'a'+10;else if(c>='A'&&c<='F')n+=c-'A'+10;else bad();}return n;}
    void utf8(std::string& out,unsigned c){if(c<=0x7f)out+=char(c);else if(c<=0x7ff){out+=char(0xc0|(c>>6));out+=char(0x80|(c&63));}else if(c<=0xffff){out+=char(0xe0|(c>>12));out+=char(0x80|((c>>6)&63));out+=char(0x80|(c&63));}else{out+=char(0xf0|(c>>18));out+=char(0x80|((c>>12)&63));out+=char(0x80|((c>>6)&63));out+=char(0x80|(c&63));}}
    std::string string(){if(!eat('"'))bad();std::string o;while(i<s.size()){unsigned char c=(unsigned char)s[i++];if(c=='"')return o;if(c<0x20)bad();if(c!='\\'){o+=char(c);continue;}if(i>=s.size())bad();char k=s[i++];switch(k){case '"':o+='"';break;case '\\':o+='\\';break;case '/':o+='/';break;case 'n':o+='\n';break;case 'r':o+='\r';break;case 't':o+='\t';break;case 'b':o+='\b';break;case 'f':o+='\f';break;case 'u':{unsigned u=hex4();if(u>=0xd800&&u<=0xdbff){if(i+2>s.size()||s[i++]!='\\'||s[i++]!='u')bad();unsigned l=hex4();if(l<0xdc00||l>0xdfff)bad();u=0x10000+((u-0xd800)<<10)+(l-0xdc00);}else if(u>=0xdc00&&u<=0xdfff)bad();utf8(o,u);break;}default:bad();}}bad();}
    J value(int depth=0){if(depth>24)bad();ws();if(i>=s.size())bad();J o;
        if(s[i]=='"'){o.type=J::Str;o.str=string();}
        else if(eat('{')){o.type=J::Obj;if(!eat('}')){do {auto key=string();if(!eat(':'))bad();auto val=value(depth+1);o.object[key]=std::move(val);}while(eat(','));if(!eat('}'))bad();}}
        else if(eat('[')){o.type=J::Arr;if(!eat(']')){do{o.array.push_back(value(depth+1));if(o.array.size()>5000)bad();}while(eat(','));if(!eat(']'))bad();}}
        else if(s.compare(i,4,"true")==0){i+=4;o.type=J::Bool;o.boolean=true;}
        else if(s.compare(i,5,"false")==0){i+=5;o.type=J::Bool;}
        else if(s.compare(i,4,"null")==0){i+=4;}
        else{size_t begin=i;if(s[i]=='-')++i;size_t digits=i;while(i<s.size()&&s[i]>='0'&&s[i]<='9')++i;if(i==digits)bad();if(i<s.size()&&s[i]=='.'){++i;digits=i;while(i<s.size()&&s[i]>='0'&&s[i]<='9')++i;if(i==digits)bad();}if(i<s.size()&&(s[i]=='e'||s[i]=='E')){++i;if(i<s.size()&&(s[i]=='+'||s[i]=='-'))++i;digits=i;while(i<s.size()&&s[i]>='0'&&s[i]<='9')++i;if(digits==i)bad();}try{o.number=std::stod(s.substr(begin,i-begin));}catch(...){bad();}if(!std::isfinite(o.number))bad();o.type=J::Num;}
        return o;
    }
};
static void quote(std::ostream& o,const std::string& str){o<<'"';for(unsigned char c:str){switch(c){case '"':o<<"\\\"";break;case '\\':o<<"\\\\";break;case '\n':o<<"\\n";break;case '\r':o<<"\\r";break;case '\t':o<<"\\t";break;default:if(c<0x20)o<<"\\u"<<std::hex<<std::setw(4)<<std::setfill('0')<<int(c)<<std::dec;else o<<c;}}o<<'"';}
std::string Project::toJson() const{
    std::ostringstream o;o<<std::setprecision(12);
    o<<"{\n  \"format\":\"electric3d\",\"version\":3,\n  \"name\":";quote(o,name);
    o<<",\n  \"board\":{\"width\":"<<board.width<<",\"height\":"<<board.height<<"},\n  \"controller_code\":";quote(o,controllerCode);
    o<<",\n  \"parts\":[\n";
    for(size_t i=0;i<parts.size();i++){const auto& p=parts[i];o<<"    {\"id\":";quote(o,p.id);o<<",\"kind\":";quote(o,p.kind);o<<",\"x\":"<<p.x<<",\"y\":"<<p.y<<",\"angle\":"<<p.angle<<",\"value\":"<<p.value<<",\"closed\":"<<(p.closed?"true":"false")<<",\"blown\":"<<(p.blown?"true":"false")<<"}"<<(i+1<parts.size()?",":"")<<"\n";}
    o<<"  ],\n  \"wires\":[\n";
    for(size_t i=0;i<wires.size();i++){const auto& w=wires[i];o<<"    {\"start_id\":";quote(o,w.startId);o<<",\"start_port\":";quote(o,w.startPort);o<<",\"end_id\":";quote(o,w.endId);o<<",\"end_port\":";quote(o,w.endPort);o<<"}"<<(i+1<wires.size()?",":"")<<"\n";}
    o<<"  ]\n}\n";return o.str();
}
Project Project::fromJson(const std::string& text){
    if(text.size()>5'000'000)throw std::runtime_error("Project too large");
    Parser parser(text);
    J root=parser.value();
    parser.ws();
    if(parser.i!=text.size()||root.type!=J::Obj||root.get("format").s()!="electric3d")
        throw std::runtime_error("Not an Electric3D project");
    int ver=int(root.get("version").n());if(ver<1||ver>3)throw std::runtime_error("Unsupported .elctrd version");
    Project p;p.name=root.get("name").s("Electric3D board");if(p.name.size()>250)throw std::runtime_error("Invalid name");
    if(ver==3){const J& b=root.get("board");p.board.width=int(b.get("width").n(20));p.board.height=int(b.get("height").n(16));if(p.board.width<6||p.board.width>60||p.board.height<6||p.board.height>60)throw std::runtime_error("Invalid board size");p.controllerCode=root.get("controller_code").s();if(p.controllerCode.size()>100000)throw std::runtime_error("Firmware too large");}
    if(p.controllerCode.empty())p.controllerCode=defaultFirmware();
    auto pa=root.get("parts"),wi=root.get("wires");if(pa.type!=J::Arr||wi.type!=J::Arr||pa.array.size()>90||wi.array.size()>300)throw std::runtime_error("Too many parts / wires");
    for(const auto& el:pa.array){if(el.type!=J::Obj)throw std::runtime_error("Invalid part");Part part;part.id=el.get("id").s();part.kind=el.get("kind").s();auto k=kindOf(part.kind);if(!k||part.id.empty()||part.id.size()>64||p.find(part.id))throw std::runtime_error("Invalid/duplicate part");part.x=el.get("x").n();part.y=el.get("y").n();part.value=el.get("value").n(k->defaultValue);part.closed=el.get("closed").b();part.blown=el.get("blown").b();part.angle=int(el.get("angle").n());if(part.angle!=0&&part.angle!=90&&part.angle!=180&&part.angle!=270)throw std::runtime_error("Invalid rotation");if(!std::isfinite(part.value)||std::abs(part.value)>1e9)throw std::runtime_error("Invalid part value");
        // Earlier format allowed a larger virtual surface, so enlarge v1/v2 boards.
        if(ver<3){p.board.width=60;p.board.height=60;}
        checkCoords(p,part.x,part.y);p.parts.push_back(part);
        if(part.id.size()>1&&part.id[0]=='p'){try{int n=std::stoi(part.id.substr(1));if(n>=p.nextId)p.nextId=n+1;}catch(...){}}
    }
    for(const auto& el:wi.array){if(el.type!=J::Obj||!p.connect(el.get("start_id").s(),el.get("start_port").s(),el.get("end_id").s(),el.get("end_port").s()))throw std::runtime_error("Invalid wire");}
    return p;
}
void Project::save(const std::string& path) const{std::ofstream f(std::filesystem::u8path(path),std::ios::binary|std::ios::trunc);if(!f)throw std::runtime_error("Cannot save .elctrd");f<<toJson();if(!f)throw std::runtime_error("Write failed");}
Project Project::load(const std::string& path){std::ifstream f(std::filesystem::u8path(path),std::ios::binary);if(!f)throw std::runtime_error("Cannot open .elctrd");std::string s((std::istreambuf_iterator<char>(f)),std::istreambuf_iterator<char>());return fromJson(s);}
std::string defaultFirmware(){return R"CPP(// C++ firmware for Electric3D. g++ (MinGW) or cl.exe is required to compile.
// input[0..3] read voltage on D0..D3; output[0..3] drive D0..D3.
// The board must power VCC and GND to activate the controller.
extern "C" __declspec(dllexport)
void controller_tick(const int* input, int* output, double seconds) {
    (void)input;
    output[0] = ((int)(seconds * 2.0) % 2) == 0; // blink D0
    output[1] = input[1];
    output[2] = 0;
    output[3] = 0;
}
)CPP";}
Project starter(){Project p;p.name="Controller and transistor";p.controllerCode=defaultFirmware();
    auto b=p.add("battery",-7,-4).id;auto c=p.add("controller",-3,-2).id;auto t=p.add("nmos",2,0).id;auto led=p.add("led",5,-3).id;auto res=p.add("resistor",2,-4).id;
    p.connect(b,"B",c,"VCC");p.connect(b,"A",c,"GND");p.connect(c,"D0",t,"G");p.connect(b,"A",t,"S");p.connect(b,"B",res,"A");p.connect(res,"B",led,"A");p.connect(led,"B",t,"D");return p;}
}
