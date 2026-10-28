#pragma once
#include <array>
#include <string>
#include <vector>

namespace e3d {
struct Pin { std::string key; double dx, dz; };
struct Kind { std::string key, name; std::vector<Pin> pins; double defaultValue; };
const std::vector<Kind>& catalog();
const Kind* kindOf(const std::string& kind);

struct Part {
    std::string id, kind;
    double x=0, y=0, value=0;
    int angle=0;
    bool closed=false, blown=false;
};
struct Wire { std::string startId,startPort,endId,endPort; };
struct Board { int width=20, height=16; };
struct Project {
    std::string name="Electric3D board";
    Board board;
    std::vector<Part> parts;
    std::vector<Wire> wires;
    std::string controllerCode;
    int nextId=1;
    Part& add(const std::string& kind, double x, double y);
    Part* find(const std::string& id);
    const Part* find(const std::string& id) const;
    bool connect(const std::string& a,const std::string& pinA,const std::string& b,const std::string& pinB);
    void removePart(const std::string& id);
    bool removeWire(int index);
    std::string toJson() const;
    static Project fromJson(const std::string& text);
    void save(const std::string& path) const;
    static Project load(const std::string& path);
};

struct PartState { bool active=false, conductive=false; double current=0; };
struct Simulation {
    std::vector<PartState> parts;
    std::vector<double> netVoltage;
    std::vector<std::vector<int>> partPins;
    bool poweredController=false;
    std::array<int,4> inputs{};
    std::array<int,4> outputs{};
    double maxCurrent=0;
    std::string warning;
};
Simulation simulate(const Project& project, const std::array<int,4>& outputs);
Project starter();
std::string defaultFirmware();
}
