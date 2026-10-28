#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <shellapi.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <utility>
#include "electric3d/core.hpp"

using namespace e3d;
namespace {
constexpr double PI=3.14159265358979323846;
enum Control {ID_TYPES=100, ID_NEW,ID_OPEN,ID_SAVE,ID_SAVE_AS,ID_SELECT,ID_PLACE,ID_WIRE,ID_ERASE_WIRE,ID_ROTATE,ID_DELETE,ID_TOGGLE,ID_UNDO,ID_SIM,ID_BOARD_W,ID_BOARD_H,ID_BOARD_APPLY,ID_PART_VALUE,ID_VALUE_APPLY,ID_INFO,ID_CODE,ID_COMPILE,ID_RESTORE};
enum Tool {Select,Place,Connect,EraseWire};
HWND window=nullptr;
HWND types=nullptr,boardW=nullptr,boardH=nullptr,partValue=nullptr,info=nullptr,codeEdit=nullptr;
HWND bNew=nullptr,bOpen=nullptr,bSave=nullptr,bSaveAs=nullptr,bSelect=nullptr,bPlace=nullptr,bWire=nullptr,bErase=nullptr,bRotate=nullptr,bDelete=nullptr,bToggle=nullptr,bUndo=nullptr,bSim=nullptr,bBoard=nullptr,bValue=nullptr,bCompile=nullptr,bRestore=nullptr;
HFONT fontUi=nullptr,fontCode=nullptr,fontTitle=nullptr;
Project project=starter();Simulation simulation;
std::wstring openedPath;std::string selectedId;std::wstring status=L"Ready: choose a part, click Place, then click the PCB";
Tool tool=Select;bool simEnabled=true, dragging=false, orbiting=false, ctrlActive=false;
std::string firstPinId,firstPinKey;
std::vector<Project> undoStack;
std::array<int,4> outputs{{0,0,0,0}};
using FirmwareFn=void (__cdecl*)(const int*,int*,double);
HMODULE firmwareDll=nullptr;FirmwareFn firmwareFn=nullptr;
std::wstring firmwareFolder;
double started=0,yaw=.42,pitch=.67,zoom=19;
int W=1270,H=810,screenX=0,screenY=0;
POINT last{};
const wchar_t FILTER[]=L"Electric3D board (*.elctrd)\0*.elctrd\0All files (*.*)\0*.*\0\0";

std::wstring wide(const std::string& u){if(u.empty())return L"";int n=MultiByteToWideChar(CP_UTF8,0,u.c_str(),(int)u.size(),nullptr,0);if(n<=0)return L"?";std::wstring s(n,L'\0');MultiByteToWideChar(CP_UTF8,0,u.c_str(),(int)u.size(),s.data(),n);return s;}
std::string utf8(const std::wstring& w){if(w.empty())return "";int n=WideCharToMultiByte(CP_UTF8,0,w.c_str(),(int)w.size(),nullptr,0,nullptr,nullptr);if(n<=0)return "";std::string s(n,'\0');WideCharToMultiByte(CP_UTF8,0,w.c_str(),(int)w.size(),s.data(),n,nullptr,nullptr);return s;}
std::wstring getText(HWND hwnd){int len=GetWindowTextLengthW(hwnd);std::wstring s(len+1,L'\0');int actual=GetWindowTextW(hwnd,s.data(),len+1);s.resize(actual);return s;}
std::wstring displayNum(double d,int digits=2){wchar_t b[80];swprintf_s(b,80,L"%.*f",digits,d);return b;}
void label(HWND h,const std::wstring& text){SetWindowTextW(h,text.c_str());}
void message(const std::wstring& m){status=m;InvalidateRect(window,nullptr,FALSE);}
void errorMsg(const std::wstring& m){MessageBoxW(window,m.c_str(),L"Electric3D",MB_OK|MB_ICONWARNING);message(m);}
void snapshot(){undoStack.push_back(project);if(undoStack.size()>40)undoStack.erase(undoStack.begin());}
void refreshCode(){SetWindowTextW(codeEdit,wide(project.controllerCode).c_str());}
void persistCode(){if(codeEdit)project.controllerCode=utf8(getText(codeEdit));}
void fillPanel(bool updateInputs=true){
    if(boardW && updateInputs)label(boardW,std::to_wstring(project.board.width));
    if(boardH && updateInputs)label(boardH,std::to_wstring(project.board.height));
    auto* part=project.find(selectedId);
    if(!part){label(info,L"No component selected\nSelect a part on the 3D board");label(partValue,L"");return;}
    auto k=kindOf(part->kind);
    std::wstring infoText=wide(k->name)+L"  ["+wide(part->id)+L"]\nContacts: ";
    for(size_t i=0;i<k->pins.size();i++){if(i)infoText+=L", ";infoText+=wide(k->pins[i].key);}
    if(part->kind=="battery")infoText+=L"\nA = negative, B = positive";
    if(part->kind=="npn"||part->kind=="pnp")infoText+=L"\nB = base, C = collector, E = emitter";
    if(part->kind=="nmos")infoText+=L"\nG = gate, D = drain, S = source";
    if(part->kind=="controller")infoText+=L"\nConnect VCC/GND and D0..D3";
    infoText+=L"\nCurrent: ";
    for(size_t i=0;i<project.parts.size();i++)if(project.parts[i].id==part->id && i<simulation.parts.size())infoText+=displayNum(simulation.parts[i].current*1000.,1)+L" mA";
    label(info,infoText);
    if(updateInputs)label(partValue,displayNum(part->value));
}
void setTool(Tool t){tool=t;firstPinId.clear();firstPinKey.clear();dragging=false;static const wchar_t* msg[]={L"SELECT: drag a component with left mouse",L"PLACE: click the PCB to add a component",L"WIRE: select two gold contacts",L"ERASE: click the wire to delete"};message(msg[(int)t]);}
HWND control(const wchar_t* cls,const wchar_t* txt,DWORD style,int id,int x=0,int y=0,int w=100,int h=28){HWND c=CreateWindowExW((std::wstring(cls)==L"EDIT"?WS_EX_CLIENTEDGE:0),cls,txt,style|WS_CHILD|WS_VISIBLE,x,y,w,h,window,(HMENU)(INT_PTR)id,GetModuleHandleW(nullptr),nullptr);SendMessageW(c,WM_SETFONT,(WPARAM)((id==ID_CODE)?fontCode:fontUi),TRUE);return c;}
HWND btn(const wchar_t* txt,int id){return control(L"BUTTON",txt,BS_PUSHBUTTON,id);}
void pos(HWND h,int x,int y,int w,int hgt){MoveWindow(h,x,y,std::max(1,w),std::max(1,hgt),TRUE);}
void layout(){
    if(!types)return;
    // Toolbar and left component palette
    pos(bNew,10,12,73,30);pos(bOpen,88,12,73,30);pos(bSave,166,12,75,30);pos(bSaveAs,247,12,95,30);
    pos(bSim,347,12,110,30);pos(bUndo,462,12,85,30);
    pos(types,10,80,214,std::max(170,H-354));
    int y=H-261;
    pos(bSelect,10,y,103,34);pos(bPlace,120,y,104,34);
    pos(bWire,10,y+41,103,34);pos(bErase,120,y+41,104,34);
    pos(bRotate,10,y+82,103,34);pos(bDelete,120,y+82,104,34);
    pos(bToggle,10,y+123,214,34);
    int x=W-347;
    pos(info,x+12,83,326,116);
    pos(partValue,x+12,226,175,26);pos(bValue,x+194,226,132,27);
    pos(boardW,x+12,300,62,27);pos(boardH,x+84,300,62,27);pos(bBoard,x+154,300,172,27);
    int editorY=373;int editH=std::max(130,H-editorY-151);
    pos(codeEdit,x+12,editorY,326,editH);
    pos(bCompile,x+12,editorY+editH+8,196,30);pos(bRestore,x+214,editorY+editH+8,124,30);
    InvalidateRect(window,nullptr,FALSE);
}
void undo(){if(undoStack.empty()){message(L"Nothing to undo");return;}project=undoStack.back();undoStack.pop_back();selectedId.clear();refreshCode();fillPanel();message(L"Undo complete");}

bool choosePath(bool saving,std::wstring& path){
    wchar_t file[MAX_PATH]{};if(!path.empty())wcsncpy_s(file,MAX_PATH,path.c_str(),_TRUNCATE);
    OPENFILENAMEW of{};of.lStructSize=sizeof(of);of.hwndOwner=window;of.lpstrFilter=FILTER;of.lpstrFile=file;of.nMaxFile=MAX_PATH;of.lpstrDefExt=L"elctrd";of.Flags=OFN_EXPLORER|OFN_PATHMUSTEXIST|(saving?OFN_OVERWRITEPROMPT:OFN_FILEMUSTEXIST);
    BOOL ok=saving?GetSaveFileNameW(&of):GetOpenFileNameW(&of);if(ok){path=file;return true;}return false;
}
void doSave(bool as){persistCode();try{if(as||openedPath.empty()){std::wstring path=openedPath;if(!choosePath(true,path))return;openedPath=path;}project.save(utf8(openedPath));message(L"Saved: "+openedPath);}catch(const std::exception& ex){errorMsg(wide(ex.what()));}}
void clearFirmware(){if(firmwareDll){FreeLibrary(firmwareDll);firmwareDll=nullptr;} firmwareFn=nullptr;outputs.fill(0);}
void doOpen(){std::wstring path;if(!choosePath(false,path))return;try{auto next=Project::load(utf8(path));clearFirmware();project=std::move(next);undoStack.clear();openedPath=path;selectedId.clear();outputs.fill(0);refreshCode();fillPanel();message(L"Opened: "+path);}catch(const std::exception& ex){errorMsg(wide(ex.what()));}}
void doNew(){snapshot();clearFirmware();project=Project();project.controllerCode=defaultFirmware();openedPath.clear();selectedId.clear();refreshCode();fillPanel();message(L"New board created");}
void rotateSelected(){auto* p=project.find(selectedId);if(!p)return;snapshot();p->angle=(p->angle+90)%360;message(L"Rotated by 90 degrees");}
void toggleSelected(){auto* p=project.find(selectedId);if(!p)return;if(p->kind=="switch"||p->kind=="button"||p->kind=="ldr"){snapshot();p->closed=!p->closed;message(p->closed?L"Switch / light sensor ON":L"Switch / light sensor OFF");}else if(p->kind=="fuse"){snapshot();p->blown=!p->blown;message(p->blown?L"Fuse manually opened":L"Fuse reset");}else message(L"Select a switch, button, LDR or fuse");}

bool compileProcess(const std::wstring& command,const std::wstring& cwd,std::wstring& why){
    std::vector<wchar_t> cmd(command.begin(),command.end());cmd.push_back(L'\0');
    std::wstring logPath=cwd+L"\\compile.log";
    SECURITY_ATTRIBUTES sa{};sa.nLength=sizeof(sa);sa.bInheritHandle=TRUE;
    HANDLE log=CreateFileW(logPath.c_str(),GENERIC_WRITE,FILE_SHARE_READ,&sa,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(log==INVALID_HANDLE_VALUE){why=L"Cannot create compilation log";return false;}
    STARTUPINFOW si{};si.cb=sizeof(si);si.dwFlags=STARTF_USESTDHANDLES;
    si.hStdOutput=log;si.hStdError=log;
    si.hStdInput=GetStdHandle(STD_INPUT_HANDLE);
    if(si.hStdInput==INVALID_HANDLE_VALUE)si.hStdInput=nullptr;
    PROCESS_INFORMATION pi{};
    bool launched=CreateProcessW(nullptr,cmd.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,cwd.c_str(),&si,&pi)!=FALSE;
    if(!launched){CloseHandle(log);why=L"Compiler not found on PATH (g++ or cl.exe)";return false;}
    DWORD wait=WaitForSingleObject(pi.hProcess,30000);
    DWORD exitCode=1;GetExitCodeProcess(pi.hProcess,&exitCode);
    if(wait==WAIT_TIMEOUT){TerminateProcess(pi.hProcess,1);why=L"C++ compilation timed out";}
    CloseHandle(pi.hThread);CloseHandle(pi.hProcess);CloseHandle(log);
    if(wait==WAIT_OBJECT_0 && exitCode==0)return true;
    std::ifstream file(std::filesystem::path(logPath),std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(file)),std::istreambuf_iterator<char>());
    if(content.size()>1600)content=content.substr(content.size()-1600);
    if(!content.empty())why=L"C++ compiler error:\n"+wide(content);
    else if(why.empty())why=L"C++ compiler returned code "+std::to_wstring(exitCode);
    return false;
}
void compileFirmware(){
    persistCode();if(project.controllerCode.size()>95000){errorMsg(L"Firmware source exceeds 95 KB");return;}
    if(firmwareDll){FreeLibrary(firmwareDll);firmwareDll=nullptr;firmwareFn=nullptr;}
    wchar_t temp[MAX_PATH];DWORD n=GetTempPathW(MAX_PATH,temp);if(n==0||n>=MAX_PATH){errorMsg(L"No temporary folder");return;}
    std::wstring dir=std::wstring(temp)+L"Electric3D_Firmware_"+std::to_wstring(GetCurrentProcessId());
    if(!CreateDirectoryW(dir.c_str(),nullptr)&&GetLastError()!=ERROR_ALREADY_EXISTS){errorMsg(L"Cannot create firmware folder");return;}
    firmwareFolder=dir;
    std::wstring src=dir+L"\\firmware.cpp",dll=dir+L"\\firmware.dll";
    {std::ofstream f(std::filesystem::path(src),std::ios::binary|std::ios::trunc);f<<project.controllerCode;if(!f){errorMsg(L"Cannot write firmware.cpp");return;}}
    std::wstring err;
    std::wstring gcc=L"g++ -std=c++17 -O2 -shared -o \""+dll+L"\" \""+src+L"\"";
    bool ok=compileProcess(gcc,dir,err);
    if(!ok && err.find(L"not found on PATH")!=std::wstring::npos){err.clear();std::wstring msvc=L"cl.exe /nologo /LD /EHsc /std:c++17 /Fe:\""+dll+L"\" \""+src+L"\"";ok=compileProcess(msvc,dir,err);}
    if(!ok){errorMsg(L"C++ compilation failed. Install MinGW-w64 (g++), or run Electric3D from a Visual Studio Developer Command Prompt.\n"+err);return;}
    firmwareDll=LoadLibraryW(dll.c_str());if(!firmwareDll){errorMsg(L"Compiled DLL was not loaded");return;}
    firmwareFn=reinterpret_cast<FirmwareFn>(GetProcAddress(firmwareDll,"controller_tick"));
    if(!firmwareFn){FreeLibrary(firmwareDll);firmwareDll=nullptr;errorMsg(L"The function controller_tick was not found in DLL. Use extern \"C\" __declspec(dllexport)");return;}
    started=GetTickCount64()/1000.;message(L"C++ firmware compiled and loaded! Outputs are now controlled by your code.");
}

struct V3{double x,y,z;};struct S2{double x,y;};
RECT viewport(){return {237,54,W-354,H-39};}
S2 project3(V3 p){RECT r=viewport();double c=std::cos(yaw),s=std::sin(yaw);return {(r.left+r.right)*.5+(p.x*c-p.z*s)*zoom, (r.top+r.bottom)*.5+(p.x*s+p.z*c)*zoom*std::sin(pitch)-p.y*zoom*std::cos(pitch)};}
V3 unproject2(int sx,int sy){RECT r=viewport();double xx=(sx-(r.left+r.right)*.5)/zoom, yy=(sy-(r.top+r.bottom)*.5)/(zoom*std::sin(pitch));double c=cos(yaw),s=sin(yaw);return {xx*c+yy*s,0,-xx*s+yy*c};}
V3 partPin(const Part& part,const Pin& pin){double a=part.angle*PI/180;return {part.x+pin.dx*cos(a)-pin.dz*sin(a),.63,part.y+pin.dx*sin(a)+pin.dz*cos(a)};}
COLORREF rgb(int r,int g,int b){return RGB(r,g,b);}
void rect(HDC d,int x,int y,int w,int h,COLORREF color){RECT r{x,y,x+w,y+h};HBRUSH b=CreateSolidBrush(color);FillRect(d,&r,b);DeleteObject(b);}
void line(HDC d,S2 a,S2 b,COLORREF color,int width=1){HPEN pen=CreatePen(PS_SOLID,width,color);HGDIOBJ old=SelectObject(d,pen);MoveToEx(d,(int)std::round(a.x),(int)std::round(a.y),nullptr);LineTo(d,(int)std::round(b.x),(int)std::round(b.y));SelectObject(d,old);DeleteObject(pen);}
void poly(HDC d,std::initializer_list<V3> vert,COLORREF fill,COLORREF border){POINT points[9];int i=0;for(const auto& v:vert){auto p=project3(v);points[i++]={(LONG)p.x,(LONG)p.y};}HBRUSH brush=CreateSolidBrush(fill);HPEN pen=CreatePen(PS_SOLID,1,border);auto oldb=SelectObject(d,brush);auto oldp=SelectObject(d,pen);Polygon(d,points,i);SelectObject(d,oldb);SelectObject(d,oldp);DeleteObject(brush);DeleteObject(pen);}
void disc(HDC d,S2 p,int radius,COLORREF color,COLORREF outline){HBRUSH brush=CreateSolidBrush(color);HPEN pen=CreatePen(PS_SOLID,1,outline);auto oldb=SelectObject(d,brush);auto oldp=SelectObject(d,pen);Ellipse(d,int(p.x)-radius,int(p.y)-radius,int(p.x)+radius,int(p.y)+radius);SelectObject(d,oldb);SelectObject(d,oldp);DeleteObject(brush);DeleteObject(pen);}
void text(HDC h,int x,int y,const std::wstring& txt,COLORREF color,HFONT font=nullptr){SetBkMode(h,TRANSPARENT);SetTextColor(h,color);auto old=SelectObject(h,font?font:fontUi);TextOutW(h,x,y,txt.c_str(),(int)txt.size());SelectObject(h,old);}
void renderBoard(HDC d){double wx=project.board.width*.5,wz=project.board.height*.5;
    // A thin 3D PCB with a visible dark side edge.
    poly(d,{{-wx,-.35,-wz},{wx,-.35,-wz},{wx,-.35,wz},{-wx,-.35,wz}},rgb(18,62,60),rgb(40,110,90));
    poly(d,{{-wx,0,-wz},{wx,0,-wz},{wx,0,wz},{-wx,0,wz}},rgb(22,91,80),rgb(72,188,149));
    for(int x=-project.board.width/2;x<=project.board.width/2;x++){
        line(d,project3({(double)x,.02,-wz}),project3({(double)x,.02,wz}),rgb(30,110,97));
    }
    for(int z=-project.board.height/2;z<=project.board.height/2;z++){
        line(d,project3({-wx,.02,(double)z}),project3({wx,.02,(double)z}),rgb(30,110,97));
    }
    if(zoom>14)for(int x=-project.board.width/2;x<=project.board.width/2;x++)for(int z=-project.board.height/2;z<=project.board.height/2;z++)disc(d,project3({(double)x,.025,(double)z}),1,rgb(92,154,119),rgb(92,154,119));
}
COLORREF partColor(const Part& p,size_t i){
    const auto& k=p.kind;
    if(k=="battery")return rgb(215,158,55);
    if(k=="controller")return rgb(57,151,178);
    if(k=="led")return simulation.parts.size()>i&&simulation.parts[i].active?rgb(255,225,72):rgb(181,88,66);
    if(k=="bulb")return simulation.parts.size()>i&&simulation.parts[i].active?rgb(255,215,68):rgb(164,142,88);
    if(k=="npn"||k=="pnp"||k=="nmos")return rgb(130,93,192);
    if(k=="motor"||k=="buzzer")return rgb(163,115,85);
    if(k=="switch"||k=="button")return p.closed?rgb(78,182,124):rgb(205,112,91);
    if(k=="resistor"||k=="pot"||k=="ldr")return rgb(73,132,202);
    return rgb(92,133,148);
}
void renderPart(HDC d,const Part& part,size_t i){
    double a=part.angle*PI/180;double width=part.kind=="controller"?2.2:1.4,depth=part.kind=="controller"?3.4:1.10;
    auto v=[&](double x,double y,double z)->V3{return {part.x+x*cos(a)-z*sin(a),y,part.y+x*sin(a)+z*cos(a)};};
    double x=width*.5,z=depth*.5;auto c=partColor(part,i);
    bool selected=part.id==selectedId;
    COLORREF edge=selected?rgb(255,229,114):rgb(17,44,48);
    poly(d,{v(-x,.12,-z),v(x,.12,-z),v(x,.12,z),v(-x,.12,z)},rgb(26,46,48),edge);
    poly(d,{v(-x,.2,z),v(x,.2,z),v(x,.95,z),v(-x,.95,z)},rgb(GetRValue(c)*.7,GetGValue(c)*.7,GetBValue(c)*.7),edge);
    poly(d,{v(x,.2,-z),v(x,.2,z),v(x,.95,z),v(x,.95,-z)},rgb(GetRValue(c)*.8,GetGValue(c)*.8,GetBValue(c)*.8),edge);
    poly(d,{v(-x,.95,-z),v(x,.95,-z),v(x,.95,z),v(-x,.95,z)},c,edge);
    auto center=project3(v(0,1.03,0));
    std::wstring name=wide(part.kind);if(part.kind=="controller")name=L"C++ MCU";
    text(d,int(center.x)-(int)name.size()*3,int(center.y)-9,name,selected?rgb(255,252,181):rgb(243,249,255));
    auto pins=kindOf(part.kind)->pins;
    for(const auto& pin:pins){S2 p=project3(partPin(part,pin));disc(d,p,zoom<14?3:5,rgb(247,204,97),rgb(92,69,27));if(zoom>11)text(d,int(p.x)+6,int(p.y)-7,wide(pin.key),rgb(234,246,237));}
}
std::pair<const Part*,const Pin*> findPin(const std::string& partId,const std::string& key){const Part* part=project.find(partId);if(!part)return {nullptr,nullptr};for(const auto& pin:kindOf(part->kind)->pins)if(pin.key==key)return {part,&pin};return {nullptr,nullptr};}
void renderWire(HDC d,const Wire& wire){auto pa=findPin(wire.startId,wire.startPort),pb=findPin(wire.endId,wire.endPort);if(!pa.first||!pb.first)return;
    V3 a=partPin(*pa.first,*pa.second),b=partPin(*pb.first,*pb.second);V3 a2=a,b2=b;a2.y+=.55;b2.y+=.55;
    COLORREF color=rgb(139,153,163);
    for(size_t i=0;i<project.parts.size();i++)if(project.parts[i].id==pa.first->id && i<simulation.partPins.size()){
        for(size_t j=0;j<kindOf(pa.first->kind)->pins.size();j++)if(kindOf(pa.first->kind)->pins[j].key==wire.startPort){auto net=simulation.partPins[i][j];if(net<(int)simulation.netVoltage.size()&&std::abs(simulation.netVoltage[net])>1)color=rgb(91,241,146);} }
    line(d,project3(a),project3(a2),color,3);line(d,project3(a2),project3(b2),color,3);line(d,project3(b2),project3(b),color,3);
}
void render(HDC d){RECT view=viewport();rect(d,0,0,W,H,rgb(19,25,34));
    rect(d,0,48,230,H-85,rgb(31,40,52));
    rect(d,W-354,48,354,H-85,rgb(31,40,52));
    rect(d,view.left,view.top,view.right-view.left,view.bottom-view.top,rgb(13,27,37));
    text(d,12,53,L"COMPONENTS / PARTS",rgb(167,217,229),fontTitle);
    text(d,W-337,57,L"PART INSPECTOR",rgb(167,217,229),fontTitle);
    text(d,W-337,201,L"Value (V / Ohm / uF / A)",rgb(203,212,222));
    text(d,W-337,273,L"PCB size (width x height)",rgb(203,212,222));
    text(d,W-337,346,L"C++ CONTROLLER PROGRAM",rgb(167,217,229),fontTitle);
    renderBoard(d);
    for(const auto& w:project.wires)renderWire(d,w);
    std::vector<size_t> order;for(size_t i=0;i<project.parts.size();i++)order.push_back(i);
    std::sort(order.begin(),order.end(),[](size_t i,size_t j){auto a=project3({project.parts[i].x,0,project.parts[i].y}),b=project3({project.parts[j].x,0,project.parts[j].y});return a.y<b.y;});
    for(size_t i:order)renderPart(d,project.parts[i],i);
    if(!firstPinId.empty()){
        auto a=findPin(firstPinId,firstPinKey);if(a.first){line(d,project3(partPin(*a.first,*a.second)),{(double)last.x,(double)last.y},rgb(255,225,96),2);}
    }
    text(d,view.left+10,view.top+8,L"PCB  "+std::to_wstring(project.board.width)+L" x "+std::to_wstring(project.board.height)+L"    "+std::to_wstring(project.parts.size())+L" parts / "+std::to_wstring(project.wires.size())+L" wires",rgb(185,227,220));
    text(d,view.left+10,view.bottom-24,L"Right drag: orbit  |  Wheel: zoom  |  Left drag: move part",rgb(172,207,215));
    rect(d,0,H-35,W,35,rgb(43,58,70));
    text(d,12,H-29,status,rgb(232,241,247));
    std::wstring stats=L"    Firmware: ";stats+=(firmwareFn?L"compiled C++":L"built-in C++ demo");stats+=(simulation.poweredController?L" | MCU powered":L" | MCU unpowered");
    text(d,W-390,H-29,stats,rgb(171,220,182));
    if(!simulation.warning.empty())text(d,view.left+10,view.top+34,wide(simulation.warning),rgb(248,155,105));
}

bool insideView(int x,int y){RECT r=viewport();return x>=r.left&&x<r.right&&y>=r.top&&y<r.bottom;}
struct HitPin{std::string id,pin;double d=1e10;};
HitPin pickPin(int x,int y){HitPin best;for(const auto& p:project.parts)for(const auto& pin:kindOf(p.kind)->pins){auto s=project3(partPin(p,pin));double d=std::hypot(s.x-x,s.y-y);if(d<best.d){best.id=p.id;best.pin=pin.key;best.d=d;}}return best;}
std::string pickPart(int x,int y){double best=1e10;std::string id;for(const auto& p:project.parts){auto sc=project3({p.x,.65,p.y});double dist=std::hypot(sc.x-x,sc.y-y);if(dist<best){best=dist;id=p.id;}}return best<zoom*1.25+7?id:"";}
int pickWire(int x,int y){int best=-1;double dist=9;for(size_t i=0;i<project.wires.size();i++){auto pa=findPin(project.wires[i].startId,project.wires[i].startPort),pb=findPin(project.wires[i].endId,project.wires[i].endPort);if(!pa.first||!pb.first)continue;auto a=project3(partPin(*pa.first,*pa.second)),b=project3(partPin(*pb.first,*pb.second));double dx=b.x-a.x,dy=b.y-a.y;double t=(dx*dx+dy*dy)>1e-6?std::clamp(((x-a.x)*dx+(y-a.y)*dy)/(dx*dx+dy*dy),0.,1.):0;double d=std::hypot(x-(a.x+t*dx),y-(a.y+t*dy));if(d<dist){dist=d;best=(int)i;}}return best;}
void clickScene(int x,int y){if(!insideView(x,y))return;
    if(tool==Place){int sel=(int)SendMessageW(types,LB_GETCURSEL,0,0);if(sel==LB_ERR)return;const auto& kinds=catalog();if(sel>=(int)kinds.size())return;V3 hit=unproject2(x,y);hit.x=std::round(hit.x*2)/2;hit.z=std::round(hit.z*2)/2;try{snapshot();auto& p=project.add(kinds[sel].key,hit.x,hit.z);selectedId=p.id;fillPanel();message(L"Placed "+wide(kinds[sel].name));}catch(const std::exception& ex){if(!undoStack.empty())undoStack.pop_back();message(wide(ex.what()));}return;}
    if(tool==Connect){auto p=pickPin(x,y);if(p.d>13){message(L"Click a gold contact to connect a wire");return;}if(firstPinId.empty()){firstPinId=p.id;firstPinKey=p.pin;message(L"Contact "+wide(p.pin)+L" selected. Click another contact.");}else{snapshot();bool okay=project.connect(firstPinId,firstPinKey,p.id,p.pin);if(!okay){undoStack.pop_back();message(L"Invalid or duplicate wire. Use two different components.");}else message(L"Contacts connected by wire");firstPinId.clear();firstPinKey.clear();}return;}
    if(tool==EraseWire){int index=pickWire(x,y);if(index>=0){snapshot();project.removeWire(index);message(L"Wire deleted");}else message(L"Click near a wire segment");return;}
    auto id=pickPart(x,y);selectedId=id;fillPanel();if(!id.empty()){snapshot();dragging=true;SetCapture(window);message(L"Selected "+wide(id)+L". Drag to move on PCB.");}else message(L"No part selected");
}
void updateSim(){if(!simEnabled){simulation={};return;}
    simulation=simulate(project,outputs);
    for(size_t i=0;i<project.parts.size();i++){
        auto& part=project.parts[i];
        if(part.kind=="fuse" && !part.blown && i<simulation.parts.size() && simulation.parts[i].current>std::max(.01,part.value)){
            part.blown=true;message(L"Fuse tripped (educational DC model). Select it and click Toggle to reset.");
            simulation=simulate(project,outputs);
        }
    }
    if(simulation.poweredController){double t=GetTickCount64()/1000.-started;std::array<int,4> next{{0,0,0,0}};
        if(firmwareFn){int tmp[4]={0,0,0,0};firmwareFn(simulation.inputs.data(),tmp,t);for(int i=0;i<4;i++)next[i]=tmp[i]?1:0;}
        else {next[0]=((int)(t*2)%2)==0;next[1]=simulation.inputs[1];}
        outputs=next;simulation=simulate(project,outputs);
    }else outputs.fill(0);
    fillPanel(false);
}
void onButton(int id){
    if(id==ID_NEW)doNew();else if(id==ID_OPEN)doOpen();else if(id==ID_SAVE)doSave(false);else if(id==ID_SAVE_AS)doSave(true);
    else if(id==ID_SELECT)setTool(Select);else if(id==ID_PLACE)setTool(Place);else if(id==ID_WIRE)setTool(Connect);else if(id==ID_ERASE_WIRE)setTool(EraseWire);
    else if(id==ID_UNDO)undo();else if(id==ID_ROTATE)rotateSelected();else if(id==ID_TOGGLE)toggleSelected();
    else if(id==ID_DELETE){if(!selectedId.empty()){snapshot();project.removePart(selectedId);selectedId.clear();fillPanel();message(L"Component deleted");}}
    else if(id==ID_SIM){simEnabled=!simEnabled;label(bSim,simEnabled?L"Simulate: ON":L"Simulate: OFF");message(simEnabled?L"DC simulation enabled":L"DC simulation paused");}
    else if(id==ID_BOARD_APPLY){try{int width=std::stoi(getText(boardW)),height=std::stoi(getText(boardH));if(width<6||height<6||width>60||height>60)throw std::runtime_error("Board must be 6..60 units");for(const auto& p:project.parts)if(std::abs(p.x)>width*.5||std::abs(p.y)>height*.5)throw std::runtime_error("Move parts inside the new board first");snapshot();project.board={width,height};message(L"PCB size updated");}catch(const std::exception& ex){errorMsg(wide(ex.what()));}fillPanel();}
    else if(id==ID_VALUE_APPLY){auto p=project.find(selectedId);if(!p)return;try{double v=std::stod(getText(partValue));if(!std::isfinite(v)||v<0||v>1e7)throw std::runtime_error("Value must be 0..10 million");snapshot();p->value=v;message(L"Part value updated");}catch(const std::exception& ex){errorMsg(wide(ex.what()));}}
    else if(id==ID_COMPILE)compileFirmware();else if(id==ID_RESTORE){SetWindowTextW(codeEdit,wide(defaultFirmware()).c_str());message(L"Example C++ program restored; compile to apply it");}
    InvalidateRect(window,nullptr,FALSE);
}
LRESULT CALLBACK wndProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    switch(msg){
    case WM_CREATE:{window=hwnd;started=GetTickCount64()/1000.;fontUi=CreateFontW(-16,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");fontTitle=CreateFontW(-17,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");fontCode=CreateFontW(-14,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,FIXED_PITCH,L"Consolas");
        bNew=btn(L"New",ID_NEW);bOpen=btn(L"Open",ID_OPEN);bSave=btn(L"Save",ID_SAVE);bSaveAs=btn(L"Save as",ID_SAVE_AS);bSim=btn(L"Simulate: ON",ID_SIM);bUndo=btn(L"Undo",ID_UNDO);
        types=control(L"LISTBOX",L"",WS_BORDER|WS_VSCROLL|LBS_NOTIFY,ID_TYPES);for(const auto& k:catalog())SendMessageW(types,LB_ADDSTRING,0,(LPARAM)wide(k.name).c_str());SendMessageW(types,LB_SETCURSEL,0,0);
        bSelect=btn(L"Select / Move",ID_SELECT);bPlace=btn(L"Place part",ID_PLACE);bWire=btn(L"Connect pins",ID_WIRE);bErase=btn(L"Erase wire",ID_ERASE_WIRE);bRotate=btn(L"Rotate 90 deg",ID_ROTATE);bDelete=btn(L"Delete part",ID_DELETE);bToggle=btn(L"Toggle switch / reset fuse",ID_TOGGLE);
        info=control(L"STATIC",L"",SS_LEFT,ID_INFO);partValue=control(L"EDIT",L"",ES_AUTOHSCROLL,ID_PART_VALUE);bValue=btn(L"Apply value",ID_VALUE_APPLY);
        boardW=control(L"EDIT",L"20",ES_AUTOHSCROLL|ES_NUMBER,ID_BOARD_W);boardH=control(L"EDIT",L"16",ES_AUTOHSCROLL|ES_NUMBER,ID_BOARD_H);bBoard=btn(L"Resize board",ID_BOARD_APPLY);
        codeEdit=control(L"EDIT",L"",ES_MULTILINE|ES_AUTOVSCROLL|ES_WANTRETURN|WS_VSCROLL,ID_CODE);SendMessageW(codeEdit,EM_SETLIMITTEXT,95000,0);
        bCompile=btn(L"Compile + Load C++",ID_COMPILE);bRestore=btn(L"Example code",ID_RESTORE);
        RECT rc{};GetClientRect(hwnd,&rc);W=rc.right;H=rc.bottom;layout();refreshCode();fillPanel();SetTimer(hwnd,1,180,nullptr);return 0;
    }
    case WM_SIZE:W=LOWORD(lp);H=HIWORD(lp);layout();return 0;
    case WM_GETMINMAXINFO:{auto* m=reinterpret_cast<MINMAXINFO*>(lp);m->ptMinTrackSize={1090,720};return 0;}
    case WM_COMMAND:{int id=LOWORD(wp),event=HIWORD(wp);if(id==ID_TYPES&&event==LBN_DBLCLK)setTool(Place);else if(event==BN_CLICKED)onButton(id);return 0;}
    case WM_PAINT:{PAINTSTRUCT ps;HDC dc=BeginPaint(hwnd,&ps);RECT r{};GetClientRect(hwnd,&r);HDC back=CreateCompatibleDC(dc);HBITMAP bmp=CreateCompatibleBitmap(dc,r.right,r.bottom);auto old=SelectObject(back,bmp);render(back);BitBlt(dc,0,0,r.right,r.bottom,back,0,0,SRCCOPY);SelectObject(back,old);DeleteObject(bmp);DeleteDC(back);EndPaint(hwnd,&ps);return 0;}
    case WM_ERASEBKGND:return 1;
    case WM_LBUTTONDOWN:{int x=GET_X_LPARAM(lp),y=GET_Y_LPARAM(lp);last={x,y};clickScene(x,y);InvalidateRect(hwnd,nullptr,FALSE);return 0;}
    case WM_LBUTTONUP:dragging=false;ReleaseCapture();return 0;
    case WM_RBUTTONDOWN:last={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};orbiting=insideView(last.x,last.y);if(orbiting)SetCapture(hwnd);return 0;
    case WM_RBUTTONUP:orbiting=false;ReleaseCapture();return 0;
    case WM_MOUSEMOVE:{int x=GET_X_LPARAM(lp),y=GET_Y_LPARAM(lp);if(orbiting){yaw+=(x-last.x)*.009;pitch=std::clamp(pitch+(y-last.y)*.006,.15,1.45);InvalidateRect(hwnd,nullptr,FALSE);}
        else if(dragging){auto p=project.find(selectedId);if(p){auto v=unproject2(x,y);p->x=std::clamp(std::round(v.x*2)/2,-project.board.width*.5,project.board.width*.5);p->y=std::clamp(std::round(v.z*2)/2,-project.board.height*.5,project.board.height*.5);InvalidateRect(hwnd,nullptr,FALSE);}}
        else if(!firstPinId.empty())InvalidateRect(hwnd,nullptr,FALSE);
        last={x,y};return 0;}
    case WM_MOUSEWHEEL:{int dir=GET_WHEEL_DELTA_WPARAM(wp);zoom=std::clamp(zoom*(dir>0?1.1:.9),5.,40.);InvalidateRect(hwnd,nullptr,FALSE);return 0;}
    case WM_KEYDOWN:{bool ctrl=(GetKeyState(VK_CONTROL)&0x8000)!=0;if(ctrl&&wp=='S'){doSave(false);return 0;}if(ctrl&&wp=='O'){doOpen();return 0;}if(ctrl&&wp=='N'){doNew();return 0;}if(ctrl&&wp=='Z'){undo();return 0;}
        if(GetFocus()==codeEdit||GetFocus()==partValue)return DefWindowProcW(hwnd,msg,wp,lp);
        if(wp==VK_DELETE){onButton(ID_DELETE);return 0;}if(wp=='R'){rotateSelected();return 0;}if(wp==VK_ESCAPE){setTool(Select);return 0;}return 0;}
    case WM_TIMER:try{updateSim();InvalidateRect(hwnd,nullptr,FALSE);}catch(const std::exception& ex){message(wide(ex.what()));}return 0;
    case WM_DESTROY:if(firmwareDll)FreeLibrary(firmwareDll);if(fontUi)DeleteObject(fontUi);if(fontTitle)DeleteObject(fontTitle);if(fontCode)DeleteObject(fontCode);PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}
}
int WINAPI wWinMain(HINSTANCE h,HINSTANCE,LPWSTR,int show){
    WNDCLASSEXW wc{};wc.cbSize=sizeof(wc);wc.lpfnWndProc=wndProc;wc.hInstance=h;wc.lpszClassName=L"Electric3DNativeWindow";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);wc.style=CS_DBLCLKS;
    if(!RegisterClassExW(&wc))return 2;
    HWND w=CreateWindowExW(0,wc.lpszClassName,L"Electric3D v0.3 - Native C++ PCB & Transistor Lab",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,1270,810,nullptr,nullptr,h,nullptr);
    if(!w)return 3;ShowWindow(w,show);UpdateWindow(w);
    MSG msg;while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}return (int)msg.wParam;
}
