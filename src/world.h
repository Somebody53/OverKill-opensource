#pragma once

#include "raylib.h"

#include <vector>

namespace world {

constexpr float BallRadius = 0.3f;
constexpr float RoadSpacing = 40.0f;
constexpr float RoadSpan = 165.0f;

struct Rect {
    float x0, z0, x1, z1;
};

struct Bump {
    float x, z, r, h;
};

struct Plot {
    Rect rect;
    float h;
    float blend;
    Color color{160, 150, 130, 255};
};

struct Field {
    Rect rect;
    Color light;
    Color dark;
};

struct Terrain {
    float half = 100.0f;
    float amp = 0.0f;
    float seed = 0.0f;
    Color grass{70, 120, 60, 255};
    Color dirt{125, 100, 70, 255};
    std::vector<Bump> hills;
    std::vector<Bump> dents;
    std::vector<Plot> plots;
    std::vector<Field> fields;
};

enum class PropKind {
    Crate, Wall, Net, Flat, House, Barn, Silo, Tree,
    Couch, Tv, Table, Pack, Shelf, Bed, Fridge, Plant, Window, Person
};

struct Prop {
    PropKind kind;
    Vector3 base;
    float w, h, d;
    Color color;
    Color accent;
    bool broken = false;
    int damage = 0;
};

struct Box {
    Vector3 min;
    Vector3 max;
};

struct Pitch {
    Vector3 center;
    float w, d;
};

struct Car {
    float pos;
    float lane;
    float dir;
    float speed;
    bool alongX;
    bool police;
    Color color;
};

constexpr float DoorWidth = 1.6f;
constexpr float DoorHeight = 2.3f;
constexpr float DoorThick = 0.15f;

// Hinged front door of a house; shut is its solid box while closed.
struct Door {
    Vector3 hinge;
    Vector3 center;
    Box shut;
    size_t solid;
    bool open = false;
    float angle = 0.0f;
};

struct Level {
    Terrain terrain;
    std::vector<Box> solids;
    std::vector<Prop> props;
    std::vector<Pitch> pitches;
    std::vector<Vector3> ballSpawns;
    std::vector<Vector3> lamps;
    std::vector<Vector3> bulbs;
    std::vector<Vector3> signals;
    std::vector<Car> cars;
    std::vector<Door> doors;
    Vector3 spawn{};
    Color sky{135, 190, 235, 255};
    Model model{};
    bool modelLoaded = false;
};

float groundAt(const Terrain &t, float x, float z);
Vector3 sunDirection();
void finishLevel(Level &L);

void buildClassic(Level &L);
void buildValley(Level &L);
void buildFarm(Level &L);
void buildVillage(Level &L);
void buildQuarry(Level &L);
void buildDesert(Level &L);
void buildSnowfield(Level &L);
void buildForest(Level &L);
void buildCity(Level &L);
void buildCanyon(Level &L);
void buildSuburb(Level &L);
void buildIndustrial(Level &L);
void buildRuins(Level &L);
void buildMesa(Level &L);
void buildOrchard(Level &L);
void buildContainers(Level &L);
void buildPlaza(Level &L);
void buildJungle(Level &L);
void buildHarbor(Level &L);
void buildCompound(Level &L);
void buildArena(Level &L);
void buildBadlands(Level &L);

} // namespace world
