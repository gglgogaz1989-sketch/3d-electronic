#include "electric3d/core.hpp"
#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>
#include <string>
#include <vector>
namespace e3d {
namespace {
struct DSU {std::vector<int> p;explicit DSU(int n):p(n){std::iota(p.begin(),p.end(),0);}int get(int a){return p[a]==a?a:p[a]=get(p[a]);}void link(int a,int b){a=get(a);b=get(b);if(a!=b)p[b]=a;}};
struct Solver {
    int n;std::vector<std::vector<double>> a;std::vector<double> rhs;
    explicit Solver(int count):n(count),a(count,std::vector<double>(count,0)),rhs(count,0){for(int i=0;i<n;i++)a[i][i]=1e-8;}
    void res(int u,int v,double r,double bias=0){if(u<0||v<0||u==v||r<=0)return;double g=1/r;a[u][u]+=g;a[v][v]+=g;a[u][v]-=g;a[v][u]-=g;rhs[u]+=g*bias;rhs[v]-=g*bias;}
    void anchor(int u){if(u>=0){a[u][u]+=1;}}
    std::vector<double> solve(){for(int k=0;k<n;k++){int best=k;for(int j=k+1;j<n;j++)if(std::abs(a[j][k])>std::abs(a[best][k]))best=j;if(best!=k){std::swap(a[k],a[best]);std::swap(rhs[k],rhs[best]);}
        double pivot=a[k][k];if(std::abs(pivot)<1e-18)continue;
        for(int i=k+1;i<n;i++){double f=a[i][k]/pivot;if(std::abs(f)<1e-22)continue;a[i][k]=0;for(int j=k+1;j<n;j++)a[i][j]-=f*a[k][j];rhs[i]-=f*rhs[k];}}
        std::vector<double> v(n,0);for(int i=n-1;i>=0;i--){double r=rhs[i];for(int j=i+1;j<n;j++)r-=a[i][j]*v[j];if(std::abs(a[i][i])>1e-18)v[i]=std::clamp(r/a[i][i],-1e6,1e6);}return v;
    }
};
}
Simulation simulate(const Project& p,const std::array<int,4>& outputs){
    Simulation result;result.parts.resize(p.parts.size());result.partPins.resize(p.parts.size());result.outputs=outputs;
    std::map<std::pair<std::string,std::string>,int> pinIndex;
    int total=0;
    for(size_t i=0;i<p.parts.size();i++){for(const auto& pin:kindOf(p.parts[i].kind)->pins){pinIndex[{p.parts[i].id,pin.key}]=total++;result.partPins[i].push_back(total-1);}}
    if(total==0)return result;
    DSU d(total);
    for(const auto& w:p.wires){auto a=pinIndex.find({w.startId,w.startPort}),b=pinIndex.find({w.endId,w.endPort});if(a!=pinIndex.end()&&b!=pinIndex.end())d.link(a->second,b->second);}
    std::map<int,int> netIds;int nn=0;for(auto& pins:result.partPins)for(auto& pin:pins){int root=d.get(pin);auto it=netIds.find(root);if(it==netIds.end())it=netIds.insert({root,nn++}).first;pin=it->second;}
    std::vector<double> v(nn,0);std::vector<bool> on(p.parts.size(),false), powered(p.parts.size(),false);
    auto voltage=[&](size_t i,int j){int net=result.partPins[i][j];return v[net];};
    for(int iter=0;iter<15;iter++){
        Solver s(nn);
        // Reference: battery negative pin is connected to earth through a 1-ohm display reference.
        for(size_t i=0;i<p.parts.size();i++)if(p.parts[i].kind=="battery"){s.anchor(result.partPins[i][0]);break;}
        for(size_t i=0;i<p.parts.size();i++){
            const auto& part=p.parts[i];const auto& pins=result.partPins[i];
            auto stamp=[&](int x,int y,double r,double bias=0){s.res(pins[x],pins[y],r,bias);};
            const auto& k=part.kind;double val=part.value;
            if(k=="battery")stamp(1,0,.5,std::clamp(val,0.01,48.0));
            else if(k=="resistor"||k=="bulb"||k=="motor"||k=="buzzer")stamp(0,1,std::max(val,.2));
            else if(k=="ammeter")stamp(0,1,.05);
            else if(k=="voltmeter")stamp(0,1,1e6);
            else if(k=="switch"||k=="button") {if(part.closed)stamp(0,1,.05);}
            else if(k=="header")stamp(0,1,.02);
            else if(k=="fuse") {if(!part.blown)stamp(0,1,.05);}
            else if(k=="led"||k=="diode") {if(on[i])stamp(0,1,k=="led"?15:2,k=="led"?2:.7);}
            else if(k=="ldr")stamp(0,1,std::max(.2,val*(part.closed?.20:5.0)));
            else if(k=="pot"){stamp(0,1,std::max(1.,val*.5));stamp(1,2,std::max(1.,val*.5));}
            else if(k=="npn"||k=="pnp"||k=="nmos"){if(on[i])stamp(1,2,.5);}
            else if(k=="relay") {stamp(0,1,75);if(on[i])stamp(2,3,.05);}
            else if(k=="controller" && powered[i]){ for(int k=0;k<4;k++)stamp(2+k,0,10,outputs[k]?5:0); }
            // Capacitors are open circuits in this DC steady-state model.
        }
        auto newV=s.solve();
        bool changed=false;
        // A fixed threshold is used for diode and transistor digital approximations.
        for(size_t i=0;i<p.parts.size();i++){
            const auto& part=p.parts[i];const auto& k=part.kind;const auto& pins=result.partPins[i];
            auto nv=[&](int j){return newV[pins[j]];};
            bool next=on[i];
            if(k=="led")next=(nv(0)-nv(1)>1.96);
            else if(k=="diode")next=(nv(0)-nv(1)>.68);
            else if(k=="npn")next=nv(0)-nv(2)>.66;
            else if(k=="pnp")next=nv(2)-nv(0)>.66;
            else if(k=="nmos")next=nv(0)-nv(2)>2;
            else if(k=="relay")next=std::abs(nv(0)-nv(1))>3;
            else if(k=="controller") {bool nextPower=nv(1)-nv(0)>3.0;if(powered[i]!=nextPower){powered[i]=nextPower;changed=true;}}
            if(next!=on[i]){on[i]=next;changed=true;}
        }
        v=std::move(newV);if(!changed)break;
    }
    result.netVoltage=v;
    for(size_t i=0;i<p.parts.size();i++){
        const auto& part=p.parts[i];const auto& k=part.kind;auto& s=result.parts[i];
        auto vdiff=[&](int j,int l){return voltage(i,j)-voltage(i,l);};
        if(k=="battery")s.current=std::abs((vdiff(1,0)-part.value)/.5);
        else if(k=="resistor"||k=="bulb"||k=="motor"||k=="buzzer")s.current=std::abs(vdiff(0,1)/std::max(.2,part.value));
        else if(k=="ammeter")s.current=std::abs(vdiff(0,1)/.05);
        else if(k=="voltmeter")s.current=std::abs(vdiff(0,1)/1e6);
        else if(k=="header")s.current=std::abs(vdiff(0,1)/.02);
        else if(k=="switch"||k=="button")s.current=part.closed?std::abs(vdiff(0,1)/.05):0;
        else if(k=="fuse")s.current=part.blown?0:std::abs(vdiff(0,1)/.05);
        else if(k=="diode"||k=="led")s.current=on[i]?std::max(0.,(vdiff(0,1)-(k=="led"?2:.7))/(k=="led"?15:2)):0;
        else if(k=="npn"||k=="pnp"||k=="nmos")s.current=on[i]?std::abs(vdiff(1,2)/.5):0;
        else if(k=="relay")s.current=on[i]?std::abs(vdiff(2,3)/.05):0;
        else if(k=="ldr")s.current=std::abs(vdiff(0,1)/std::max(.2,part.value*(part.closed?.2:5)));
        else if(k=="pot")s.current=std::abs(vdiff(0,1)/std::max(1.,part.value*.5))+std::abs(vdiff(1,2)/std::max(1.,part.value*.5));
        s.active=(s.current>0.0001);s.conductive=on[i];result.maxCurrent=std::max(result.maxCurrent,s.current);
        if(k=="controller") {
            if(powered[i]){result.poweredController=true;for(int q=0;q<4;q++)result.inputs[q]=voltage(i,q+2)-voltage(i,0)>2.4?1:0;}
        }
        if(k=="fuse"&&s.current>std::max(.01,part.value))result.warning="Fuse over-current (DC model; no automatic damage)";
    }
    if(result.maxCurrent>8)result.warning="High current / possible short circuit (approximation)";
    return result;
}
}
