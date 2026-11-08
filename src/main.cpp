#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <string>
#include <vector>

namespace {

constexpr float Pi = 3.14159265f;
constexpr float CellSize = 2.0f;
constexpr float WallHeight = 3.0f;
constexpr float PlayerRadius = 0.5f;
constexpr float EyeHeight = 1.7f;
constexpr float MoveSpeed = 6.0f;
constexpr float FireCooldown = 0.1f;
constexpr float TracerTime = 0.05f;
constexpr float MuzzleTime = 0.04f;
constexpr float EnemyRadius = 0.5f;
constexpr float EnemySpeed = 1.8f;
constexpr float HeadY = 1.65f;
constexpr float HeadRadius = 0.28f;
constexpr float TorsoRadius = 0.45f;
constexpr float ArmRadius = 0.25f;
constexpr int ShotSoundCount = 3;
constexpr size_t PlaylistMemory = 3;

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

const char *WarehouseRows[] = {
    "#########################",
    "#.......#.......#.......#",
    "#.......#.......#.......#",
    "#...##..........##......#",
    "#...##..........##......#",
    "#.......................#",
    "#.......#####...........#",
    "#...............#.......#",
    "#...............#.......#",
    "#.......................#",
    "#.....#####.............#",
    "#.......................#",
    "#.......#.......#.......#",
    "#...##..........##......#",
    "#...##.....S....##......#",
    "#.......#.......#.......#",
    "#########################",
};

const char *ArenaRows[] = {
    "#########################",
    "#S......................#",
    "#.....##.........##.....#",
    "#.....##.........##.....#",
    "#.......................#",
    "#...........##..........#",
    "#...........##..........#",
    "#.......................#",
    "#.....##.........##.....#",
    "#.....##.........##.....#",
    "#.......................#",
    "#.......................#",
    "#########################",
};

const char *CorridorRows[] = {
    "#########################",
    "#S....#.........#.......#",
    "#.....#.........#.......#",
    "#.....#.........#.......#",
    "#.....###.###.###.......#",
    "#...............#.......#",
    "#.....#.........#.......#",
    "#.....#.........#.......#",
    "#...............#.......#",
    "#...............#.......#",
    "#.....#...###...#.......#",
    "#.....#.........#.......#",
    "#.....#.........#.......#",
    "#.......................#",
    "#########################",
};

const char *CrossfireRows[] = {
    "#########################",
    "#.......................#",
    "#.##.......##.......##..#",
    "#.##.......##.......##..#",
    "#.......................#",
    "#######.#######.#######.#",
    "#.......................#",
    "#.......................#",
    "#######.#######.#######.#",
    "#.......................#",
    "#.##.......##.......##..#",
    "#.##.......##.......##..#",
    "#........S..............#",
    "#########################",
};

struct MapDef {
    const char *name;
    const char *description;
    const char *const *rows;
    int rowCount;
    Color floor;
    Color wall;
    Color sky;
};

const char *DustYardRows[] = {
    "###########################",
    "#.........#.........#.....#",
    "#.........#.........#.....#",
    "#...###...#...###...#.....#",
    "#...###.........###.......#",
    "#.........#.........#.....#",
    "#####.#####.....#####.#####",
    "#.........#.........#.....#",
    "#.........#...###...#.....#",
    "#...........S.............#",
    "#.###.#.........#.###.#...#",
    "#.###.#.........#.###.#...#",
    "#...........#.........#...#",
    "#####.#####.............###",
    "###########################",
};

const MapDef Maps[] = {
    {"Classic", "The original maze", ClassicRows, static_cast<int>(std::size(ClassicRows)),
     Color{55, 70, 55, 255}, Color{125, 118, 108, 255}, Color{135, 190, 235, 255}},
    {"Warehouse", "Open floor with crate cover", WarehouseRows, static_cast<int>(std::size(WarehouseRows)),
     Color{70, 68, 62, 255}, Color{140, 110, 70, 255}, Color{90, 95, 110, 255}},
    {"Arena", "Open room with pillars", ArenaRows, static_cast<int>(std::size(ArenaRows)),
     Color{60, 90, 60, 255}, Color{150, 150, 160, 255}, Color{150, 200, 240, 255}},
    {"Corridors", "Rooms joined by corridors", CorridorRows, static_cast<int>(std::size(CorridorRows)),
     Color{80, 75, 65, 255}, Color{100, 110, 130, 255}, Color{60, 70, 90, 255}},
    {"Crossfire", "Lanes with cover walls", CrossfireRows, static_cast<int>(std::size(CrossfireRows)),
     Color{50, 60, 75, 255}, Color{170, 80, 60, 255}, Color{120, 160, 200, 255}},
    {"Dust Yard", "Original two-site map with a mid", DustYardRows, static_cast<int>(std::size(DustYardRows)),
     Color{140, 120, 80, 255}, Color{190, 160, 110, 255}, Color{230, 200, 150, 255}},
};
constexpr int MapCount = static_cast<int>(std::size(Maps));

enum class Screen { Menu, Playing };

struct Box {
    Vector3 center;
    Vector3 size;
};

struct Enemy {
    Vector3 pos;
    Vector3 dir;
    float turnTimer;
};

struct Target {
    Vector3 center;
    float radius;
    int points;
    const char *name;
};

Vector3 enemyPoint(const Enemy &e, float sideOffset, float y) {
    const Vector3 side{-e.dir.z, 0.0f, e.dir.x};
    return {e.pos.x + side.x * sideOffset, y, e.pos.z + side.z * sideOffset};
}

std::vector<Target> targetsOf(const Enemy &e) {
    return {
        {enemyPoint(e, 0.0f, HeadY), HeadRadius, 300, "HEAD"},
        {enemyPoint(e, 0.0f, 0.9f), TorsoRadius, 100, "BODY"},
        {enemyPoint(e, 0.45f, 0.93f), ArmRadius, 50, "ARM"},
        {enemyPoint(e, -0.45f, 0.93f), ArmRadius, 50, "ARM"},
    };
}

struct Basis {
    Vector3 side;
    Vector3 up;
    Vector3 fwd;
    Vector3 origin;
};

struct Playlist {
    std::vector<std::string> tracks;
    std::vector<int> recent;
    int current = -1;
    Music music{};
    bool loaded = false;
};

struct Audio {
    Sound shots[ShotSoundCount]{};
    int nextShot = 0;
    bool ready = false;
    Playlist playlist;
};

struct Game {
    Camera3D cam{};
    float yaw = 0.0f;
    float pitch = 0.0f;
    Vector3 pos{};
    int score = 0;
    int shots = 0;
    int hits = 0;
    int wave = 0;
    int mapIndex = 0;
    float fireTimer = 0.0f;
    float tracerTimer = 0.0f;
    float muzzleTimer = 0.0f;
    float kick = 0.0f;
    std::string lastHit;
    float hitTextTimer = 0.0f;
    Vector3 tracerFrom{};
    Vector3 tracerTo{};
    int cols = 0;
    int rows = 0;
    std::vector<std::string> grid;
    std::vector<Box> walls;
    std::vector<Enemy> enemies;
};

float randRange(float a, float b) {
    return a + (b - a) * (GetRandomValue(0, 10000) / 10000.0f);
}

Vector3 cellCenter(const Game &g, int col, int row) {
    return {(col - g.cols / 2.0f + 0.5f) * CellSize, 0.0f, (row - g.rows / 2.0f + 0.5f) * CellSize};
}

bool isWallCell(const Game &g, int col, int row) {
    if (row < 0 || col < 0 || row >= g.rows || col >= g.cols) {
        return true;
    }
    return g.grid[row][col] == '#';
}

bool isWallAt(const Game &g, float x, float z) {
    const int col = static_cast<int>(std::floor(x / CellSize + g.cols / 2.0f));
    const int row = static_cast<int>(std::floor(z / CellSize + g.rows / 2.0f));
    return isWallCell(g, col, row);
}

void buildMap(Game &g, const MapDef &m) {
    g.grid.assign(m.rows, m.rows + m.rowCount);
    g.rows = m.rowCount;
    g.cols = 0;
    for (const std::string &row : g.grid) {
        g.cols = std::max(g.cols, static_cast<int>(row.size()));
    }
    g.walls.clear();
    for (int r = 0; r < g.rows; ++r) {
        g.grid[r].resize(g.cols, '#');
        for (int c = 0; c < g.cols; ++c) {
            const char ch = g.grid[r][c];
            if (ch == 'S') {
                g.pos = cellCenter(g, c, r);
            }
            if (ch == '#') {
                Vector3 center = cellCenter(g, c, r);
                center.y = WallHeight / 2.0f;
                g.walls.push_back({center, {CellSize, WallHeight, CellSize}});
            }
        }
    }
}

Vector3 forwardOf(const Game &g) {
    return {std::cos(g.pitch) * std::sin(g.yaw), std::sin(g.pitch), std::cos(g.pitch) * std::cos(g.yaw)};
}

void pickDir(Enemy &e) {
    const float a = randRange(0.0f, 2.0f * Pi);
    e.dir = {std::cos(a), 0.0f, std::sin(a)};
    e.turnTimer = randRange(1.5f, 3.5f);
}

bool canStand(const Game &g, float x, float z) {
    for (const float dx : {-EnemyRadius, EnemyRadius}) {
        for (const float dz : {-EnemyRadius, EnemyRadius}) {
            if (isWallAt(g, x + dx, z + dz)) {
                return false;
            }
        }
    }
    return true;
}

void spawnEnemy(Game &g) {
    for (int tries = 0; tries < 500; ++tries) {
        const int c = GetRandomValue(0, g.cols - 1);
        const int r = GetRandomValue(0, g.rows - 1);
        if (isWallCell(g, c, r)) {
            continue;
        }
        const Vector3 p = cellCenter(g, c, r);
        if (Vector3Distance(p, g.pos) < 8.0f) {
            continue;
        }
        Enemy e{p, {0.0f, 0.0f, 0.0f}, 0.0f};
        pickDir(e);
        g.enemies.push_back(e);
        return;
    }
}

void spawnWave(Game &g) {
    ++g.wave;
    const int count = 2 + g.wave * 2;
    for (int i = 0; i < count; ++i) {
        spawnEnemy(g);
    }
}

void resetGame(Game &g, int mapIndex) {
    g = Game{};
    g.mapIndex = mapIndex;
    g.cam.up = {0.0f, 1.0f, 0.0f};
    g.cam.fovy = 70.0f;
    g.cam.projection = CAMERA_PERSPECTIVE;
    buildMap(g, Maps[mapIndex]);
    g.cam.position = {g.pos.x, EyeHeight, g.pos.z};
    spawnWave(g);
}

void updateEnemy(const Game &g, Enemy &e, float dt) {
    e.turnTimer -= dt;
    if (e.turnTimer <= 0.0f) {
        pickDir(e);
    }
    const float nx = e.pos.x + e.dir.x * EnemySpeed * dt;
    const float nz = e.pos.z + e.dir.z * EnemySpeed * dt;
    if (canStand(g, nx, nz)) {
        e.pos.x = nx;
        e.pos.z = nz;
    } else {
        pickDir(e);
    }
}

void updatePlayer(Game &g, float dt) {
    const Vector3 fwd = forwardOf(g);
    Vector3 flat = {fwd.x, 0.0f, fwd.z};
    flat = Vector3Normalize(flat);
    const Vector3 right = Vector3Normalize(Vector3CrossProduct(flat, Vector3{0.0f, 1.0f, 0.0f}));

    Vector3 move{0.0f, 0.0f, 0.0f};
    if (IsKeyDown(KEY_W)) move = Vector3Add(move, flat);
    if (IsKeyDown(KEY_S)) move = Vector3Subtract(move, flat);
    if (IsKeyDown(KEY_D)) move = Vector3Add(move, right);
    if (IsKeyDown(KEY_A)) move = Vector3Subtract(move, right);
    if (Vector3Length(move) > 0.0f) {
        move = Vector3Scale(Vector3Normalize(move), MoveSpeed * dt);
        g.pos.x += move.x;
        g.pos.z += move.z;
    }

    // Push out of walls (circle vs. axis-aligned box in XZ).
    for (const Box &w : g.walls) {
        const float hx = w.size.x / 2.0f + PlayerRadius;
        const float hz = w.size.z / 2.0f + PlayerRadius;
        const float dx = g.pos.x - w.center.x;
        const float dz = g.pos.z - w.center.z;
        if (std::fabs(dx) < hx && std::fabs(dz) < hz) {
            const float penX = hx - std::fabs(dx);
            const float penZ = hz - std::fabs(dz);
            if (penX < penZ) {
                g.pos.x += (dx >= 0.0f ? penX : -penX);
            } else {
                g.pos.z += (dz >= 0.0f ? penZ : -penZ);
            }
        }
    }
}

Basis gunBasis(const Game &g) {
    const Vector3 fwd = forwardOf(g);
    const Vector3 right = Vector3Normalize(Vector3CrossProduct(fwd, Vector3{0.0f, 1.0f, 0.0f}));
    const Vector3 up = Vector3CrossProduct(right, fwd);
    const Vector3 side = Vector3CrossProduct(up, fwd);
    Vector3 o = Vector3Add(g.cam.position, Vector3Scale(fwd, 0.8f - g.kick * 0.05f));
    o = Vector3Add(o, Vector3Scale(right, 0.25f));
    o = Vector3Add(o, Vector3Scale(up, -0.25f));
    return {side, up, fwd, o};
}

Vector3 muzzlePoint(const Basis &b) {
    return Vector3Add(Vector3Add(b.origin, Vector3Scale(b.up, 0.02f)), Vector3Scale(b.fwd, 0.67f));
}

Sound makeShotSound() {
    constexpr unsigned int rate = 44100;
    const int frames = static_cast<int>(rate * 0.18f);
    std::vector<short> samples(frames);
    unsigned int seed = 12345u;
    for (int i = 0; i < frames; ++i) {
        const float t = i / static_cast<float>(rate);
        seed = seed * 1664525u + 1013904223u;
        const float noise = (seed >> 16) / 32768.0f - 1.0f;
        const float thump = std::sin(2.0f * Pi * 90.0f * t) * std::exp(-t * 20.0f);
        const float s = (noise * 0.8f + thump * 0.6f) * std::exp(-t * 35.0f);
        samples[i] = static_cast<short>(std::clamp(s, -1.0f, 1.0f) * 30000.0f);
    }
    Wave wave{};
    wave.frameCount = static_cast<unsigned int>(frames);
    wave.sampleRate = rate;
    wave.sampleSize = 16;
    wave.channels = 1;
    wave.data = samples.data();
    return LoadSoundFromWave(wave);
}

void playShot(Audio &a) {
    if (!a.ready) {
        return;
    }
    PlaySound(a.shots[a.nextShot]);
    a.nextShot = (a.nextShot + 1) % ShotSoundCount;
}

void shoot(Game &g, Audio &a) {
    if (g.fireTimer > 0.0f) {
        return;
    }
    g.fireTimer = FireCooldown;
    g.kick = 1.0f;
    g.muzzleTimer = MuzzleTime;
    ++g.shots;
    playShot(a);

    const Basis b = gunBasis(g);
    const Vector3 muzzle = muzzlePoint(b);
    const Ray ray{g.cam.position, b.fwd};

    float wallDist = 60.0f;
    for (const Box &w : g.walls) {
        const BoundingBox bb{
            {w.center.x - w.size.x / 2.0f, w.center.y - w.size.y / 2.0f, w.center.z - w.size.z / 2.0f},
            {w.center.x + w.size.x / 2.0f, w.center.y + w.size.y / 2.0f, w.center.z + w.size.z / 2.0f}};
        const RayCollision rc = GetRayCollisionBox(ray, bb);
        if (rc.hit && rc.distance < wallDist) {
            wallDist = rc.distance;
        }
    }

    int hitIndex = -1;
    float hitDist = wallDist;
    Target hit{};
    for (int i = 0; i < static_cast<int>(g.enemies.size()); ++i) {
        for (const Target &t : targetsOf(g.enemies[i])) {
            const RayCollision rc = GetRayCollisionSphere(ray, t.center, t.radius);
            if (rc.hit && rc.distance < hitDist) {
                hitDist = rc.distance;
                hitIndex = i;
                hit = t;
            }
        }
    }

    if (hitIndex >= 0) {
        g.enemies.erase(g.enemies.begin() + hitIndex);
        ++g.hits;
        g.score += hit.points;
        g.lastHit = std::string(hit.name) + " +" + std::to_string(hit.points);
        g.hitTextTimer = 1.0f;
    }

    g.tracerFrom = muzzle;
    g.tracerTo = Vector3Add(g.cam.position, Vector3Scale(b.fwd, hitDist));
    g.tracerTimer = TracerTime;

    // Small upward kick on the view.
    g.pitch = std::min(g.pitch + 0.01f, 1.5f);
}

void update(Game &g, Audio &a, float dt) {
    const Vector2 d = GetMouseDelta();
    g.yaw -= d.x * 0.0025f;
    g.pitch = std::clamp(g.pitch - d.y * 0.0025f, -1.5f, 1.5f);

    updatePlayer(g, dt);

    g.cam.position = {g.pos.x, EyeHeight, g.pos.z};
    g.cam.target = Vector3Add(g.cam.position, forwardOf(g));

    g.fireTimer = std::max(0.0f, g.fireTimer - dt);
    g.tracerTimer = std::max(0.0f, g.tracerTimer - dt);
    g.muzzleTimer = std::max(0.0f, g.muzzleTimer - dt);
    g.hitTextTimer = std::max(0.0f, g.hitTextTimer - dt);
    g.kick = std::max(0.0f, g.kick - dt * 10.0f);

    if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) || IsKeyDown(KEY_SPACE)) {
        shoot(g, a);
    }

    for (Enemy &e : g.enemies) {
        updateEnemy(g, e, dt);
    }
    if (g.enemies.empty()) {
        spawnWave(g);
    }
}

void loadTrack(Playlist &pl, int index) {
    if (pl.loaded) {
        UnloadMusicStream(pl.music);
    }
    pl.loaded = false;
    pl.music = LoadMusicStream(pl.tracks[index].c_str());
    pl.loaded = pl.music.frameCount > 0;
    if (!pl.loaded) {
        return;
    }
    pl.current = index;
    pl.recent.push_back(index);
    if (pl.recent.size() > PlaylistMemory) {
        pl.recent.erase(pl.recent.begin());
    }
    SetMusicVolume(pl.music, 0.4f);
    PlayMusicStream(pl.music);
}

// Random track that was not played in the last PlaylistMemory rounds.
// Falls back to any track other than the current one when there are too few.
int pickNext(const Playlist &pl) {
    const int n = static_cast<int>(pl.tracks.size());
    std::vector<int> candidates;
    for (int i = 0; i < n; ++i) {
        if (std::find(pl.recent.begin(), pl.recent.end(), i) == pl.recent.end()) {
            candidates.push_back(i);
        }
    }
    if (candidates.empty()) {
        for (int i = 0; i < n; ++i) {
            if (i != pl.current) {
                candidates.push_back(i);
            }
        }
    }
    if (candidates.empty()) {
        return pl.current;
    }
    return candidates[GetRandomValue(0, static_cast<int>(candidates.size()) - 1)];
}

void updatePlaylist(Playlist &pl) {
    if (!pl.loaded) {
        return;
    }
    UpdateMusicStream(pl.music);
    const float length = GetMusicTimeLength(pl.music);
    if (!IsMusicStreamPlaying(pl.music) || GetMusicTimePlayed(pl.music) >= length - 0.1f) {
        loadTrack(pl, pickNext(pl));
    }
}

void scanMusic(Playlist &pl, const std::string &dir) {
    FilePathList files = LoadDirectoryFilesEx(dir.c_str(), ".mp3;.MP3", false);
    for (unsigned int i = 0; i < files.count; ++i) {
        pl.tracks.push_back(files.paths[i]);
    }
    UnloadDirectoryFiles(files);
}

void drawWeapon(const Game &g) {
    const Basis b = gunBasis(g);
    const float m[16] = {
        b.side.x, b.side.y, b.side.z, 0.0f,
        b.up.x, b.up.y, b.up.z, 0.0f,
        b.fwd.x, b.fwd.y, b.fwd.z, 0.0f,
        b.origin.x, b.origin.y, b.origin.z, 1.0f};

    const Color steel{30, 30, 32, 255};
    const Color mag{50, 50, 50, 255};
    const Color wood{110, 70, 35, 255};

    rlPushMatrix();
    rlMultMatrixf(m);
    DrawCubeV(Vector3{0.0f, 0.0f, 0.0f}, Vector3{0.07f, 0.09f, 0.42f}, steel);
    DrawCubeV(Vector3{0.0f, 0.02f, 0.52f}, Vector3{0.03f, 0.03f, 0.3f}, steel);
    DrawCubeV(Vector3{0.0f, 0.065f, 0.35f}, Vector3{0.025f, 0.03f, 0.25f}, steel);
    DrawCubeV(Vector3{0.0f, -0.01f, 0.3f}, Vector3{0.065f, 0.065f, 0.2f}, wood);
    DrawCubeV(Vector3{0.0f, -0.13f, 0.05f}, Vector3{0.05f, 0.2f, 0.09f}, mag);
    DrawCubeV(Vector3{0.0f, -0.12f, -0.14f}, Vector3{0.05f, 0.14f, 0.06f}, wood);
    DrawCubeV(Vector3{0.0f, 0.0f, -0.4f}, Vector3{0.06f, 0.1f, 0.3f}, wood);
    DrawCubeV(Vector3{0.0f, 0.075f, 0.05f}, Vector3{0.03f, 0.03f, 0.08f}, steel);
    if (g.muzzleTimer > 0.0f) {
        DrawSphere(Vector3{0.0f, 0.02f, 0.72f}, 0.09f, Color{255, 200, 60, 230});
    }
    rlPopMatrix();
}

void drawWorld(const Game &g) {
    const MapDef &m = Maps[g.mapIndex];
    DrawPlane(Vector3{0.0f, 0.0f, 0.0f},
              Vector2{g.cols * CellSize, g.rows * CellSize},
              m.floor);

    for (const Box &w : g.walls) {
        DrawCubeV(w.center, w.size, m.wall);
        DrawCubeWiresV(w.center, w.size, Color{60, 60, 60, 255});
    }

    for (const Enemy &e : g.enemies) {
        const Color body{200, 60, 60, 255};
        DrawCapsule(enemyPoint(e, 0.0f, 0.45f), enemyPoint(e, 0.0f, 1.3f), 0.35f, 8, 8, body);
        DrawCapsuleWires(enemyPoint(e, 0.0f, 0.45f), enemyPoint(e, 0.0f, 1.3f), 0.35f, 8, 8, MAROON);
        DrawSphere(enemyPoint(e, 0.0f, HeadY), HeadRadius, Color{235, 200, 170, 255});
        DrawCapsule(enemyPoint(e, 0.3f, 1.25f), enemyPoint(e, 0.6f, 0.6f), 0.12f, 6, 6, body);
        DrawCapsule(enemyPoint(e, -0.3f, 1.25f), enemyPoint(e, -0.6f, 0.6f), 0.12f, 6, 6, body);
    }

    if (g.tracerTimer > 0.0f) {
        DrawLine3D(g.tracerFrom, g.tracerTo, YELLOW);
    }

    drawWeapon(g);
}

void drawHud(const Game &g, const Playlist &pl) {
    const int acc = g.shots > 0 ? (100 * g.hits) / g.shots : 0;
    DrawText(TextFormat("Map: %s", Maps[g.mapIndex].name), 20, 20, 24, RAYWHITE);
    DrawText(TextFormat("Score: %d", g.score), 20, 50, 24, RAYWHITE);
    DrawText(TextFormat("Wave: %d", g.wave), 20, 80, 24, RAYWHITE);
    DrawText(TextFormat("Enemies: %d", static_cast<int>(g.enemies.size())), 20, 110, 24, RAYWHITE);
    DrawText(TextFormat("Shots: %d  Hits: %d  Accuracy: %d%%", g.shots, g.hits, acc), 20, 140, 20, RAYWHITE);
    DrawText("LMB/Space: shoot   WASD: move   Mouse: aim   R: restart   ESC: menu",
             20, GetScreenHeight() - 30, 18, RAYWHITE);

    if (pl.loaded && pl.current >= 0) {
        DrawText(TextFormat("Now playing: %s", GetFileNameWithoutExt(pl.tracks[pl.current].c_str())),
                 20, GetScreenHeight() - 58, 20, RAYWHITE);
    }

    const int cx = GetScreenWidth() / 2;
    const int cy = GetScreenHeight() / 2;
    DrawCircleLines(cx, cy, 3.0f, WHITE);
    DrawLine(cx - 10, cy, cx - 4, cy, WHITE);
    DrawLine(cx + 4, cy, cx + 10, cy, WHITE);
    DrawLine(cx, cy - 10, cx, cy - 4, WHITE);
    DrawLine(cx, cy + 4, cx, cy + 10, WHITE);

    if (g.hitTextTimer > 0.0f) {
        DrawText(g.lastHit.c_str(), cx - 60, cy + 30, 28, YELLOW);
    }
}

Rectangle menuItemRect(int i) {
    return Rectangle{GetScreenWidth() / 2.0f - 210.0f, 200.0f + i * 70.0f, 420.0f, 56.0f};
}

// Returns the chosen map index, or -1 if none was chosen this frame.
int updateMenu(int &sel) {
    if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S)) {
        sel = (sel + 1) % MapCount;
    }
    if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W)) {
        sel = (sel + MapCount - 1) % MapCount;
    }
    const bool mouseMoved = Vector2Length(GetMouseDelta()) > 0.0f;
    const Vector2 m = GetMousePosition();
    for (int i = 0; i < MapCount; ++i) {
        if (CheckCollisionPointRec(m, menuItemRect(i))) {
            if (mouseMoved) {
                sel = i;
            }
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                return i;
            }
        }
    }
    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE)) {
        return sel;
    }
    return -1;
}

void drawMenu(int sel) {
    const char *title = "cs2aimtest";
    DrawText(title, GetScreenWidth() / 2 - MeasureText(title, 60) / 2, 80, 60, RAYWHITE);

    for (int i = 0; i < MapCount; ++i) {
        const Rectangle r = menuItemRect(i);
        DrawRectangleRec(r, i == sel ? Color{200, 70, 50, 255} : Color{50, 50, 60, 255});
        DrawText(Maps[i].name, static_cast<int>(r.x) + 16, static_cast<int>(r.y) + 12, 28, RAYWHITE);
    }

    const int descY = static_cast<int>(200.0f + MapCount * 70.0f) + 10;
    DrawText(Maps[sel].description, GetScreenWidth() / 2 - MeasureText(Maps[sel].description, 22) / 2,
             descY, 22, LIGHTGRAY);

    const char *hint = "Up/Down or mouse to choose   Enter/click to play   Q to quit";
    DrawText(hint, GetScreenWidth() / 2 - MeasureText(hint, 18) / 2, GetScreenHeight() - 40, 18, GRAY);
}

} // namespace

int main() {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(1280, 720, "cs2aimtest");
    SetWindowMinSize(640, 360);
    SetExitKey(KEY_NULL);
    SetTargetFPS(60);
    InitAudioDevice();

    Audio audio;
    audio.ready = IsAudioDeviceReady();
    if (audio.ready) {
        for (Sound &s : audio.shots) {
            s = makeShotSound();
            SetSoundVolume(s, 0.6f);
        }
    }

    scanMusic(audio.playlist, std::string(GetApplicationDirectory()) + "music");
    if (!audio.playlist.tracks.empty()) {
        loadTrack(audio.playlist, pickNext(audio.playlist));
    }

    Screen screen = Screen::Menu;
    int menuSel = 0;
    Game game;
    EnableCursor();

    while (!WindowShouldClose()) {
        const float dt = GetFrameTime();
        updatePlaylist(audio.playlist);

        if (screen == Screen::Menu) {
            if (IsKeyPressed(KEY_Q)) {
                break;
            }
            const int pick = updateMenu(menuSel);
            if (pick >= 0) {
                resetGame(game, pick);
                DisableCursor();
                screen = Screen::Playing;
            }

            BeginDrawing();
            ClearBackground(Color{20, 22, 35, 255});
            drawMenu(menuSel);
            EndDrawing();
            continue;
        }

        if (IsKeyPressed(KEY_ESCAPE)) {
            EnableCursor();
            screen = Screen::Menu;
            continue;
        }
        if (IsKeyPressed(KEY_R)) {
            resetGame(game, game.mapIndex);
        }
        update(game, audio, dt);

        BeginDrawing();
        ClearBackground(Maps[game.mapIndex].sky);
        BeginMode3D(game.cam);
        drawWorld(game);
        EndMode3D();
        drawHud(game, audio.playlist);
        EndDrawing();
    }

    for (Sound &s : audio.shots) {
        if (audio.ready) {
            UnloadSound(s);
        }
    }
    if (audio.playlist.loaded) {
        UnloadMusicStream(audio.playlist.music);
    }
    CloseAudioDevice();
    CloseWindow();
    return 0;
}
