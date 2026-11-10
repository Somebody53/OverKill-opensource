#include "world.h"

#include "raymath.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iterator>

namespace world {
namespace {

constexpr float Pi = 3.14159265f;
constexpr float CellSize = 2.0f;
constexpr int TerrainCells = 160;

// '#' wall, '.' floor, 'S' player spawn
const char *ClassicRows[] = {
    "#########################",
    "#.......#.........#.....#",
    "#.......#.........#.....#",
    "#...##......###.........#",
    "#...##...#.#...#...###..#",
    "#.......#.#.......#.....#",
    "#####.#####...#...#####.#",
    "#.......#.........#.....#",
    "#...##.........#........#",
    "#.....#.....#.....#.....#",
    "#.###.#.###.#.....#.....#",
    "#.........#.....#.......#",
    "#.#.#.#.#.#.#.#.#.#.#.#.#",
    "#.........#.....#.....#.#",
    "#.###.#.###.#.....#.....#",
    "#.........S.....#.......#",
    "#####.#####...#...#####.#",
    "#.......#.........#.....#",
    "#...##......###.........#",
    "#.......#...#.....#.....#",
    "#########################",
};

struct CropPalette {
    Color light;
    Color dark;
};

const CropPalette Crops[] = {
    {Color{70, 140, 50, 255}, Color{55, 115, 40, 255}},    // corn
    {Color{205, 180, 80, 255}, Color{185, 160, 60, 255}},  // wheat
    {Color{120, 160, 60, 255}, Color{100, 140, 50, 255}},  // potatoes
    {Color{170, 190, 90, 255}, Color{150, 170, 70, 255}},  // beets
    {Color{130, 90, 150, 255}, Color{110, 75, 130, 255}},  // lavender
};

const Color WallColors[] = {
    Color{210, 200, 180, 255}, Color{180, 160, 130, 255}, Color{200, 195, 205, 255}, Color{170, 120, 90, 255}};
const Color RoofColors[] = {
    Color{120, 50, 40, 255}, Color{60, 70, 90, 255}, Color{90, 60, 40, 255}, Color{70, 70, 70, 255}};

struct Rng {
    unsigned s;
    float next() {
        s = s * 1664525u + 1013904223u;
        return (s >> 8) / 16777216.0f;
    }
    float range(float a, float b) { return a + (b - a) * next(); }
    int index(int n) { return std::min(n - 1, static_cast<int>(next() * n)); }
};

template <typename T, size_t N>
const T &pick(Rng &r, const T (&items)[N]) {
    return items[r.index(static_cast<int>(N))];
}

bool inRect(const Rect &r, float x, float z, float margin) {
    return x >= r.x0 - margin && x <= r.x1 + margin && z >= r.z0 - margin && z <= r.z1 + margin;
}

bool overlaps(const Rect &a, const Rect &b, float margin) {
    return a.x0 < b.x1 + margin && b.x0 < a.x1 + margin && a.z0 < b.z1 + margin && b.z0 < a.z1 + margin;
}

float rawHeight(const Terrain &t, float x, float z) {
    float h = t.amp * (0.6f * std::sin(x * 0.037f + t.seed) * std::cos(z * 0.043f) +
                       0.4f * std::sin((x + z) * 0.09f + t.seed * 0.5f));
    for (const Bump &b : t.hills) {
        const float d2 = (x - b.x) * (x - b.x) + (z - b.z) * (z - b.z);
        h += b.h * std::exp(-d2 / (b.r * b.r));
    }
    for (const Bump &b : t.dents) {
        const float d2 = (x - b.x) * (x - b.x) + (z - b.z) * (z - b.z);
        h -= b.h * std::exp(-d2 / (b.r * b.r));
    }
    for (const Field &f : t.fields) {
        if (inRect(f.rect, x, z, 0.0f)) {
            h += 0.12f * std::sin((x - f.rect.x0) * 2.0f * Pi / 2.5f);
        }
    }
    return h;
}

Color surfaceColor(const Terrain &t, float x, float z, float upY) {
    Color base = t.grass;
    bool special = false;
    for (const Field &f : t.fields) {
        if (inRect(f.rect, x, z, 0.0f)) {
            const int stripe = static_cast<int>(std::floor((x - f.rect.x0) / 1.25f));
            base = (stripe % 2 == 0) ? f.light : f.dark;
            special = true;
        }
    }
    for (const Plot &p : t.plots) {
        if (inRect(p.rect, x, z, 0.0f)) {
            base = p.color;
            special = true;
        }
    }
    if (!special && upY < 0.7f) {
        base = t.dirt;
    }
    const float shade = 0.55f + 0.45f * upY;
    return Color{static_cast<unsigned char>(base.r * shade), static_cast<unsigned char>(base.g * shade),
                 static_cast<unsigned char>(base.b * shade), 255};
}

Mesh buildTerrainMesh(const Terrain &t, int n) {
    const int v = n + 1;
    const float cell = 2.0f * t.half / n;
    std::vector<float> hs(v * v);
    for (int j = 0; j < v; ++j) {
        for (int i = 0; i < v; ++i) {
            hs[j * v + i] = groundAt(t, -t.half + i * cell, -t.half + j * cell);
        }
    }

    Mesh m{};
    m.vertexCount = v * v;
    m.triangleCount = n * n * 2;
    m.vertices = static_cast<float *>(std::malloc(sizeof(float) * 3 * m.vertexCount));
    m.normals = static_cast<float *>(std::malloc(sizeof(float) * 3 * m.vertexCount));
    m.colors = static_cast<unsigned char *>(std::malloc(sizeof(unsigned char) * 4 * m.vertexCount));
    m.texcoords = static_cast<float *>(std::malloc(sizeof(float) * 2 * m.vertexCount));
    m.indices = static_cast<unsigned short *>(std::malloc(sizeof(unsigned short) * 3 * m.triangleCount));

    for (int j = 0; j < v; ++j) {
        for (int i = 0; i < v; ++i) {
            const int k = j * v + i;
            const float x = -t.half + i * cell;
            const float z = -t.half + j * cell;
            const int il = std::max(i - 1, 0), ir = std::min(i + 1, v - 1);
            const int jd = std::max(j - 1, 0), ju = std::min(j + 1, v - 1);
            const float dx = (hs[j * v + ir] - hs[j * v + il]) / ((ir - il) * cell);
            const float dz = (hs[ju * v + i] - hs[jd * v + i]) / ((ju - jd) * cell);
            const Vector3 nrm = Vector3Normalize(Vector3{-dx, 1.0f, -dz});
            const Color c = surfaceColor(t, x, z, nrm.y);

            m.vertices[3 * k] = x;
            m.vertices[3 * k + 1] = hs[k];
            m.vertices[3 * k + 2] = z;
            m.normals[3 * k] = nrm.x;
            m.normals[3 * k + 1] = nrm.y;
            m.normals[3 * k + 2] = nrm.z;
            m.colors[4 * k] = c.r;
            m.colors[4 * k + 1] = c.g;
            m.colors[4 * k + 2] = c.b;
            m.colors[4 * k + 3] = 255;
            // Terrain UVs spanning the whole square.
            m.texcoords[2 * k] = static_cast<float>(i) / n;
            m.texcoords[2 * k + 1] = static_cast<float>(j) / n;
        }
    }

    int q = 0;
    for (int j = 0; j < n; ++j) {
        for (int i = 0; i < n; ++i) {
            const unsigned short a = static_cast<unsigned short>(j * v + i);
            const unsigned short b = static_cast<unsigned short>(a + 1);
            const unsigned short c = static_cast<unsigned short>(a + v);
            const unsigned short d = static_cast<unsigned short>(c + 1);
            m.indices[q++] = a;
            m.indices[q++] = c;
            m.indices[q++] = b;
            m.indices[q++] = b;
            m.indices[q++] = c;
            m.indices[q++] = d;
        }
    }
    UploadMesh(&m, false);
    return m;
}

void addProp(Level &L, PropKind kind, float cx, float cz, float w, float h, float d, Color c, Color accent,
             bool solid) {
    const float y = groundAt(L.terrain, cx, cz);
    L.props.push_back({kind, Vector3{cx, y, cz}, w, h, d, c, accent});
    if (solid) {
        L.solids.push_back({Vector3{cx - w / 2, y, cz - d / 2}, Vector3{cx + w / 2, y + h, cz + d / 2}});
    }
}

void addPlot(Level &L, float cx, float cz, float w, float d, float blend,
             Color color = Color{160, 150, 130, 255}) {
    const Rect rect{cx - w / 2 - 1.0f, cz - d / 2 - 1.0f, cx + w / 2 + 1.0f, cz + d / 2 + 1.0f};
    L.terrain.plots.push_back({rect, rawHeight(L.terrain, cx, cz), blend, color});
}

void addBox(Level &L, PropKind kind, Vector3 lo, Vector3 hi, Color c, Color accent, bool solid) {
    L.props.push_back({kind, Vector3{(lo.x + hi.x) / 2, lo.y, (lo.z + hi.z) / 2}, hi.x - lo.x, hi.y - lo.y,
                       hi.z - lo.z, c, accent});
    if (solid) {
        L.solids.push_back({lo, hi});
    }
}

void addHouse(Level &L, Rng &r, float cx, float cz, float w, float h, float d, Color wall, Color roof,
              bool office = false) {
    const float base = groundAt(L.terrain, cx, cz);
    const float top = base + h;
    const float T = 0.15f;
    const float x0 = cx - w / 2, x1 = cx + w / 2, z0 = cz - d / 2, z1 = cz + d / 2;
    const float pz = cz + 0.5f;
    const Color trim{235, 230, 220, 255};
    const Color wood{150, 110, 70, 255};
    const Color sofa{70, 90, 130, 255};

    L.props.push_back({PropKind::House, Vector3{cx, base, cz}, w, h, d, wall, roof});

    auto slab = [&](float xa, float xb, float za, float zb, float ya, float yb, Color c) {
        addBox(L, PropKind::Wall, Vector3{xa, ya, za}, Vector3{xb, yb, zb}, c, c, true);
    };
    auto item = [&](PropKind k, float x, float z, float iw, float id, float ih, Color c, Color a) {
        addBox(L, k, Vector3{x - iw / 2, base, z - id / 2}, Vector3{x + iw / 2, base + ih, z + id / 2}, c, a, true);
    };

    slab(x0, cx - 0.8f, z0, z0 + T, base, top, wall);
    slab(cx + 0.8f, x1, z0, z0 + T, base, top, wall);
    slab(cx - 0.8f, cx + 0.8f, z0, z0 + T, base + 2.3f, top, wall);
    const Box shut{Vector3{cx - 0.8f, base, z0}, Vector3{cx + 0.8f, base + DoorHeight, z0 + T}};
    L.solids.push_back(shut);
    L.doors.push_back(Door{Vector3{cx - 0.8f, base, z0 + T / 2}, Vector3{cx, base, z0}, shut, L.solids.size() - 1});
    slab(x0, x1, z1 - T, z1, base, top, wall);
    slab(x0, x0 + T, z0, z1, base, top, wall);
    slab(x1 - T, x1, z0, z1, base, top, wall);

    slab(x0 + T, cx - 2.7f, pz - T / 2, pz + T / 2, base, top, trim);
    slab(cx - 1.3f, x1 - T, pz - T / 2, pz + T / 2, base, top, trim);
    if (!office) {
        const float xm = cx + w * 0.2f;
        slab(xm - T / 2, xm + T / 2, z0 + T, z0 + 1.2f, base, top, trim);
        slab(xm - T / 2, xm + T / 2, z0 + 2.6f, pz, base, top, trim);
    }
    slab(x0 + T, x1 - T, z0 + T, z1 - T, top - 0.2f, top, trim);
    const Color floorCol = office ? Color{185, 185, 180, 255} : Color{120, 78, 66, 255};
    const Color paper = office ? Color{200, 218, 230, 255} : Color{236, 206, 160, 255};
    addBox(L, PropKind::Flat, Vector3{x0 + T, base, z0 + T}, Vector3{x1 - T, base + 0.02f, z1 - T}, floorCol, floorCol, false);
    const float ph = top - 0.2f;
    addBox(L, PropKind::Flat, Vector3{x0 + T, base + 0.02f, z0 + T}, Vector3{x0 + T + 0.03f, ph, z1 - T}, paper, paper, false);
    addBox(L, PropKind::Flat, Vector3{x1 - T - 0.03f, base + 0.02f, z0 + T}, Vector3{x1 - T, ph, z1 - T}, paper, paper, false);
    addBox(L, PropKind::Flat, Vector3{x0 + T, base + 0.02f, z1 - T - 0.03f}, Vector3{x1 - T, ph, z1 - T}, paper, paper, false);
    addBox(L, PropKind::Flat, Vector3{x0 + T, base + 0.02f, z0 + T}, Vector3{cx - 0.8f, ph, z0 + T + 0.03f}, paper, paper, false);
    addBox(L, PropKind::Flat, Vector3{cx + 0.8f, base + 0.02f, z0 + T}, Vector3{x1 - T, ph, z0 + T + 0.03f}, paper, paper, false);

    const Color glass{120, 160, 190, 255};
    auto window = [&](float xa, float xb, float za, float zb) {
        addBox(L, PropKind::Window, Vector3{xa, base + 1.0f, za}, Vector3{xb, base + 2.3f, zb}, glass, glass, false);
    };
    for (float s : {-0.3f, 0.3f}) {
        const float px = cx + s * w;
        window(px - 0.6f, px + 0.6f, z0 - 0.06f, z0 + 0.06f);
        window(px - 0.6f, px + 0.6f, z1 - 0.06f, z1 + 0.06f);
    }
    for (float s : {-0.25f, 0.25f}) {
        const float wz = cz + s * d;
        window(x0 - 0.06f, x0 + 0.06f, wz - 0.6f, wz + 0.6f);
        window(x1 - 0.06f, x1 + 0.06f, wz - 0.6f, wz + 0.6f);
    }

    // Two-storey houses get an upper floor slab and its own row of windows.
    if (!office && h >= 5.5f) {
        const float fy = base + 3.0f;
        slab(x0 + T, x1 - T, z0 + T, z1 - T, fy, fy + 0.15f, trim);
        const float uy0 = base + 3.6f, uy1 = base + 4.9f;
        auto upper = [&](float xa, float xb, float za, float zb) {
            addBox(L, PropKind::Window, Vector3{xa, uy0, za}, Vector3{xb, uy1, zb}, glass, glass, false);
        };
        for (float s : {-0.3f, 0.3f}) {
            const float px = cx + s * w;
            upper(px - 0.6f, px + 0.6f, z0 - 0.06f, z0 + 0.06f);
            upper(px - 0.6f, px + 0.6f, z1 - 0.06f, z1 + 0.06f);
        }
        for (float s : {-0.25f, 0.25f}) {
            const float wz = cz + s * d;
            upper(x0 - 0.06f, x0 + 0.06f, wz - 0.6f, wz + 0.6f);
            upper(x1 - 0.06f, x1 + 0.06f, wz - 0.6f, wz + 0.6f);
        }
    }

    if (office) {
        item(PropKind::Shelf, cx, cz - 1.6f, 4.0f, 0.8f, 1.1f, wood, Color{110, 80, 50, 255});
        L.props.push_back({PropKind::Person, Vector3{cx, base, cz - 0.6f}, 0.5f, 1.8f, 0.5f,
                           Color{60, 90, 140, 255}, Color{230, 200, 170, 255}});
        item(PropKind::Couch, x0 + 1.6f, cz + 2.5f, 0.9f, 2.2f, 0.85f, sofa, Color{100, 120, 160, 255});
        item(PropKind::Couch, x1 - 1.6f, cz + 2.5f, 0.9f, 2.2f, 0.85f, sofa, Color{100, 120, 160, 255});
        L.props.push_back({PropKind::Plant, Vector3{x1 - 1.3f, base, z0 + 1.3f}, 0.5f, 1.3f, 0.5f,
                           Color{150, 90, 60, 255}, Color{40, 120, 50, 255}});
    } else {
        const float xm = cx + w * 0.2f;
        const float lx = (x0 + xm) / 2.0f, kx = (xm + x1) / 2.0f;
        const float bulbY = top - 0.5f;
        // Living room
        item(PropKind::Tv, x0 + 0.6f, cz - 1.2f, 0.3f, 1.4f, 1.0f, Color{20, 20, 22, 255}, Color{40, 70, 120, 255});
        item(PropKind::Couch, x0 + 3.2f, cz - 1.2f, 0.9f, 2.2f, 0.85f, sofa, Color{100, 120, 160, 255});
        L.props.push_back({PropKind::Plant, Vector3{x0 + 1.2f, base, cz - 0.1f}, 0.5f, 1.3f, 0.5f,
                           Color{150, 90, 60, 255}, Color{40, 120, 50, 255}});
        L.bulbs.push_back(Vector3{lx, bulbY, cz - 1.2f});
        // Kitchen
        item(PropKind::Fridge, x1 - 0.6f, z0 + 1.0f, 0.9f, 0.8f, 1.9f, Color{230, 230, 235, 255}, Color{120, 120, 130, 255});
        item(PropKind::Shelf, x1 - 0.5f, cz - 1.6f, 0.6f, 2.2f, 1.0f, wood, Color{90, 65, 40, 255});
        item(PropKind::Table, kx - 0.2f, cz - 0.4f, 1.2f, 0.8f, 0.8f, wood, Color{110, 80, 50, 255});
        L.bulbs.push_back(Vector3{kx, bulbY, cz - 0.4f});
        // Bedroom
        item(PropKind::Bed, x0 + 1.8f, cz + 3.0f, 1.6f, 2.2f, 0.6f, Color{70, 110, 160, 255}, Color{235, 235, 240, 255});
        item(PropKind::Shelf, x1 - 0.8f, cz + 3.6f, 0.6f, 1.2f, 2.0f, wood, Color{90, 65, 40, 255});
        L.bulbs.push_back(Vector3{cx, bulbY, cz + 3.0f});
    }

    if (r.next() < 0.5f) {
        L.ballSpawns.push_back(Vector3{cx - 0.4f, base + BallRadius, cz - 2.6f});
    }
}

void addBoundary(Level &L, Color) {
    const float h = L.terrain.half - 2.0f;
    const float low = -10.0f;
    const float high = 30.0f;
    L.solids.push_back({Vector3{-h, low, -h}, Vector3{h, high, -h + 1.0f}});
    L.solids.push_back({Vector3{-h, low, h - 1.0f}, Vector3{h, high, h}});
    L.solids.push_back({Vector3{-h, low, -h}, Vector3{-h + 1.0f, high, h}});
    L.solids.push_back({Vector3{h - 1.0f, low, -h}, Vector3{h, high, h}});
}

void addTree(Level &L, float x, float z, float h) {
    const float y = groundAt(L.terrain, x, z);
    L.props.push_back({PropKind::Tree, Vector3{x, y, z}, h * 0.3f, h, h * 0.3f, Color{40, 110, 45, 255},
                       Color{90, 60, 35, 255}});
    L.solids.push_back({Vector3{x - 0.3f, y, z - 0.3f}, Vector3{x + 0.3f, y + 2.0f, z + 0.3f}});
}

bool rectFree(const Level &L, const Rect &r, float margin) {
    for (const Field &f : L.terrain.fields) {
        if (overlaps(f.rect, r, margin)) {
            return false;
        }
    }
    for (const Plot &p : L.terrain.plots) {
        if (overlaps(p.rect, r, margin)) {
            return false;
        }
    }
    const float reach = (r.x1 - r.x0) / 2.0f + 8.0f;
    const float dx = (r.x0 + r.x1) / 2.0f - L.spawn.x;
    const float dz = (r.z0 + r.z1) / 2.0f - L.spawn.z;
    return dx * dx + dz * dz > reach * reach;
}

bool clearOf(const Level &L, float x, float z, float margin) {
    for (const Field &f : L.terrain.fields) {
        if (inRect(f.rect, x, z, margin)) {
            return false;
        }
    }
    for (const Plot &p : L.terrain.plots) {
        if (inRect(p.rect, x, z, margin)) {
            return false;
        }
    }
    const float dx = x - L.spawn.x, dz = z - L.spawn.z;
    return dx * dx + dz * dz > 64.0f;
}

void placeField(Level &L, Rng &r, float reach, float minSize, float maxSize) {
    for (int tries = 0; tries < 60; ++tries) {
        const float w = r.range(minSize, maxSize), d = r.range(minSize, maxSize);
        const float cx = r.range(-reach, reach), cz = r.range(-reach, reach);
        const Rect rect{cx - w / 2, cz - d / 2, cx + w / 2, cz + d / 2};
        if (!rectFree(L, rect, 2.0f)) {
            continue;
        }
        const CropPalette &c = pick(r, Crops);
        L.terrain.fields.push_back({rect, c.light, c.dark});
        return;
    }
}

void placePitch(Level &L, Rng &r, float reach) {
    constexpr float W = 28.0f, D = 16.0f;
    const Color grass{60, 140, 60, 255};
    const Color white{245, 245, 245, 255};
    for (int tries = 0; tries < 200; ++tries) {
        const float cx = r.range(-reach, reach), cz = r.range(-reach, reach);
        const Rect rect{cx - W / 2 - 3.0f, cz - D / 2 - 2.0f, cx + W / 2 + 3.0f, cz + D / 2 + 2.0f};
        if (!rectFree(L, rect, 2.0f)) {
            continue;
        }
        addPlot(L, cx, cz, W + 4.0f, D + 4.0f, 3.0f, grass);
        const float base = groundAt(L.terrain, cx, cz);
        L.pitches.push_back(Pitch{Vector3{cx, base, cz}, W, D});
        for (float sign : {-1.0f, 1.0f}) {
            const float edge = cx + sign * W / 2;
            const float back = edge + sign * 2.0f;
            for (float zc : {cz - 3.66f, cz + 3.66f}) {
                addBox(L, PropKind::Wall, Vector3{edge - 0.06f, base, zc - 0.06f},
                       Vector3{edge + 0.06f, base + 2.44f, zc + 0.06f}, white, white, true);
            }
            addBox(L, PropKind::Wall, Vector3{edge - 0.06f, base + 2.38f, cz - 3.66f},
                   Vector3{edge + 0.06f, base + 2.44f, cz + 3.66f}, white, white, true);
            addBox(L, PropKind::Net, Vector3{back - 0.05f, base, cz - 3.66f},
                   Vector3{back + 0.05f, base + 2.44f, cz + 3.66f}, white, white, true);
        }
        L.ballSpawns.push_back(Vector3{cx, base + BallRadius, cz});
        return;
    }
}

bool tryBuilding(Level &L, Rng &r, PropKind kind, float cx, float cz) {
    const bool barn = kind == PropKind::Barn;
    const bool silo = kind == PropKind::Silo;
    const bool house = kind == PropKind::House;
    const float w = silo ? r.range(4.0f, 5.0f) : barn ? r.range(11.0f, 13.0f) : house ? r.range(10.5f, 12.0f) : r.range(7.6f, 9.0f);
    const float d = silo ? w : barn ? r.range(8.0f, 10.0f) : house ? r.range(9.5f, 11.0f) : r.range(7.6f, 9.0f);
    const float h = silo ? r.range(11.0f, 13.0f) : barn ? r.range(5.5f, 6.5f) : house ? r.range(6.0f, 7.0f) : r.range(3.5f, 4.5f);
    const Rect footprint{cx - w / 2 - 1.0f, cz - d / 2 - 1.0f, cx + w / 2 + 1.0f, cz + d / 2 + 1.0f};
    if (!rectFree(L, footprint, 2.0f)) {
        return false;
    }
    addPlot(L, cx, cz, w + 2.0f, d + 2.0f, 4.0f);
    const Color wall = barn ? Color{170, 50, 40, 255} : silo ? Color{215, 215, 220, 255} : pick(r, WallColors);
    const Color roof = barn ? Color{80, 80, 85, 255} : silo ? Color{120, 120, 130, 255} : pick(r, RoofColors);
    if (kind == PropKind::House) {
        addHouse(L, r, cx, cz, w, h, d, wall, roof);
    } else {
        addProp(L, kind, cx, cz, w, h, d, wall, roof, true);
    }
    return true;
}

void scatterBuildings(Level &L, Rng &r, int count, float reach) {
    int placed = 0;
    for (int tries = 0; tries < count * 30 && placed < count; ++tries) {
        const float x = r.range(-reach, reach);
        const float z = r.range(-reach, reach);
        const float roll = r.next();
        const PropKind kind = roll < 0.7f ? PropKind::House : roll < 0.9f ? PropKind::Barn : PropKind::Silo;
        if (tryBuilding(L, r, kind, x, z)) {
            ++placed;
        }
    }
}

void scatterTrees(Level &L, Rng &r, int count, float reach) {
    int placed = 0;
    for (int tries = 0; tries < count * 20 && placed < count; ++tries) {
        const float x = r.range(-reach, reach);
        const float z = r.range(-reach, reach);
        if (!clearOf(L, x, z, 4.0f)) {
            continue;
        }
        addTree(L, x, z, r.range(6.0f, 10.0f));
        ++placed;
    }
}

void scatterRocks(Level &L, Rng &r, int count, float reach) {
    int placed = 0;
    for (int tries = 0; tries < count * 20 && placed < count; ++tries) {
        const float x = r.range(-reach, reach);
        const float z = r.range(-reach, reach);
        if (!clearOf(L, x, z, 3.0f)) {
            continue;
        }
        const float s = r.range(1.5f, 3.5f);
        addProp(L, PropKind::Crate, x, z, s, s * 0.7f, s, Color{120, 115, 110, 255}, Color{90, 88, 84, 255}, true);
        ++placed;
    }
}

} // namespace

float groundAt(const Terrain &t, float x, float z) {
    float h = rawHeight(t, x, z);
    for (const Plot &p : t.plots) {
        const float dx = std::max({p.rect.x0 - x, 0.0f, x - p.rect.x1});
        const float dz = std::max({p.rect.z0 - z, 0.0f, z - p.rect.z1});
        const float d = std::sqrt(dx * dx + dz * dz);
        if (d >= p.blend) {
            continue;
        }
        float k = 1.0f - d / p.blend;
        k = k * k * (3.0f - 2.0f * k);
        h += (p.h - h) * k;
    }
    return h;
}

Vector3 sunDirection() {
    return Vector3Normalize(Vector3{0.6f, 0.5f, 0.4f});
}

void finishLevel(Level &L) {
    L.model = LoadModelFromMesh(buildTerrainMesh(L.terrain, TerrainCells));
    L.modelLoaded = true;
}

void buildClassic(Level &L) {
    const int rowN = static_cast<int>(std::size(ClassicRows));
    int cols = 0;
    for (int r = 0; r < rowN; ++r) {
        cols = std::max(cols, static_cast<int>(std::strlen(ClassicRows[r])));
    }
    L.terrain.half = static_cast<float>(std::max(cols, rowN)) + 4.0f;
    L.terrain.grass = Color{55, 70, 55, 255};
    L.terrain.dirt = Color{55, 70, 55, 255};
    L.sky = Color{135, 190, 235, 255};

    for (int r = 0; r < rowN; ++r) {
        for (int c = 0; c < cols; ++c) {
            const int len = static_cast<int>(std::strlen(ClassicRows[r]));
            const char ch = c < len ? ClassicRows[r][c] : '#';
            const float cx = (c - cols / 2.0f + 0.5f) * CellSize;
            const float cz = (r - rowN / 2.0f + 0.5f) * CellSize;
            if (ch == '#') {
                addProp(L, PropKind::Crate, cx, cz, CellSize, 3.0f, CellSize, Color{125, 118, 108, 255},
                        Color{90, 85, 78, 255}, true);
            } else if (ch == 'S') {
                L.spawn = Vector3{cx, 0.0f, cz};
            }
        }
    }
}

void buildValley(Level &L) {
    Terrain &t = L.terrain;
    t.half = 90.0f;
    t.amp = 3.5f;
    t.seed = 7.0f;
    t.grass = Color{70, 125, 60, 255};
    t.dirt = Color{125, 100, 70, 255};
    L.sky = Color{150, 200, 240, 255};
    Rng r{101u};
    for (int i = 0; i < 8; ++i) {
        t.hills.push_back({r.range(-80, 80), r.range(-80, 80), r.range(12, 22), r.range(4, 9)});
    }
    for (int i = 0; i < 5; ++i) {
        t.dents.push_back({r.range(-70, 70), r.range(-70, 70), r.range(6, 10), r.range(2, 4)});
    }
    L.spawn = Vector3{-70.0f, 0.0f, -70.0f};
    addBoundary(L, Color{120, 100, 80, 255});
    for (int i = 0; i < 6; ++i) {
        placeField(L, r, t.half - 14.0f, 12.0f, 22.0f);
    }
    scatterBuildings(L, r, 6, t.half - 14.0f);
    scatterTrees(L, r, 110, t.half - 8.0f);
    scatterRocks(L, r, 30, t.half - 8.0f);
}

void buildFarm(Level &L) {
    Terrain &t = L.terrain;
    Rng r{202u};
    t.half = 90.0f;
    t.amp = 1.2f;
    t.seed = 3.0f;
    t.grass = Color{85, 130, 60, 255};
    t.dirt = Color{120, 95, 60, 255};
    L.sky = Color{160, 205, 245, 255};
    t.hills.push_back({-40.0f, 55.0f, 30.0f, 2.5f});
    t.hills.push_back({65.0f, -55.0f, 35.0f, 3.0f});

    const Color corn1{70, 140, 50, 255}, corn2{55, 115, 40, 255};
    const Color wheat1{205, 180, 80, 255}, wheat2{185, 160, 60, 255};
    t.fields = {
        {{-70, -55, -50, -40}, corn1, corn2},
        {{-46, -55, -26, -40}, wheat1, wheat2},
        {{-70, -36, -50, -21}, wheat1, wheat2},
        {{-46, -36, -26, -21}, corn1, corn2},
    };

    addPlot(L, 20.0f, -30.0f, 12.0f, 10.0f, 6.0f);
    addPlot(L, 40.0f, 10.0f, 20.0f, 12.0f, 6.0f);
    addPlot(L, 26.0f, -4.0f, 6.0f, 6.0f, 6.0f);

    L.spawn = Vector3{-10.0f, 0.0f, 30.0f};
    addHouse(L, r, 20.0f, -30.0f, 12.0f, 5.0f, 10.0f, Color{210, 200, 180, 255}, Color{120, 50, 40, 255});
    addProp(L, PropKind::Barn, 40.0f, 10.0f, 20.0f, 7.0f, 12.0f, Color{170, 50, 40, 255},
            Color{80, 80, 85, 255}, true);
    addProp(L, PropKind::Silo, 26.0f, -4.0f, 6.0f, 14.0f, 6.0f, Color{215, 215, 220, 255},
            Color{120, 120, 130, 255}, true);

    const Color bale{225, 195, 90, 255};
    const float bales[][2] = {{-10, -10}, {-8, -10}, {-9, -12}, {0, -14}, {3, -14}, {-20, 20}};
    for (const auto &b : bales) {
        addProp(L, PropKind::Crate, b[0], b[1], 2.0f, 1.2f, 1.2f, bale, bale, true);
    }

    addBoundary(L, Color{110, 80, 50, 255});
    for (int i = 0; i < 10; ++i) {
        placeField(L, r, t.half - 12.0f, 10.0f, 20.0f);
    }
    scatterBuildings(L, r, 3, t.half - 14.0f);
    scatterTrees(L, r, 40, t.half - 8.0f);
}

bool nearSolid(const Level &L, float x, float z, float margin) {
    for (const Box &b : L.solids) {
        if (x > b.min.x - margin && x < b.max.x + margin && z > b.min.z - margin && z < b.max.z + margin) {
            return true;
        }
    }
    return false;
}

void placeLamp(Level &L, float x, float z) {
    if (!nearSolid(L, x, z, 1.0f)) {
        L.lamps.push_back(Vector3{x, groundAt(L.terrain, x, z), z});
    }
}

void addLamps(Level &L, float reach) {
    for (float v = -reach; v <= reach; v += 15.0f) {
        for (float side : {-4.5f, 4.5f}) {
            placeLamp(L, v, side);
            placeLamp(L, side, v);
        }
    }
}

void buildVillage(Level &L) {
    Terrain &t = L.terrain;
    t.half = 75.0f;
    t.amp = 0.8f;
    t.seed = 5.0f;
    t.grass = Color{95, 140, 65, 255};
    t.dirt = Color{130, 105, 75, 255};
    L.sky = Color{170, 210, 245, 255};
    L.spawn = Vector3{-62.0f, 0.0f, -62.0f};
    Rng r{404u};

    addPlot(L, 0.0f, 0.0f, 2.0f * t.half - 10.0f, 6.0f, 3.0f);
    addPlot(L, 0.0f, 0.0f, 6.0f, 2.0f * t.half - 10.0f, 3.0f);

    const float lanes[] = {-34.0f, -12.0f, 12.0f, 34.0f};
    for (float cx : lanes) {
        for (float cz : lanes) {
            if (r.next() < 0.75f) {
                tryBuilding(L, r, r.next() < 0.8f ? PropKind::House : PropKind::Barn, cx, cz);
            }
        }
    }
    scatterBuildings(L, r, 4, t.half - 14.0f);
    placePitch(L, r, t.half - 22.0f);
    addLamps(L, t.half - 12.0f);

    addBoundary(L, Color{120, 100, 80, 255});
    for (int i = 0; i < 14; ++i) {
        placeField(L, r, t.half - 14.0f, 10.0f, 18.0f);
    }
    scatterTrees(L, r, 50, t.half - 8.0f);
    scatterRocks(L, r, 10, t.half - 8.0f);
}

void buildQuarry(Level &L) {
    Terrain &t = L.terrain;
    t.half = 90.0f;
    t.amp = 2.0f;
    t.seed = 11.0f;
    t.grass = Color{110, 120, 80, 255};
    t.dirt = Color{140, 120, 100, 255};
    L.sky = Color{170, 160, 150, 255};
    t.dents.push_back({0.0f, 0.0f, 28.0f, 14.0f});
    Rng r{303u};
    for (int i = 0; i < 8; ++i) {
        const float a = i * 2.0f * Pi / 8.0f;
        t.hills.push_back({55.0f * std::cos(a), 55.0f * std::sin(a), r.range(14, 20), r.range(12, 16)});
    }
    L.spawn = Vector3{-82.0f, 0.0f, 0.0f};
    addBoundary(L, Color{120, 110, 100, 255});
    scatterRocks(L, r, 40, t.half - 8.0f);
    scatterTrees(L, r, 20, t.half - 8.0f);
}

void buildDesert(Level &L) {
    Terrain &t = L.terrain;
    Rng r{505u};
    t.half = 90.0f;
    t.amp = 4.0f;
    t.seed = 17.0f;
    t.grass = Color{222, 190, 120, 255};
    t.dirt = Color{210, 175, 110, 255};
    L.sky = Color{245, 215, 160, 255};
    for (int i = 0; i < 12; ++i) {
        t.hills.push_back({r.range(-80, 80), r.range(-80, 80), r.range(10, 18), r.range(3, 7)});
    }
    L.spawn = Vector3{-70.0f, 0.0f, 70.0f};
    addBoundary(L, Color{180, 140, 90, 255});
    scatterBuildings(L, r, 4, t.half - 14.0f);
    int placed = 0;
    for (int tries = 0; placed < 60 && tries < 1200; ++tries) {
        const float x = r.range(-t.half + 8.0f, t.half - 8.0f);
        const float z = r.range(-t.half + 8.0f, t.half - 8.0f);
        if (!clearOf(L, x, z, 4.0f)) {
            continue;
        }
        addProp(L, PropKind::Crate, x, z, 0.8f, r.range(2.5f, 4.5f), 0.8f, Color{80, 130, 70, 255},
                Color{60, 105, 55, 255}, true);
        ++placed;
    }
    scatterRocks(L, r, 25, t.half - 8.0f);
}

void buildSnowfield(Level &L) {
    Terrain &t = L.terrain;
    Rng r{606u};
    t.half = 90.0f;
    t.amp = 2.5f;
    t.seed = 23.0f;
    t.grass = Color{235, 240, 245, 255};
    t.dirt = Color{215, 225, 235, 255};
    L.sky = Color{190, 215, 235, 255};
    for (int i = 0; i < 10; ++i) {
        t.hills.push_back({r.range(-80, 80), r.range(-80, 80), r.range(12, 20), r.range(4, 8)});
    }
    L.spawn = Vector3{-70.0f, 0.0f, -70.0f};
    addBoundary(L, Color{120, 110, 100, 255});
    scatterBuildings(L, r, 5, t.half - 14.0f);
    scatterTrees(L, r, 60, t.half - 8.0f);
    scatterRocks(L, r, 20, t.half - 8.0f);
}

void buildForest(Level &L) {
    Terrain &t = L.terrain;
    Rng r{707u};
    t.half = 90.0f;
    t.amp = 3.0f;
    t.seed = 31.0f;
    t.grass = Color{45, 85, 45, 255};
    t.dirt = Color{80, 65, 45, 255};
    L.sky = Color{120, 165, 150, 255};
    for (int i = 0; i < 14; ++i) {
        t.hills.push_back({r.range(-80, 80), r.range(-80, 80), r.range(8, 16), r.range(3, 6)});
    }
    L.spawn = Vector3{-70.0f, 0.0f, -70.0f};
    addBoundary(L, Color{70, 55, 40, 255});
    scatterTrees(L, r, 180, t.half - 8.0f);
    scatterRocks(L, r, 15, t.half - 8.0f);
}

template <size_t N>
bool hasSpot(const float (&spots)[N][2], float x, float z) {
    for (const auto &s : spots) {
        if (s[0] == x && s[1] == z) {
            return true;
        }
    }
    return false;
}

bool nearIntersection(float s) {
    return std::fabs(s - std::round(s / RoadSpacing) * RoadSpacing) < 9.0f;
}

void addBuilding(Level &L, Rng &r, float cx, float cz, float w, float h, float d, Color wall, Color roof,
                 bool office) {
    addPlot(L, cx, cz, w + 2.0f, d + 2.0f, 4.0f);
    addHouse(L, r, cx, cz, w, h, d, wall, roof, office);
}

void buildCity(Level &L) {
    constexpr int Grid = 4;
    const float BankSpots[][2] = {{-20.0f, -20.0f}, {60.0f, -60.0f}, {-100.0f, 100.0f}};
    const float PoliceSpots[][2] = {{20.0f, 20.0f}, {-60.0f, 60.0f}};
    const Color CarPalette[] = {Color{180, 40, 40, 255}, Color{40, 90, 180, 255}, Color{230, 230, 230, 255},
                                Color{30, 30, 35, 255}, Color{200, 160, 40, 255}, Color{60, 140, 80, 255}};
    const Color PoliceBlue{30, 50, 120, 255};
    const Color asphalt{55, 55, 60, 255};
    const Color paint{230, 230, 220, 255};

    Terrain &t = L.terrain;
    Rng r{808u};
    t.half = RoadSpan + 5.0f;
    t.amp = 0.4f;
    t.seed = 2.0f;
    t.grass = Color{90, 90, 95, 255};
    t.dirt = Color{75, 75, 80, 255};
    L.sky = Color{110, 125, 150, 255};
    L.spawn = Vector3{-Grid * RoadSpacing, 0.0f, -Grid * RoadSpacing};
    addBoundary(L, Color{80, 80, 90, 255});

    for (int k = -Grid; k <= Grid; ++k) {
        const float p = k * RoadSpacing;
        addPlot(L, p, 0.0f, 10.0f, 2.0f * RoadSpan, 0.5f, asphalt);
        addPlot(L, 0.0f, p, 2.0f * RoadSpan, 10.0f, 0.5f, asphalt);
    }
    for (int k = -Grid; k <= Grid; ++k) {
        const float p = k * RoadSpacing;
        for (float s = -RoadSpan; s < RoadSpan; s += 12.0f) {
            if (nearIntersection(s)) {
                continue;
            }
            const float yv = groundAt(t, p, s);
            addBox(L, PropKind::Flat, Vector3{p - 0.12f, yv, s}, Vector3{p + 0.12f, yv + 0.03f, s + 5.0f}, paint,
                   paint, false);
            const float yh = groundAt(t, s, p);
            addBox(L, PropKind::Flat, Vector3{s, yh, p - 0.12f}, Vector3{s + 5.0f, yh + 0.03f, p + 0.12f}, paint,
                   paint, false);
        }
    }
    for (int k = -Grid; k <= Grid; ++k) {
        const float p = k * RoadSpacing;
        for (float s = -RoadSpan; s <= RoadSpan; s += 20.0f) {
            if (nearIntersection(s)) {
                continue;
            }
            for (float side : {-6.0f, 6.0f}) {
                placeLamp(L, p + side, s);
                placeLamp(L, s, p + side);
            }
        }
    }
    for (int i = -Grid; i <= Grid; ++i) {
        for (int j = -Grid; j <= Grid; ++j) {
            const float px = i * RoadSpacing, pz = j * RoadSpacing;
            L.signals.push_back(Vector3{px + 6.5f, groundAt(t, px + 6.5f, pz + 6.5f), pz + 6.5f});
        }
    }

    for (int i = -Grid; i < Grid; ++i) {
        for (int j = -Grid; j < Grid; ++j) {
            const float cx = i * RoadSpacing + RoadSpacing / 2, cz = j * RoadSpacing + RoadSpacing / 2;
            if (hasSpot(BankSpots, cx, cz)) {
                addBuilding(L, r, cx, cz, 12.0f, 5.0f, 10.0f, Color{205, 195, 160, 255}, Color{70, 60, 50, 255}, true);
            } else if (hasSpot(PoliceSpots, cx, cz)) {
                addBuilding(L, r, cx, cz, 12.0f, 5.0f, 10.0f, Color{70, 95, 140, 255}, Color{40, 45, 60, 255}, true);
                L.cars.push_back(Car{cx, cz - RoadSpacing / 2 + 2.5f, 1.0f, 0.0f, true, true, PoliceBlue});
            } else {
                const float roll = r.next();
                if (roll < 0.3f) {
                    addBuilding(L, r, cx - 5.0f, cz, 8.0f, 4.0f, 8.0f, pick(r, WallColors), pick(r, RoofColors), false);
                    addBuilding(L, r, cx + 5.0f, cz, 8.0f, 4.0f, 8.0f, pick(r, WallColors), pick(r, RoofColors), false);
                } else if (roll < 0.6f) {
                    for (float sx : {-9.0f, 9.0f}) {
                        for (float sz : {-9.0f, 9.0f}) {
                            addBuilding(L, r, cx + sx, cz + sz, 8.0f, 4.0f, 8.0f, pick(r, WallColors), pick(r, RoofColors), false);
                        }
                    }
                } else if (roll < 0.85f) {
                    addBuilding(L, r, cx, cz, 12.0f, 5.0f, 10.0f, pick(r, WallColors), pick(r, RoofColors), false);
                } else {
                    addPlot(L, cx, cz, 12.0f, 12.0f, 4.0f);
                    addProp(L, PropKind::Wall, cx, cz, 10.0f, r.range(14.0f, 26.0f), 10.0f, pick(r, WallColors),
                            Color{60, 70, 85, 255}, true);
                }
            }
        }
    }

    for (int k = -Grid; k <= Grid; ++k) {
        const float p = k * RoadSpacing;
        for (float lane : {-2.5f, 2.5f}) {
            L.cars.push_back(Car{r.range(-RoadSpan, RoadSpan), p + lane, lane > 0.0f ? 1.0f : -1.0f,
                                 r.range(8.0f, 14.0f), false, false, pick(r, CarPalette)});
            L.cars.push_back(Car{r.range(-RoadSpan, RoadSpan), p + lane, lane > 0.0f ? -1.0f : 1.0f,
                                 r.range(8.0f, 14.0f), true, false, pick(r, CarPalette)});
        }
    }
    L.cars.push_back(Car{0.0f, 2.5f, 1.0f, 10.0f, false, true, PoliceBlue});
    L.cars.push_back(Car{60.0f, -2.5f, -1.0f, 10.0f, true, true, PoliceBlue});
}

void buildSuburb(Level &L) {
    Terrain &t = L.terrain;
    Rng r{1010u};
    t.half = 90.0f;
    t.amp = 0.8f;
    t.seed = 13.0f;
    t.grass = Color{100, 150, 75, 255};
    t.dirt = Color{140, 115, 85, 255};
    L.sky = Color{160, 210, 250, 255};
    L.spawn = Vector3{-70.0f, 0.0f, -70.0f};
    const Color asphalt{70, 70, 75, 255};
    for (float p : {-30.0f, 30.0f}) {
        addPlot(L, p, 0.0f, 8.0f, 2.0f * t.half - 20.0f, 2.0f, asphalt);
        addPlot(L, 0.0f, p, 2.0f * t.half - 20.0f, 8.0f, 2.0f, asphalt);
    }
    const float lanes[] = {-60.0f, -45.0f, -15.0f, 0.0f, 15.0f, 45.0f, 60.0f};
    for (float cx : lanes) {
        for (float cz : lanes) {
            if (r.next() < 0.55f) {
                tryBuilding(L, r, PropKind::House, cx, cz);
            }
        }
    }
    placePitch(L, r, t.half - 22.0f);
    for (int i = 0; i < 3; ++i) {
        placeField(L, r, t.half - 14.0f, 10.0f, 18.0f);
    }
    addLamps(L, t.half - 12.0f);
    addBoundary(L, Color{110, 95, 80, 255});
    scatterTrees(L, r, 90, t.half - 8.0f);
}

void buildIndustrial(Level &L) {
    Terrain &t = L.terrain;
    Rng r{1111u};
    t.half = 90.0f;
    t.amp = 0.6f;
    t.seed = 29.0f;
    t.grass = Color{105, 105, 100, 255};
    t.dirt = Color{90, 88, 84, 255};
    L.sky = Color{200, 150, 110, 255};
    L.spawn = Vector3{-70.0f, 0.0f, 70.0f};
    const Color concrete{80, 80, 84, 255};
    addPlot(L, 0.0f, 0.0f, 2.0f * t.half - 20.0f, 12.0f, 2.0f, concrete);
    addPlot(L, 0.0f, 0.0f, 12.0f, 2.0f * t.half - 20.0f, 2.0f, concrete);
    for (float cx : {-55.0f, -25.0f, 25.0f, 55.0f}) {
        for (float cz : {-55.0f, -25.0f, 25.0f, 55.0f}) {
            if (r.next() < 0.6f) {
                tryBuilding(L, r, PropKind::Barn, cx, cz);
            } else {
                tryBuilding(L, r, PropKind::Silo, cx, cz);
            }
        }
    }
    const Color boxes[] = {Color{180, 60, 40, 255}, Color{40, 90, 140, 255}, Color{210, 170, 40, 255},
                           Color{80, 120, 80, 255}};
    int placed = 0;
    for (int tries = 0; placed < 18 && tries < 400; ++tries) {
        const float x = r.range(-t.half + 10.0f, t.half - 10.0f);
        const float z = r.range(-t.half + 10.0f, t.half - 10.0f);
        if (!clearOf(L, x, z, 6.0f)) {
            continue;
        }
        const bool alongX = r.next() < 0.5f;
        addProp(L, PropKind::Crate, x, z, alongX ? 6.0f : 2.4f, 2.6f, alongX ? 2.4f : 6.0f, pick(r, boxes),
                Color{60, 60, 60, 255}, true);
        ++placed;
    }
    for (float z : {-40.0f, 40.0f}) {
        addProp(L, PropKind::Crate, 0.0f, z, 14.0f, 1.0f, 0.8f, Color{150, 150, 155, 255}, Color{110, 110, 115, 255}, true);
    }
    for (float cx : {-10.0f, 10.0f}) {
        addProp(L, PropKind::Wall, cx, -60.0f, 2.0f, 18.0f, 2.0f, Color{200, 150, 40, 255}, Color{60, 60, 60, 255}, true);
    }
    addBoundary(L, Color{150, 150, 150, 255});
    addLamps(L, t.half - 12.0f);
    scatterRocks(L, r, 12, t.half - 8.0f);
}

void buildCanyon(Level &L) {
    Terrain &t = L.terrain;
    Rng r{909u};
    t.half = 90.0f;
    t.amp = 1.5f;
    t.seed = 41.0f;
    t.grass = Color{150, 95, 60, 255};
    t.dirt = Color{130, 80, 50, 255};
    L.sky = Color{235, 170, 120, 255};
    for (float x = -80.0f; x <= 80.0f; x += 8.0f) {
        t.dents.push_back({x, r.range(-3.0f, 3.0f), 7.0f, 9.0f});
    }
    for (float x = -80.0f; x <= 80.0f; x += 10.0f) {
        t.hills.push_back({x, -30.0f, 12.0f, 14.0f});
        t.hills.push_back({x, 30.0f, 12.0f, 14.0f});
    }
    L.spawn = Vector3{-80.0f, 0.0f, 0.0f};
    addBoundary(L, Color{120, 70, 40, 255});
    scatterRocks(L, r, 40, t.half - 8.0f);
    scatterTrees(L, r, 12, t.half - 8.0f);
}

void buildRuins(Level &L) {
    Terrain &t = L.terrain;
    Rng r{1212u};
    t.half = 90.0f;
    t.amp = 2.2f;
    t.seed = 37.0f;
    t.grass = Color{115, 120, 85, 255};
    t.dirt = Color{120, 105, 85, 255};
    L.sky = Color{185, 180, 165, 255};
    for (int i = 0; i < 9; ++i) {
        t.dents.push_back({r.range(-75, 75), r.range(-75, 75), r.range(5, 9), r.range(2, 4)});
    }
    L.spawn = Vector3{-70.0f, 0.0f, 0.0f};
    addBoundary(L, Color{100, 90, 80, 255});
    scatterBuildings(L, r, 6, t.half - 14.0f);
    scatterRocks(L, r, 35, t.half - 8.0f);
    scatterTrees(L, r, 15, t.half - 8.0f);
}

void buildMesa(Level &L) {
    Terrain &t = L.terrain;
    Rng r{1313u};
    t.half = 90.0f;
    t.amp = 1.0f;
    t.seed = 43.0f;
    t.grass = Color{170, 110, 70, 255};
    t.dirt = Color{150, 90, 60, 255};
    L.sky = Color{250, 200, 150, 255};
    t.hills.push_back({0.0f, 0.0f, 26.0f, 16.0f});
    for (int i = 0; i < 6; ++i) {
        t.hills.push_back({r.range(-70, 70), r.range(-70, 70), r.range(6, 10), r.range(10, 14)});
    }
    L.spawn = Vector3{-80.0f, 0.0f, 0.0f};
    addBoundary(L, Color{140, 80, 50, 255});
    scatterRocks(L, r, 30, t.half - 8.0f);
    scatterTrees(L, r, 8, t.half - 8.0f);
}

void buildOrchard(Level &L) {
    Terrain &t = L.terrain;
    Rng r{1414u};
    t.half = 90.0f;
    t.amp = 0.7f;
    t.seed = 47.0f;
    t.grass = Color{90, 150, 70, 255};
    t.dirt = Color{115, 90, 60, 255};
    L.sky = Color{170, 220, 250, 255};
    L.spawn = Vector3{-75.0f, 0.0f, 75.0f};
    addBuilding(L, r, 40.0f, 40.0f, 12.0f, 5.0f, 10.0f, Color{220, 205, 170, 255}, Color{130, 60, 45, 255}, false);
    for (float x = -64.0f; x <= 64.0f; x += 8.0f) {
        for (float z = -64.0f; z <= 64.0f; z += 8.0f) {
            if (std::fabs(x - 40.0f) < 12.0f && std::fabs(z - 40.0f) < 12.0f) {
                continue;
            }
            if (r.next() < 0.8f) {
                addTree(L, x + r.range(-1.0f, 1.0f), z + r.range(-1.0f, 1.0f), r.range(4.0f, 6.0f));
            }
        }
    }
    addBoundary(L, Color{110, 80, 50, 255});
}

void buildContainers(Level &L) {
    Terrain &t = L.terrain;
    Rng r{1515u};
    t.half = 90.0f;
    t.amp = 0.5f;
    t.seed = 53.0f;
    t.grass = Color{120, 115, 105, 255};
    t.dirt = Color{100, 95, 90, 255};
    L.sky = Color{120, 150, 185, 255};
    L.spawn = Vector3{-80.0f, 0.0f, -80.0f};
    const Color boxes[] = {Color{180, 60, 40, 255}, Color{40, 90, 140, 255}, Color{210, 170, 40, 255},
                           Color{80, 120, 80, 255}};
    int placed = 0;
    for (int tries = 0; placed < 90 && tries < 2000; ++tries) {
        const float x = r.range(-t.half + 10.0f, t.half - 10.0f);
        const float z = r.range(-t.half + 10.0f, t.half - 10.0f);
        if (!clearOf(L, x, z, 4.0f)) {
            continue;
        }
        const bool alongX = r.next() < 0.5f;
        addProp(L, PropKind::Crate, x, z, alongX ? 6.0f : 2.4f, 2.6f, alongX ? 2.4f : 6.0f, pick(r, boxes),
                Color{60, 60, 60, 255}, true);
        ++placed;
    }
    addBoundary(L, Color{150, 150, 150, 255});
    addLamps(L, t.half - 12.0f);
    scatterRocks(L, r, 8, t.half - 8.0f);
}

void buildPlaza(Level &L) {
    Terrain &t = L.terrain;
    Rng r{1616u};
    t.half = 80.0f;
    t.amp = 0.3f;
    t.seed = 59.0f;
    t.grass = Color{120, 160, 110, 255};
    t.dirt = Color{150, 140, 125, 255};
    L.sky = Color{140, 200, 240, 255};
    L.spawn = Vector3{-70.0f, 0.0f, 0.0f};
    addPlot(L, 0.0f, 0.0f, 40.0f, 40.0f, 2.0f, Color{170, 165, 155, 255});
    for (float cx : {-50.0f, 50.0f}) {
        for (float cz : {-50.0f, 50.0f}) {
            tryBuilding(L, r, PropKind::House, cx, cz);
        }
    }
    for (float x : {-12.0f, 12.0f}) {
        for (float z : {-12.0f, 12.0f}) {
            addProp(L, PropKind::Wall, x, z, 2.0f, 4.0f, 2.0f, Color{200, 195, 185, 255}, Color{120, 115, 105, 255},
                    true);
        }
    }
    addBoundary(L, Color{130, 125, 115, 255});
    addLamps(L, t.half - 12.0f);
    scatterTrees(L, r, 30, t.half - 8.0f);
}

void buildJungle(Level &L) {
    Terrain &t = L.terrain;
    Rng r{1717u};
    t.half = 90.0f;
    t.amp = 2.0f;
    t.seed = 61.0f;
    t.grass = Color{35, 75, 35, 255};
    t.dirt = Color{60, 50, 30, 255};
    L.sky = Color{110, 160, 130, 255};
    for (int i = 0; i < 16; ++i) {
        t.hills.push_back({r.range(-80, 80), r.range(-80, 80), r.range(8, 14), r.range(3, 6)});
    }
    for (int i = 0; i < 6; ++i) {
        t.dents.push_back({r.range(-70, 70), r.range(-70, 70), r.range(5, 8), r.range(1.5f, 3.0f)});
    }
    L.spawn = Vector3{-75.0f, 0.0f, 75.0f};
    addBoundary(L, Color{60, 45, 30, 255});
    scatterTrees(L, r, 220, t.half - 8.0f);
    scatterRocks(L, r, 25, t.half - 8.0f);
    scatterBuildings(L, r, 2, t.half - 14.0f);
}

void buildHarbor(Level &L) {
    Terrain &t = L.terrain;
    Rng r{1818u};
    t.half = 90.0f;
    t.amp = 0.3f;
    t.seed = 67.0f;
    t.grass = Color{95, 110, 120, 255};
    t.dirt = Color{80, 90, 100, 255};
    L.sky = Color{150, 185, 210, 255};
    L.spawn = Vector3{-75.0f, 0.0f, 0.0f};
    addPlot(L, 10.0f, 0.0f, 70.0f, 10.0f, 1.0f, Color{120, 85, 50, 255});
    addBuilding(L, r, -40.0f, -35.0f, 16.0f, 6.0f, 12.0f, Color{170, 160, 150, 255}, Color{90, 70, 60, 255}, false);
    addBuilding(L, r, -40.0f, 35.0f, 16.0f, 6.0f, 12.0f, Color{150, 165, 175, 255}, Color{70, 80, 90, 255}, false);
    addBuilding(L, r, 20.0f, -35.0f, 14.0f, 6.0f, 10.0f, Color{185, 120, 80, 255}, Color{60, 55, 50, 255}, false);
    addBuilding(L, r, 20.0f, 35.0f, 14.0f, 6.0f, 10.0f, Color{180, 175, 160, 255}, Color{80, 60, 50, 255}, false);
    const Color boxes[] = {Color{180, 60, 40, 255}, Color{40, 90, 140, 255}, Color{210, 170, 40, 255},
                           Color{80, 120, 80, 255}};
    int placed = 0;
    for (int tries = 0; placed < 60 && tries < 1500; ++tries) {
        const float x = r.range(-t.half + 10.0f, t.half - 10.0f);
        const float z = r.range(-t.half + 10.0f, t.half - 10.0f);
        if (!clearOf(L, x, z, 4.0f)) {
            continue;
        }
        const bool alongX = r.next() < 0.5f;
        addProp(L, PropKind::Crate, x, z, alongX ? 6.0f : 2.4f, 2.6f, alongX ? 2.4f : 6.0f, pick(r, boxes),
                Color{60, 60, 60, 255}, true);
        ++placed;
    }
    addBoundary(L, Color{70, 80, 90, 255});
    addLamps(L, t.half - 12.0f);
    scatterRocks(L, r, 6, t.half - 8.0f);
}

void buildCompound(Level &L) {
    Terrain &t = L.terrain;
    Rng r{1919u};
    t.half = 90.0f;
    t.amp = 0.4f;
    t.seed = 71.0f;
    t.grass = Color{130, 125, 105, 255};
    t.dirt = Color{110, 100, 85, 255};
    L.sky = Color{200, 185, 160, 255};
    L.spawn = Vector3{-75.0f, 0.0f, 75.0f};
    const Color wallC{170, 160, 140, 255};
    const Color wallA{100, 95, 85, 255};
    addPlot(L, 0.0f, 0.0f, 56.0f, 56.0f, 2.0f, Color{150, 140, 120, 255});
    // Perimeter walls with a gap in the middle of each side.
    for (float s = -28.0f; s <= 28.0f; s += 4.0f) {
        if (std::fabs(s) < 6.0f) {
            continue;
        }
        addProp(L, PropKind::Wall, s, -28.0f, 4.0f, 3.0f, 1.0f, wallC, wallA, true);
        addProp(L, PropKind::Wall, s, 28.0f, 4.0f, 3.0f, 1.0f, wallC, wallA, true);
        addProp(L, PropKind::Wall, -28.0f, s, 1.0f, 3.0f, 4.0f, wallC, wallA, true);
        addProp(L, PropKind::Wall, 28.0f, s, 1.0f, 3.0f, 4.0f, wallC, wallA, true);
    }
    for (float cx : {-26.0f, 26.0f}) {
        for (float cz : {-26.0f, 26.0f}) {
            addProp(L, PropKind::Wall, cx, cz, 4.0f, 7.0f, 4.0f, wallC, wallA, true);
        }
    }
    tryBuilding(L, r, PropKind::House, -12.0f, -12.0f);
    tryBuilding(L, r, PropKind::House, 12.0f, -12.0f);
    tryBuilding(L, r, PropKind::House, 0.0f, 14.0f);
    const Color boxes[] = {Color{120, 90, 50, 255}, Color{90, 90, 80, 255}};
    int placed = 0;
    for (int tries = 0; placed < 12 && tries < 600; ++tries) {
        const float x = r.range(-22.0f, 22.0f);
        const float z = r.range(-22.0f, 22.0f);
        if (!clearOf(L, x, z, 3.0f)) {
            continue;
        }
        addProp(L, PropKind::Crate, x, z, 2.4f, 2.4f, 2.4f, pick(r, boxes), Color{60, 60, 60, 255}, true);
        ++placed;
    }
    addBoundary(L, Color{110, 100, 85, 255});
    addLamps(L, t.half - 12.0f);
    scatterTrees(L, r, 25, t.half - 8.0f);
}

namespace {

enum class ShopKind { Grocery, Fashion, Electronics, Phones, Computers };

constexpr float MallHalfX = 80.0f;     // outer walls sit at x = +-80
constexpr float MallHalfZ = 40.0f;     // outer walls sit at z = +-40
constexpr float MallHall = 6.0f;       // shop fronts face the corridor at z = +-6
constexpr float MallEntrance = 12.0f;  // half-width of the south entrance and hall
constexpr float MallWallH = 7.0f;
constexpr float MallWallT = 0.4f;
constexpr float HighwayX = 130.0f;

const Color MallWallC{230, 225, 215, 255};
const Color MallWallA{120, 130, 140, 255};
const Color MallWood{150, 110, 70, 255};
const Color MallWoodA{110, 80, 50, 255};
const Color MallSnacks[] = {Color{230, 60, 40, 255}, Color{250, 190, 30, 255}, Color{60, 140, 220, 255},
                            Color{90, 170, 60, 255}, Color{240, 120, 30, 255}};
const Color MallClothes[] = {Color{40, 60, 120, 255},  Color{200, 40, 60, 255},  Color{30, 30, 30, 255},
                             Color{230, 230, 220, 255}, Color{120, 160, 80, 255}, Color{220, 130, 170, 255}};
const Color MallPhones[] = {Color{30, 30, 35, 255}, Color{220, 220, 225, 255}, Color{70, 110, 200, 255},
                            Color{200, 60, 80, 255}};
const Color MallCars[] = {Color{200, 30, 40, 255}, Color{235, 235, 240, 255}, Color{40, 60, 110, 255},
                          Color{90, 90, 95, 255}, Color{60, 140, 90, 255}};

// Box centred on (x, z) standing on y.
void mallBox(Level &L, PropKind kind, float x, float y, float z, float w, float h, float d, Color c, Color a,
             bool solid) {
    addBox(L, kind, Vector3{x - w / 2, y, z - d / 2}, Vector3{x + w / 2, y + h, z + d / 2}, c, a, solid);
}

// One shop between x0 and x1, opening onto the corridor with an automatic door.
void buildShop(Level &L, Rng &r, float x0, float x1, bool north, ShopKind kind) {
    const float cx = (x0 + x1) / 2.0f;
    const float g0 = groundAt(L.terrain, cx, 0.0f);
    const float zFront = north ? -MallHall : MallHall;
    const float sgn = north ? 1.0f : -1.0f;  // +1 walks from the back wall towards the corridor
    const float zIn = north ? -(MallHalfZ - 1.4f) : MallHalfZ - 1.4f;  // row against the back wall
    auto at = [&](float k) { return zIn + sgn * k; };

    Color sign{};
    switch (kind) {
    case ShopKind::Grocery: sign = Color{60, 160, 70, 255}; break;
    case ShopKind::Fashion: sign = Color{220, 90, 150, 255}; break;
    case ShopKind::Electronics: sign = Color{40, 110, 210, 255}; break;
    case ShopKind::Phones: sign = Color{240, 130, 30, 255}; break;
    case ShopKind::Computers: sign = Color{120, 70, 180, 255}; break;
    }

    // Front wall with a doorway in the middle, lintel above it.
    auto seg = [&](float xa, float xb, float y0, float y1) {
        mallBox(L, PropKind::Wall, (xa + xb) / 2.0f, y0, zFront, xb - xa, y1 - y0, MallWallT, MallWallC, MallWallA, true);
    };
    seg(x0, cx - 0.8f, g0, g0 + MallWallH);
    seg(cx + 0.8f, x1, g0, g0 + MallWallH);
    seg(cx - 0.8f, cx + 0.8f, g0 + DoorHeight, g0 + MallWallH);
    const Box shut{Vector3{cx - 0.8f, g0, zFront - DoorThick / 2}, Vector3{cx + 0.8f, g0 + DoorHeight, zFront + DoorThick / 2}};
    L.solids.push_back(shut);
    L.doors.push_back(Door{Vector3{cx - 0.8f, g0, zFront}, Vector3{cx, g0, zFront}, shut, L.solids.size() - 1,
                           false, 0.0f, true});
    mallBox(L, PropKind::Flat, cx, g0 + 2.7f, zFront + sgn * 0.35f, 3.2f, 0.7f, 0.1f, sign, sign, false);
    L.bulbs.push_back(Vector3{cx, g0 + MallWallH - 0.6f, at(8.0f)});

    if (kind == ShopKind::Grocery) {
        // Fridges along the back wall, chip shelves in two aisles.
        for (int i = 0; i < 4; ++i) {
            mallBox(L, PropKind::Fridge, cx - 4.5f + 3.0f * i, g0, zIn, 2.2f, 2.0f, 1.0f, Color{235, 240, 245, 255},
                    i % 2 ? Color{60, 120, 220, 255} : Color{70, 170, 90, 255}, true);
        }
        for (float dx : {-3.2f, 3.2f}) {
            mallBox(L, PropKind::Shelf, cx + dx, g0, at(9.0f), 0.8f, 1.8f, 9.0f, MallWood, MallWoodA, true);
            for (int k = 0; k < 7; ++k) {
                const Color c = pick(r, MallSnacks);
                mallBox(L, PropKind::Pack, cx + dx, g0 + 1.8f, at(5.0f + 1.5f * k), 0.6f, 0.5f, 0.4f, c, c, false);
            }
        }
    } else if (kind == ShopKind::Fashion) {
        // Clothes folded on shelving units.
        for (float dx : {-4.5f, 4.5f}) {
            for (int k = 0; k < 3; ++k) {
                const float z = at(6.0f + 4.0f * k);
                mallBox(L, PropKind::Shelf, cx + dx, g0, z, 4.0f, 1.8f, 0.3f, MallWoodA, MallWood, true);
                for (int j = 0; j < 4; ++j) {
                    const Color c = pick(r, MallClothes);
                    mallBox(L, PropKind::Pack, cx + dx - 1.5f + j, g0 + 1.8f, z, 0.7f, 0.3f, 0.5f, c, c, false);
                }
            }
        }
    } else if (kind == ShopKind::Electronics) {
        // Wall of TVs, laptop tables.
        for (float dx : {-4.0f, 0.0f, 4.0f}) {
            mallBox(L, PropKind::Tv, cx + dx, g0 + 1.2f, zIn, 2.4f, 1.4f, 0.15f, Color{20, 20, 22, 255},
                    Color{40, 90, 160, 255}, false);
        }
        for (float dx : {-4.0f, 4.0f}) {
            for (float k : {8.0f, 14.0f}) {
                mallBox(L, PropKind::Table, cx + dx, g0, at(k), 3.0f, 0.9f, 1.2f, MallWood, MallWoodA, true);
                mallBox(L, PropKind::Pack, cx + dx, g0 + 0.9f, at(k), 0.9f, 0.1f, 0.6f, Color{190, 190, 200, 255},
                        Color{40, 40, 50, 255}, false);
            }
        }
    } else if (kind == ShopKind::Phones) {
        // Phone wall and glass-style counters.
        mallBox(L, PropKind::Shelf, cx, g0, zIn, 10.0f, 2.0f, 0.8f, MallWoodA, MallWood, true);
        for (int i = 0; i < 6; ++i) {
            const Color c = pick(r, MallPhones);
            mallBox(L, PropKind::Pack, cx - 4.0f + 1.6f * i, g0 + 2.0f, zIn, 0.5f, 0.1f, 0.3f, c, c, false);
        }
        for (float dx : {-3.0f, 3.0f}) {
            mallBox(L, PropKind::Table, cx + dx, g0, at(8.0f), 3.0f, 1.0f, 1.0f, Color{200, 210, 220, 255},
                    Color{120, 130, 140, 255}, true);
            for (int j = 0; j < 3; ++j) {
                const Color c = pick(r, MallPhones);
                mallBox(L, PropKind::Pack, cx + dx - 0.8f + 0.8f * j, g0 + 1.0f, at(8.0f), 0.35f, 0.06f, 0.6f, c, c,
                        false);
            }
        }
    } else {
        // Computer desks with monitors and towers.
        for (float dx : {-3.6f, 3.6f}) {
            for (float k : {7.0f, 11.0f, 15.0f}) {
                mallBox(L, PropKind::Table, cx + dx, g0, at(k), 2.4f, 0.9f, 1.0f, MallWood, MallWoodA, true);
                mallBox(L, PropKind::Tv, cx + dx - 0.3f, g0 + 0.9f, at(k), 1.2f, 0.7f, 0.1f, Color{25, 25, 30, 255},
                        Color{60, 140, 220, 255}, false);
                mallBox(L, PropKind::Pack, cx + dx + 0.9f, g0 + 0.9f, at(k), 0.4f, 0.8f, 0.7f, Color{30, 30, 35, 255},
                        Color{80, 220, 120, 255}, false);
            }
        }
    }
}

} // namespace

// A large supermarket-style mall with a stepped roof, skylights, a car park and a highway.
void buildMall(Level &L) {
    Terrain &t = L.terrain;
    Rng r{2222u};
    t.half = RoadSpan + 5.0f;
    t.amp = 0.0f;
    t.seed = 83.0f;
    t.grass = Color{95, 130, 65, 255};
    t.dirt = Color{110, 100, 85, 255};
    L.sky = Color{170, 200, 225, 255};
    L.spawn = Vector3{0.0f, 0.0f, 0.0f};

    const Color asphalt{70, 70, 75, 255};
    const Color paint{235, 235, 230, 255};
    const Color roofC{150, 155, 162, 255};
    const Color roofA{95, 100, 108, 255};
    const Color glass{120, 170, 210, 255};

    addPlot(L, 0.0f, 0.0f, 2.0f * MallHalfX, 2.0f * MallHalfZ, 2.0f, Color{225, 220, 205, 255});
    addPlot(L, 0.0f, 70.0f, 2.0f * MallHalfX, 60.0f, 2.0f, asphalt);  // car park
    addPlot(L, HighwayX, 0.0f, 24.0f, 2.0f * RoadSpan, 0.5f, asphalt);

    auto wall = [&](float x0, float x1, float z0, float z1) {
        addBox(L, PropKind::Wall, Vector3{x0, 0.0f, z0}, Vector3{x1, MallWallH, z1}, MallWallC, MallWallA, true);
    };
    wall(-MallHalfX, MallHalfX, -MallHalfZ, -MallHalfZ + 0.4f);            // north
    wall(-MallHalfX, -MallEntrance, MallHalfZ - 0.4f, MallHalfZ);          // south, with the entrance gap
    wall(MallEntrance, MallHalfX, MallHalfZ - 0.4f, MallHalfZ);
    wall(-MallHalfX, -MallHalfX + 0.4f, -MallHalfZ, MallHalfZ);            // west
    wall(MallHalfX - 0.4f, MallHalfX, -MallHalfZ, MallHalfZ);              // east

    // Dividers between the shop units.
    for (float x : {-64.0f, -48.0f, -32.0f, -16.0f, 0.0f, 16.0f, 32.0f, 48.0f, 64.0f}) {
        wall(x - 0.2f, x + 0.2f, -MallHalfZ, -MallHall);
    }
    for (float x : {-63.0f, -46.0f, -29.0f, -MallEntrance, MallEntrance, 29.0f, 46.0f, 63.0f}) {
        wall(x - 0.2f, x + 0.2f, MallHall, MallHalfZ);
    }

    // North row: ten shops facing the corridor.
    const ShopKind north[] = {ShopKind::Grocery,  ShopKind::Fashion,   ShopKind::Electronics, ShopKind::Phones,
                              ShopKind::Computers, ShopKind::Grocery, ShopKind::Fashion,     ShopKind::Electronics,
                              ShopKind::Phones,   ShopKind::Computers};
    for (int i = 0; i < 10; ++i) {
        buildShop(L, r, -MallHalfX + 16.0f * i, -MallHalfX + 16.0f * (i + 1), true, north[i]);
    }

    // South row: eight shops either side of the entrance hall.
    const float sb[] = {-MallHalfX, -63.0f, -46.0f, -29.0f, -MallEntrance, MallEntrance, 29.0f, 46.0f, 63.0f, MallHalfX};
    const ShopKind south[] = {ShopKind::Fashion,  ShopKind::Grocery,   ShopKind::Phones,      ShopKind::Computers,
                              ShopKind::Electronics, ShopKind::Fashion, ShopKind::Grocery, ShopKind::Electronics};
    for (int i = 0; i < 4; ++i) {
        buildShop(L, r, sb[i], sb[i + 1], false, south[i]);
        buildShop(L, r, sb[i + 5], sb[i + 6], false, south[i + 4]);
    }

    // Corridor furniture and lights.
    for (float x : {-40.0f, 40.0f}) {
        for (float z : {-2.4f, 2.4f}) {
            addProp(L, PropKind::Plant, x, z, 0.8f, 1.5f, 0.8f, Color{150, 90, 60, 255}, Color{40, 120, 50, 255}, false);
        }
    }
    for (float x : {-20.0f, 20.0f}) {
        for (float z : {-2.4f, 2.4f}) {
            addProp(L, PropKind::Couch, x, z, 2.2f, 0.9f, 0.8f, Color{70, 90, 130, 255}, Color{100, 120, 160, 255}, true);
        }
    }
    for (float x : {-70.0f, -50.0f, -30.0f, -10.0f, 10.0f, 30.0f, 50.0f, 70.0f}) {
        L.bulbs.push_back(Vector3{x, MallWallH - 0.6f, 0.0f});
    }

    // Entrance hall: information desk, seating, plants and a sign.
    addProp(L, PropKind::Table, 0.0f, 14.0f, 2.0f, 1.0f, 1.0f, MallWood, MallWoodA, true);
    addProp(L, PropKind::Couch, -4.0f, 19.0f, 2.2f, 0.9f, 0.8f, Color{70, 90, 130, 255}, Color{100, 120, 160, 255}, true);
    addProp(L, PropKind::Couch, 4.0f, 19.0f, 2.2f, 0.9f, 0.8f, Color{70, 90, 130, 255}, Color{100, 120, 160, 255}, true);
    addProp(L, PropKind::Plant, -6.0f, 8.0f, 0.8f, 1.5f, 0.8f, Color{150, 90, 60, 255}, Color{40, 120, 50, 255}, false);
    addProp(L, PropKind::Plant, 6.0f, 8.0f, 0.8f, 1.5f, 0.8f, Color{150, 90, 60, 255}, Color{40, 120, 50, 255}, false);
    L.bulbs.push_back(Vector3{0.0f, MallWallH - 0.6f, 14.0f});
    mallBox(L, PropKind::Flat, 0.0f, 3.8f, 23.4f, 10.0f, 0.8f, 0.1f, Color{200, 30, 40, 255}, Color{200, 30, 40, 255},
            false);

    // Entrance canopy with two pillars.
    addBox(L, PropKind::Wall, Vector3{-16.0f, 4.8f, 40.0f}, Vector3{16.0f, 5.3f, 48.0f}, roofC, roofA, false);
    for (float x : {-15.0f, 15.0f}) {
        addBox(L, PropKind::Wall, Vector3{x - 0.3f, 0.0f, 46.5f}, Vector3{x + 0.3f, 4.8f, 47.1f}, MallWallC, MallWallA,
               true);
    }

    // Stepped roof: eaves, a raised atrium and a higher skylight tier.
    addBox(L, PropKind::Wall, Vector3{-MallHalfX - 1.0f, MallWallH, -MallHalfZ - 1.0f},
           Vector3{MallHalfX + 1.0f, MallWallH + 0.6f, MallHalfZ + 1.0f}, roofC, roofA, false);
    addBox(L, PropKind::Wall, Vector3{-44.0f, MallWallH + 0.6f, -18.0f}, Vector3{44.0f, 10.0f, 18.0f}, roofC, roofA,
           false);
    addBox(L, PropKind::Wall, Vector3{-22.0f, 10.0f, -9.0f}, Vector3{22.0f, 12.5f, 9.0f}, roofC, roofA, false);
    auto glassSlab = [&](float x0, float x1, float z0, float z1, float y) {
        addBox(L, PropKind::Window, Vector3{x0, y, z0}, Vector3{x1, y + 0.12f, z1}, glass, glass, false);
    };
    glassSlab(-44.0f, -22.0f, -18.0f, 18.0f, 10.0f);
    glassSlab(22.0f, 44.0f, -18.0f, 18.0f, 10.0f);
    glassSlab(-22.0f, 22.0f, -18.0f, -9.0f, 10.0f);
    glassSlab(-22.0f, 22.0f, 9.0f, 18.0f, 10.0f);
    glassSlab(-22.0f, 22.0f, -9.0f, 9.0f, 12.5f);

    // Car park: stall lines and parked cars.
    for (float x = -78.0f; x <= 78.0f; x += 6.0f) {
        addBox(L, PropKind::Flat, Vector3{x - 0.1f, 0.02f, 44.0f}, Vector3{x + 0.1f, 0.05f, 96.0f}, paint, paint, false);
    }
    for (float x = -75.0f; x <= 75.0f; x += 6.0f) {
        if (r.next() < 0.3f) {
            continue;
        }
        L.cars.push_back(Car{56.0f, x, 1.0f, 0.0f, false, false, pick(r, MallCars)});
        L.cars.push_back(Car{84.0f, x, -1.0f, 0.0f, false, false, pick(r, MallCars)});
    }

    // Highway: four lanes with a dashed centre line and moving traffic.
    for (float z = -RoadSpan; z < RoadSpan; z += 12.0f) {
        addBox(L, PropKind::Flat, Vector3{HighwayX - 0.12f, 0.02f, z}, Vector3{HighwayX + 0.12f, 0.05f, z + 6.0f}, paint,
               paint, false);
    }
    for (float lane : {121.0f, 127.0f, 133.0f, 139.0f}) {
        const float dir = lane < HighwayX ? 1.0f : -1.0f;
        for (int n = 0; n < 6; ++n) {
            L.cars.push_back(Car{r.range(-RoadSpan, RoadSpan), lane, dir, r.range(10.0f, 16.0f), false, false,
                                 pick(r, MallCars)});
        }
    }
    for (float z = -RoadSpan + 10.0f; z < RoadSpan; z += 30.0f) {
        placeLamp(L, HighwayX - 13.0f, z);
        placeLamp(L, HighwayX + 13.0f, z);
    }

    // Trees around the site.
    for (float z : {-120.0f, -60.0f, 0.0f, 60.0f, 120.0f}) {
        addTree(L, -96.0f, z, 7.0f);
    }
    for (float x : {-60.0f, -20.0f, 20.0f, 60.0f}) {
        addTree(L, x, -70.0f, 7.0f);
    }

    addBoundary(L, Color{110, 110, 115, 255});
}

void buildArena(Level &L) {
    Terrain &t = L.terrain;
    Rng r{2020u};
    t.half = 90.0f;
    t.amp = 0.0f;
    t.seed = 73.0f;
    t.grass = Color{180, 150, 100, 255};
    t.dirt = Color{160, 130, 90, 255};
    L.sky = Color{230, 210, 180, 255};
    L.spawn = Vector3{-70.0f, 0.0f, 0.0f};
    addPlot(L, 0.0f, 0.0f, 66.0f, 66.0f, 2.0f, Color{190, 160, 110, 255});
    constexpr int Segments = 20;
    constexpr float Radius = 32.0f;
    const Color ringC{140, 120, 100, 255};
    const Color ringA{90, 75, 60, 255};
    for (int i = 0; i < Segments; ++i) {
        if (i % 5 == 0) {
            continue;  // four gates
        }
        const float a = 2.0f * Pi * static_cast<float>(i) / Segments;
        addProp(L, PropKind::Wall, Radius * std::cos(a), Radius * std::sin(a), 4.0f, 2.5f, 1.0f, ringC, ringA, true);
    }
    addProp(L, PropKind::Wall, 0.0f, 0.0f, 6.0f, 2.0f, 6.0f, ringC, ringA, true);
    for (float cx : {-14.0f, 14.0f}) {
        for (float cz : {-14.0f, 14.0f}) {
            addProp(L, PropKind::Crate, cx, cz, 2.4f, 2.4f, 2.4f, Color{160, 110, 60, 255}, Color{100, 70, 40, 255},
                    true);
        }
    }
    addBoundary(L, Color{120, 100, 80, 255});
    scatterRocks(L, r, 10, t.half - 8.0f);
}

void buildBadlands(Level &L) {
    Terrain &t = L.terrain;
    Rng r{2121u};
    t.half = 90.0f;
    t.amp = 4.5f;
    t.seed = 79.0f;
    t.grass = Color{160, 95, 60, 255};
    t.dirt = Color{140, 80, 50, 255};
    L.sky = Color{240, 190, 140, 255};
    for (int i = 0; i < 22; ++i) {
        t.hills.push_back({r.range(-80, 80), r.range(-80, 80), r.range(5, 10), r.range(4, 9)});
    }
    for (int i = 0; i < 14; ++i) {
        t.dents.push_back({r.range(-75, 75), r.range(-75, 75), r.range(4, 7), r.range(2, 4)});
    }
    L.spawn = Vector3{-75.0f, 0.0f, -75.0f};
    addBoundary(L, Color{120, 70, 40, 255});
    scatterRocks(L, r, 60, t.half - 8.0f);
    scatterTrees(L, r, 10, t.half - 8.0f);
    scatterBuildings(L, r, 3, t.half - 14.0f);
}

} // namespace world
