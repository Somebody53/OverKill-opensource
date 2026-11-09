#include <cstdint>

#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include "logo_data.h"
#include "world.h"
#include "mods.h"
#include "skins.h"
#include "lobby_net.h"
#include "server_lookup.h"
#include <sstream>

// C interface of src/ai_rs (Rust enemy brain). Field order matches the Rust #[repr(C)] structs.
struct AiUnit {
    int32_t uid;
    double x, z, dirX, dirZ;
    int32_t team, weapon, last, candStart, candCount;
};
struct AiCand {
    int32_t target;
    double dist;
    int32_t clear;
    double aimX, aimZ;
};
struct AiReply {
    double moveX, moveZ;
    int32_t target, fire;
};
extern "C" {
void ai_set_map(const unsigned char *cells, int32_t cols, double cell, double half);
void ai_think(const AiUnit *units, int32_t count, const AiCand *cands, double dt, AiReply *out);
}

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <future>
#include <fstream>
#include <map>
#include <string>
#include <vector>

using namespace world;

namespace {

constexpr float Pi = 3.14159265f;
constexpr float PlayerRadius = 0.5f;
constexpr float StandHeight = 1.8f;
constexpr float CrouchHeight = 1.1f;
constexpr float WalkSpeed = 4.5f;
constexpr float SprintSpeed = 8.5f;
constexpr float CrouchSpeed = 2.2f;
constexpr float Gravity = 22.0f;
constexpr float JumpSpeed = 7.5f;
constexpr float StepHeight = 0.4f;
constexpr float EnemyHeight = 1.8f;
constexpr float KickCooldown = 0.35f;
constexpr float KickSpeed = 13.0f;
constexpr float BallGravity = 14.0f;
constexpr float BallBounce = 0.5f;
constexpr float SlopeGravity = 9.8f;
constexpr float RollDrag = 1.2f;
constexpr float RapidCooldown = 0.07f;
constexpr float SemiCooldown = 0.3f;
constexpr float KnifeCooldown = 0.45f;
constexpr float KnifeRange = 2.4f;
constexpr float KnifeCone = 0.6f;
constexpr int KnifePoints = 2;
constexpr float BaseFov = 70.0f;
constexpr float ScopeFov = 18.0f;
constexpr float TracerTime = 0.05f;
constexpr float MuzzleTime = 0.04f;
constexpr float FireTime = 0.09f;
constexpr int MaxLamps = 16;
constexpr int MaxTvs = 4;
constexpr float EnemyRadius = 0.5f;
constexpr float EnemySpeed = 1.8f;
constexpr float EnemySearchSpeed = 2.8f;
constexpr float HeadY = 1.65f;
constexpr float HeadRadius = 0.28f;
constexpr float TorsoRadius = 0.45f;
constexpr float ArmRadius = 0.25f;
constexpr float MaxShotDist = 150.0f;
constexpr float MarchStep = 0.25f;
constexpr int ShotSoundCount = 3;
constexpr size_t PlaylistMemory = 3;
constexpr int EnemyHp = 2;
constexpr float CorpseTime = 12.0f;
constexpr float StepStride = 1.9f;
constexpr float CrouchStride = 1.2f;
constexpr int StepSoundCount = 6;
constexpr float VolumeStep = 0.05f;
constexpr int GridCols = 5;
constexpr float CardW = 200.0f;
constexpr float PreviewH = 130.0f;
constexpr float CardH = PreviewH + 34.0f;
constexpr float CardGap = 16.0f;
constexpr float GridTop = 150.0f;
constexpr float ShadowLift = 0.1f;
const Color ShadowColor{0, 0, 0, 85};
constexpr float LampHeight = 4.5f;
constexpr float FlashRange = 30.0f;
constexpr int ChirpCount = 3;
constexpr float CarLength = 4.6f;
constexpr float CarWidth = 2.2f;
constexpr float CarHeight = 1.6f;
constexpr float SignalCycle = 16.0f;
constexpr int StartReserve = 48;
constexpr int StartMoney = 800;
constexpr int RoundWinMoney = 1500;
constexpr float IntermissionMin = 10.0f;
constexpr float IntermissionMax = 15.0f;
constexpr float RoundEndTime = 5.0f;   // online: seconds locked after a round ends
constexpr float RoundBuyTime = 10.0f;  // online: seconds to buy before the next round
// Enemy money: StartMoney plus this much each wave, spent on guns.
constexpr int EnemyIncomePerWave = 900;
constexpr size_t MaxCorpses = 24;
constexpr int TvHits = 3;
constexpr float NavCell = 1.0f;
constexpr float NavEdgeInset = 0.25f;
constexpr int AmmoBoxMax = 8;
constexpr int AmmoBoxAmount = 30;
constexpr float AmmoBoxRespawn = 12.0f;
constexpr float AmmoPickupReach = 1.2f;
constexpr float ChestReach = 1.8f;
constexpr int ChestGrenadePercent = 12;
constexpr int ChestHealthPercent = 50;
constexpr int ChestHealthMin = 11;
constexpr int ChestHealthMax = 25;
constexpr int WorldChestCount = 2;
constexpr float GrenadeFuse = 2.2f;
constexpr float GrenadeSpeed = 16.0f;
constexpr float GrenadeGravity = 14.0f;
constexpr float GrenadeRadius = 5.0f;
constexpr float GrenadeDamage = 90.0f;
constexpr float BlastTime = 0.5f;
constexpr float PlayerMaxHealth = 100.0f;
constexpr float LowHealthFrac = 0.35f;
constexpr float HurtFlashTime = 0.35f;
constexpr float SplatLife = 2.5f;
constexpr size_t MaxSplats = 16;
constexpr float FoeRange = 36.0f;
constexpr float FoeBodyRadius = 0.45f;
constexpr float FoeCooldownMin = 0.7f;
constexpr float FoeCooldownMax = 1.4f;
constexpr float FoeTracerTime = 0.12f;
constexpr float FoeHoldTime = 0.25f;
constexpr float FoePreferNear = 9.0f;
constexpr float FoePreferFar = 16.0f;
constexpr float FoeSoundRange = 60.0f;
constexpr float NoiseMemory = 8.0f;
constexpr int ChestHousePercent = 50;
constexpr float ArmorMax = 100.0f;
constexpr float ArmorAbsorb = 0.5f;      // share of each hit that armor soaks
constexpr int AmmoPackAmount = 60;
constexpr int ThrowCap = 4;              // most of each throwable the player can carry
constexpr float SpawnClearDist = 30.0f;  // foes spawn at least this far from the player's spawn
constexpr float FlashRadius = 14.0f;
constexpr float FlashBlindTime = 3.0f;
constexpr float FlashPlayerTime = 2.5f;
constexpr float SmokeLife = 16.0f;
constexpr float SmokeRadius = 3.0f;
constexpr float SmokeSightRange = 25.0f;
constexpr float DecoyLife = 12.0f;
constexpr float DecoyBeepTime = 0.6f;
constexpr float KillFlashTime = 0.8f;
constexpr float WaveBannerTime = 2.5f;
constexpr float FuseShort = 1.4f;
struct Resolution {
    int w;
    int h;
};

const Resolution Resolutions[] = {{1280, 720}, {1600, 900}, {1920, 1080}, {2560, 1440}};
constexpr int ResolutionCount = static_cast<int>(std::size(Resolutions));
const char *const QualityNames[] = {"Low", "Medium", "High"};
constexpr float QualityDrawDist[] = {70.0f, 130.0f, 200.0f};
constexpr int QualityCount = 3;
const char *const BrightnessNames[] = {"Dark", "Normal", "Bright"};
constexpr float BrightnessDim[] = {0.2f, 0.35f, 0.6f};
constexpr int BrightnessCount = 3;

struct Settings {
    int resolution = 0;
    bool fullscreen = false;
    int quality = 1;
    bool shadows = true;
    int brightness = 1;
    bool teamMode = false;
    bool potato = false;
};

Settings gSettings;
bool gDisplayDirty = false;
bool gRescanMusic = false;

float drawDistance() {
    return QualityDrawDist[gSettings.quality];
}

float nightDim() {
    return BrightnessDim[gSettings.brightness];
}
constexpr float ViewCone = 1.3f;
constexpr int StaticW = 64;
constexpr int StaticH = 48;

struct MapDef {
    const char *name;
    const char *description;
    void (*build)(Level &);
    int crowd;
};

const MapDef Maps[] = {
    {"Classic", "The original maze", buildClassic, 1},
    {"Valley", "Rolling hills, farmhouses, crops and trees", buildValley, 1},
    {"Farm", "Fields, a house, a barn and a silo", buildFarm, 1},
    {"Village", "Houses on streets, surrounded by crops", buildVillage, 1},
    {"Quarry", "A crater ringed by hills", buildQuarry, 1},
    {"Desert", "Dunes, cacti and sandstone rocks", buildDesert, 1},
    {"Snowfield", "Frozen hills, snowy trees and cabins", buildSnowfield, 1},
    {"Forest", "Dense woods with rocky clearings", buildForest, 1},
    {"City", "Big grid of streets with cars, traffic lights, offices and houses", buildCity, 2},
    {"Canyon", "A deep trench between two ridges", buildCanyon, 1},
    {"Suburb", "Streets of houses with yards, a pitch and trees", buildSuburb, 1},
    {"Industrial", "Warehouses, silos, container stacks and a crane", buildIndustrial, 1},
    {"Ruins", "Scattered broken houses, rocks and dents in the ground", buildRuins, 1},
    {"Mesa", "A big hill with rocky slopes and a few trees", buildMesa, 1},
    {"Orchard", "Rows of trees around a single house", buildOrchard, 1},
    {"Containers", "A yard of stacked crates and container blocks", buildContainers, 1},
    {"Plaza", "A paved square with houses in the corners and pillars", buildPlaza, 1},
    {"Jungle", "Dense trees over rolling hills", buildJungle, 1},
    {"Harbor", "A pier, warehouses and stacked crates by the water", buildHarbor, 1},
    {"Compound", "A walled compound with a gated entrance and watchtowers", buildCompound, 1},
    {"Arena", "A ring of walls around an open pit", buildArena, 1},
    {"Badlands", "Eroded ridges and rock fields", buildBadlands, 1},
};
constexpr int MapCount = static_cast<int>(std::size(Maps));

enum class Screen { Menu, Settings, Playing, Mods, Avatar, Cases, Lobby };

// Set each frame: true while joined to a public lobby, which blocks mods.
bool gModsLocked = false;

enum Team { TeamBlue, TeamRed };

enum Weapon {
    WeaponPistol,
    WeaponKnife,
    WeaponAK,
    WeaponSMG,
    WeaponAWP,
    WeaponMac10,
    WeaponM4,
    WeaponDeagle,
    WeaponP90,
    WeaponScout,
    WeaponGlock,
    WeaponUsp,
    WeaponP250,
    WeaponFiveSeven,
    WeaponRevolver,
    WeaponFamas,
    WeaponGalil,
    WeaponMp5,
    WeaponNova,
    WeaponM249,
    WeaponTaser,
    WeaponCount
};

// Throwables carried and thrown with MMB; keys 4-7 pick the type.
enum Throwable { ThrowFrag, ThrowFlash, ThrowSmoke, ThrowDecoy, ThrowCount };
const char *const ThrowNames[ThrowCount] = {"HE grenade", "Flashbang", "Smoke", "Decoy"};

struct WeaponDef {
    const char *name;
    int price;
    int mag;
    float reload;
    bool autoFire;     // full-auto when rapid mode (X) is on
    float autoCooldown;
    float semiCooldown;
    int hits;          // enemy health removed per hit
    float playerDamage; // damage the player takes per hit from an enemy using this weapon
    float spread;      // aim jitter for enemy shots
    float weight;      // kg carried; heavier slows the player
    bool scope;        // right-click zoom
};

constexpr WeaponDef Weapons[WeaponCount] = {
    // name, price, mag, reload, auto, autoCooldown, semiCooldown, hits, playerDamage, spread, weight, scope
    {"pistol", 0, 8, 1.1f, false, SemiCooldown, SemiCooldown, 1, 8.0f, 0.03f, 1.0f, false},
    {"carambit", 0, 0, 0.0f, false, 0.0f, 0.0f, 0, 0.0f, 0.0f, 0.4f, false},
    {"AK-47", 2700, 30, 1.6f, true, RapidCooldown, SemiCooldown, 1, 8.0f, 0.02f, 3.4f, false},
    {"SMG", 1250, 30, 1.4f, true, 0.075f, 0.2f, 1, 6.0f, 0.045f, 2.5f, false},
    {"AWP", 4750, 10, 2.6f, false, 1.5f, 1.5f, 2, 45.0f, 0.002f, 4.8f, true},
    {"Mac-10", 1050, 25, 1.3f, true, 0.07f, 0.2f, 1, 6.0f, 0.05f, 2.3f, false},
    {"M4A1", 3100, 30, 1.5f, true, 0.085f, SemiCooldown, 1, 7.0f, 0.015f, 3.1f, false},
    {"Deagle", 700, 7, 2.2f, false, 0.35f, 0.35f, 1, 16.0f, 0.02f, 1.6f, false},
    {"P90", 2350, 50, 1.8f, true, 0.065f, 0.2f, 1, 6.0f, 0.04f, 2.6f, false},
    {"Scout", 2750, 10, 2.4f, false, 1.2f, 1.2f, 2, 40.0f, 0.002f, 3.6f, true},
    {"Glock-18", 200, 20, 1.2f, false, SemiCooldown, SemiCooldown, 1, 7.0f, 0.03f, 0.9f, false},
    {"USP-S", 500, 12, 1.4f, false, 0.2f, 0.2f, 1, 10.0f, 0.025f, 1.0f, false},
    {"P250", 300, 13, 1.3f, false, 0.2f, 0.2f, 1, 9.0f, 0.03f, 0.9f, false},
    {"Five-SeveN", 500, 20, 1.4f, false, 0.18f, 0.18f, 1, 9.0f, 0.02f, 1.0f, false},
    {"R8 Revolver", 600, 8, 2.2f, false, 0.5f, 0.5f, 1, 20.0f, 0.02f, 1.3f, false},
    {"FAMAS", 2050, 25, 1.9f, true, 0.08f, SemiCooldown, 1, 7.0f, 0.025f, 3.0f, false},
    {"Galil AR", 1800, 35, 2.0f, true, 0.09f, SemiCooldown, 1, 7.0f, 0.03f, 3.3f, false},
    {"MP5", 1500, 30, 1.5f, true, 0.07f, 0.2f, 1, 6.0f, 0.04f, 2.4f, false},
    {"Nova", 1050, 8, 2.5f, false, 0.8f, 0.8f, 1, 14.0f, 0.05f, 3.4f, false},
    {"M249", 5200, 100, 4.5f, true, 0.1f, 0.1f, 1, 8.0f, 0.05f, 7.0f, false},
    {"Taser", 0, 1, 1.8f, false, 0.0f, 1.2f, 0, 0.0f, 0.0f, 0.5f, false},
};

// Weapons shown in the buy menu (pistol and knife are free and always owned).
constexpr int ShopItems[] = {WeaponDeagle, WeaponGlock,  WeaponUsp,   WeaponP250,  WeaponFiveSeven, WeaponRevolver,
                             WeaponAK,     WeaponM4,     WeaponFamas, WeaponGalil,  WeaponSMG,        WeaponMac10,
                             WeaponP90,    WeaponMp5,    WeaponAWP,   WeaponScout,  WeaponNova,       WeaponM249};
constexpr int ShopWeaponCount = static_cast<int>(std::size(ShopItems));
// Buy-menu keys: rows 1-9, then 0 and minus.
constexpr int ShopKeys[] = {KEY_ONE,   KEY_TWO,  KEY_THREE, KEY_FOUR, KEY_FIVE, KEY_SIX,
                            KEY_SEVEN, KEY_EIGHT, KEY_NINE, KEY_ZERO, KEY_MINUS};
const char *const ShopKeyNames[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "-"};
constexpr int ShopKeyCount = 11;  // rows past this are mouse-only
// Gear rows follow the weapons; every buy-menu row is bought with keys 1-9.
enum Gear { GearArmor, GearAmmo, GearFrag, GearFlash, GearSmoke, GearDecoy, GearCount };
struct GearDef {
    const char *name;
    int price;
};
constexpr GearDef Gears[GearCount] = {{"Armor", 650}, {"Ammo +60", 200}, {"HE grenade", 300},
                                      {"Flashbang", 200}, {"Smoke", 300}, {"Decoy", 100}};
constexpr int ShopCount = ShopWeaponCount + GearCount;
// Buy-menu categories; ShopItems lists the weapons in this order, gear comes last.
enum ShopCat { CatPistols, CatRifles, CatSmg, CatHeavy, CatGear, ShopCatCount };
const char *const ShopCatNames[ShopCatCount] = {"Pistols", "Rifles", "SMG", "Heavy", "Gear"};
constexpr int ShopCatSize[ShopCatCount] = {6, 4, 4, 4, GearCount};
static_assert(ShopCatSize[0] + ShopCatSize[1] + ShopCatSize[2] + ShopCatSize[3] == ShopWeaponCount,
              "buy menu categories must cover every weapon");
// Guns enemies may buy, cheapest first.
constexpr int EnemyLoadouts[] = {WeaponPistol, WeaponDeagle, WeaponMac10, WeaponSMG, WeaponP90,
                                 WeaponAK,     WeaponScout,  WeaponM4,    WeaponAWP};
constexpr int EnemyLoadoutCount = 9;

// Cosmetic styles for fighters. Shirts keep the team color; the rest is picked at random.
enum HairStyle { HairBald, HairShort, HairLong, HairMohawk, HairStyleCount };
enum HatStyle { HatNone, HatCap, HatBeanie, HatTop, HatStyleCount };
enum JewelStyle { JewelNone, JewelChain, JewelEarring, JewelBoth, JewelStyleCount };
enum LogoStyle { LogoPlain, LogoStripes, LogoRing, LogoCross, LogoBadge, LogoStyleCount };
const Color HairColors[] = {Color{30, 25, 20, 255}, Color{120, 70, 30, 255}, Color{230, 200, 90, 255},
                            Color{190, 50, 40, 255}, Color{150, 150, 155, 255}};
const Color HatColors[] = {Color{40, 40, 45, 255}, Color{200, 170, 40, 255}, Color{60, 120, 60, 255},
                           Color{235, 235, 235, 255}, Color{120, 50, 130, 255}};

const Color ShirtColors[] = {Color{60, 110, 220, 255}, Color{200, 60, 60, 255}, Color{60, 160, 80, 255},
                             Color{230, 190, 60, 255}, Color{235, 235, 235, 255}};
constexpr int ShirtColorCount = static_cast<int>(std::size(ShirtColors));

struct Look {
    int hair = HairBald;
    int hairColor = 0;
    int hat = HatNone;
    int hatColor = 0;
    int jewel = JewelNone;
    int logo = LogoPlain;
    int shirt = 0;  // index into ShirtColors; only the player's shirt is customizable
};

// The player's avatar, saved in settings.cfg and shown in the menu and buy menu.
Look gPlayerLook;
// Display name, saved in settings.cfg; defaults to PlayerXXXX.
constexpr size_t MaxNameLen = 16;
std::string gPlayerName;
std::string gServerAddress = "127.0.0.1:7777";
std::string gPlayerCustomId;
std::future<ServerLookup> gServerLookup;

// Overcoins, boxes, keys and owned skins. Saved to inventory.cfg.
Inventory gInv;

std::string inventoryFilePath() {
    return std::string(GetApplicationDirectory()) + "inventory.cfg";
}

void persistInventory() {
    saveInventory(gInv, inventoryFilePath());
}

struct Enemy {
    Vector3 pos;
    Vector3 dir;
    float turnTimer;
    int hp = EnemyHp;
    float fireTimer = 0.0f;
    Team team = TeamRed;
    int uid = 0;
    int mag = 0;
    float reloadTimer = 0.0f;
    int weapon = WeaponPistol;
    int lastShot = -1;          // -1 no shot since last brain call, 0 missed, 1 hit
    float blind = 0.0f;         // seconds left blinded by a flashbang
    bool aimingSniper = false;  // AWP holder with a clear shot on the player: gun glows as a warning
    float flash = 0.0f;         // seconds left on the night muzzle flame
    float stun = 0.0f;          // seconds left stunned by a taser: no moves, no shots
    Vector3 flashPos{};
    Vector3 flashDir{};
    Look look;                  // cosmetics: hair, hat, jewelry, shirt logo
    int netId = 0;              // online: lobby id of the remote player (0 for bots and teammates)
    float netHealth = 1.0f;     // online: remote health as a fraction of full
    int gunSkin = -1;           // online: remote player's gun skin for the weapon in hand
    int knifeSkin = -1;         // online: remote player's knife skin
    Vector3 netPos{};           // online: last position reported by the remote player
};

struct Target {
    Vector3 center;
    float radius;
    int points;
    const char *name;
};

struct Ball {
    Vector3 pos;
    Vector3 vel;
};

constexpr int PartCount = 7;
constexpr int LinkCount = 6;
enum Part { PHead, PChest, PHip, PLHand, PRHand, PLFoot, PRFoot };

struct BodyLink {
    int a;
    int b;
};

const BodyLink BodyLinks[LinkCount] = {
    {PHead, PChest}, {PChest, PHip}, {PChest, PLHand}, {PChest, PRHand}, {PHip, PLFoot}, {PHip, PRFoot}};

struct Corpse {
    Vector3 p[PartCount];
    Vector3 v[PartCount];
    float rest[LinkCount];
    float age;
    float still = 0.0f;   // seconds the body has been nearly motionless
    bool asleep = false;  // settled corpses skip physics
};

struct Bird {
    Vector3 pos;
    Vector3 vel;
    float age;
    float life;
    float phase;
};

Vector3 enemyPoint(const Enemy &e, float sideOffset, float y) {
    const Vector3 side{-e.dir.z, 0.0f, e.dir.x};
    return {e.pos.x + side.x * sideOffset, e.pos.y + y, e.pos.z + side.z * sideOffset};
}

std::vector<Target> targetsOf(const Enemy &e) {
    return {
        {enemyPoint(e, 0.0f, HeadY), HeadRadius, 120, "HEAD"},
        {enemyPoint(e, 0.0f, 0.9f), TorsoRadius, 40, "BODY"},
        {enemyPoint(e, 0.45f, 0.93f), ArmRadius, 20, "ARM"},
        {enemyPoint(e, -0.45f, 0.93f), ArmRadius, 20, "ARM"},
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
    float volume = 0.4f;
    bool muted = false;
};

struct Audio {
    Sound shots[WeaponCount][ShotSoundCount]{};
    int nextShot = 0;
    Sound steps[StepSoundCount]{};
    int nextStep = 0;
    Sound chirps[ChirpCount]{};
    Sound bug{};
    Sound flap{};
    Sound wind{};
    Sound hurt{};
    Sound hitMarker{};
    Sound crack{};
    Sound kill{};
    Sound boom{};
    Sound bang{};
    Sound ring{};
    Sound hiss{};
    Sound cash{};
    Sound clink{};
    Sound tick{};
    Sound waveStart{};
    Sound roundWon{};
    Sound foeShots[WeaponCount][ShotSoundCount]{};
    int nextFoeShot = 0;
    float chirpTimer = 2.0f;
    float bugTimer = 6.0f;
    float flockTimer = 20.0f;
    Sound saberHum{};
    Sound saberIgnite{};
    Sound saberSwing{};
    bool saberLit = false;  // lightsaber is on and humming
    bool ready = false;
    Playlist playlist;
};

struct AmmoBox {
    Vector3 pos;
};

struct Chest {
    Vector3 pos;
    bool opened = false;
};

struct Grenade {
    Vector3 pos;
    Vector3 vel;
    float fuse;
    int kind = ThrowFrag;
};

struct Blast {
    Vector3 pos;
    float age;
    bool bright = false;  // flashbang flash instead of an explosion
};

struct SmokeCloud {
    Vector3 pos;
    float age;
};

struct Decoy {
    Vector3 pos;
    float life;
    float beep;
};

struct FoeTracer {
    Vector3 from;
    Vector3 to;
    float age;
};

struct BloodSplat {
    float x;
    float y;
    float r;
    float age;
};

// A gun lying on the floor.
struct DroppedWeapon {
    Vector3 pos;
    int weapon;
    int mag;
};

// One killfeed line: who killed whom, with what, and the icons for headshot, flash, and smoke.
struct KillEntry {
    std::string killer;
    Color killerColor;
    std::string weapon;
    std::string victim;
    Color victimColor;
    bool headshot;
    bool blind;
    bool smoke;
    float age = 0.0f;
};

// What a purchase changed, saved so it can be returned during the same wave break.
struct BuySnapshot {
    std::string name;
    int price = 0;
    int weapon = WeaponPistol;
    int mags[WeaponCount]{};
    bool owned[WeaponCount]{};
    int primary = -1;
    int sidearm = WeaponPistol;
    int reserve = 0;
    float armor = 0.0f;
    int stock[ThrowCount]{};
    float reloadTimer = 0.0f;
    std::vector<DroppedWeapon> drops;
};

struct Game {
    Camera3D cam{};
    float yaw = 0.0f;
    float pitch = 0.0f;
    Vector3 pos{};
    int money = StartMoney;
    int roundKillMoney = 0;     // combat money earned this wave, kept when the player respawns
    int shots = 0;
    int hits = 0;
    int wave = 0;
    int mapIndex = 0;
    float fireTimer = 0.0f;
    float tracerTimer = 0.0f;
    float muzzleTimer = 0.0f;
    float fireGlow = 0.0f;  // seconds left on the night muzzle fire
    float kick = 0.0f;
    Vector3 noisePos{};
    float noiseTimer = 0.0f;  // seconds the last gunshot can still draw foes
    std::string lastHit;
    float hitTextTimer = 0.0f;
    Vector3 tracerFrom{};
    Vector3 tracerTo{};
    Level level;
    std::vector<Enemy> enemies;
    std::vector<Ball> balls;
    float height = StandHeight;
    float vy = 0.0f;
    bool grounded = true;
    float kickTimer = 0.0f;
    Vector3 playerMove{};
    bool rapid = true;
    float stepDist = 0.0f;
    std::vector<Corpse> corpses;
    std::vector<Bird> birds;
    bool night = false;
    bool flashlight = false;
    std::vector<Car> cars;
    int weapon = WeaponPistol;
    int mags[WeaponCount]{};
    bool owned[WeaponCount]{};
    int primary = -1;           // the bought primary weapon, -1 if none
    int reserve = StartReserve;
    float intermission = 0.0f;  // seconds left before the next wave, 0 while fighting
    bool shopOpen = false;
    float reloadTimer = 0.0f;
    float health = PlayerMaxHealth;
    float armor = 0.0f;
    int stock[ThrowCount]{};    // carried throwables: frag, flash, smoke, decoy
    int throwSel = ThrowFrag;
    bool holding = false;       // grenade in hand: shooting is blocked
    float ammoRespawn = AmmoBoxRespawn;
    std::vector<AmmoBox> ammoBoxes;
    std::vector<Chest> chests;
    std::vector<Grenade> grenadeList;
    std::vector<Blast> blasts;
    std::vector<SmokeCloud> smokes;
    std::vector<Decoy> decoys;
    float flashed = 0.0f;       // white-out strength from a nearby flashbang
    Vector3 flashAt{};          // where that flashbang went off
    float shake = 0.0f;         // camera shake strength
    float killFlash = 0.0f;     // seconds left on the KILL popup
    float waveBanner = 0.0f;    // seconds left on the WAVE banner
    float holdTimer = 0.0f;     // seconds left on the held-item label
    std::string holdText;       // what the held-item label says
    int shopSel = 0;            // buy-menu row shown in the spinning preview
    int shopAlly = -1;          // uid of the teammate the buy menu buys for, -1 for the player
    float shopScroll = 0.0f;    // buy-menu list scroll, in pixels
    std::vector<BuySnapshot> buyHistory;  // purchases this wave break, newest last
    bool scoped = false;        // right-click scope on a scoped rifle
    bool freshKill = false;     // set by a melee or gun kill, cleared after the sound plays
    std::vector<FoeTracer> foeTracers;
    float hurtTimer = 0.0f;
    float lastHealth = PlayerMaxHealth;
    std::vector<BloodSplat> splats;
    std::vector<KillEntry> killFeed;
    std::vector<DroppedWeapon> drops;
    int sidearm = WeaponPistol;  // pistol-class slot: pistol or Deagle
    float saberSwing = 0.0f;     // seconds left on the lightsaber swing arc
    bool online = false;         // true while in a shared match with other players
    bool netHost = false;        // online: this client is the lobby host and drives the cars
    int localFd = 0;             // online: our lobby id
    Team localTeam = TeamBlue;   // online: our side in team mode
    bool dead = false;           // online: dead until the next round starts
    bool deathSent = false;      // online: DEATH already sent for this life
    int lastAttacker = 0;        // online: lobby id of the last player who hurt us
    int lastWeapon = -1;         // online: weapon of that hit
    bool lastHead = false;       // online: that hit was a headshot
    std::vector<std::string> netOut; // online: lines waiting to be sent
    bool roundLive = false;          // online: a round is being fought
    int roundPlayers = 0;            // online: most players seen alive this round
    std::string roundMsg;            // online: who won the last round
    float netSendTimer = 0.0f;
    float carSendTimer = 0.0f;
};

constexpr float KillFeedTime = 6.0f;
constexpr size_t KillFeedMax = 5;
constexpr float NetSendInterval = 0.1f;  // online: seconds between player state updates
constexpr float CarSendInterval = 0.2f;  // online: seconds between car snapshots from the host

Color teamColor(Team t) {
    return t == TeamBlue ? SKYBLUE : Color{255, 120, 120, 255};
}

std::string botName(const Enemy &e) {
    return "Bot " + std::to_string(e.uid);
}

void addKill(Game &g, KillEntry k) {
    if (k.killer == "You") {
        gInv.coins += k.headshot ? 3 : 1;
        persistInventory();
    }
    g.killFeed.push_back(k);
    if (g.killFeed.size() > KillFeedMax) {
        g.killFeed.erase(g.killFeed.begin());
    }
}

// Online: the HIT line a shooter sends so the server can pass the damage to the victim.
std::string hitLine(int target, float dmg, bool head, int weapon) {
    std::ostringstream out;
    out << "HIT " << target << ' ' << dmg << ' ' << (head ? 1 : 0) << ' ' << weapon;
    return out.str();
}

// Kill feed name: "You" for us, "Player N" for others.
std::string playerName(const Game &g, int fd) {
    if (fd == g.localFd) {
        return "You";
    }
    if (fd == 0) {
        return "World";
    }
    return "Player " + std::to_string(fd);
}

// The remote player with this lobby id, or nullptr.
Enemy *remoteOf(Game &g, int fd) {
    for (Enemy &e : g.enemies) {
        if (e.netId == fd) {
            return &e;
        }
    }
    return nullptr;
}

void removeRemote(Game &g, int fd) {
    g.enemies.erase(std::remove_if(g.enemies.begin(), g.enemies.end(),
                                   [fd](const Enemy &e) { return e.netId == fd; }),
                    g.enemies.end());
}

bool isSidearm(int w) {
    return w == WeaponPistol || w == WeaponDeagle || w == WeaponGlock || w == WeaponUsp || w == WeaponP250 ||
           w == WeaponFiveSeven || w == WeaponRevolver;
}

// Puts a carried gun on the floor at the player's feet.
void releaseWeapon(Game &g, int w) {
    g.drops.push_back(DroppedWeapon{g.pos, w, g.mags[w]});
    g.owned[w] = false;
    g.mags[w] = 0;
}

// The pistol is not worth a drop.
void dropEnemyGun(Game &g, const Enemy &e) {
    if (e.weapon != WeaponPistol) {
        g.drops.push_back(DroppedWeapon{e.pos, e.weapon, e.mag});
    }
}

// Index of a dropped gun within reach, or -1.
int nearDroppedWeapon(const Game &g) {
    for (size_t i = 0; i < g.drops.size(); ++i) {
        const Vector3 p = g.drops[i].pos;
        if (std::hypot(p.x - g.pos.x, p.z - g.pos.z) < AmmoPickupReach && std::fabs(p.y - g.pos.y) < 2.0f) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int &activeMag(Game &g) {
    return g.mags[g.weapon];
}

int magSizeOf(const Game &g) {
    return Weapons[g.weapon].mag;
}

float reloadTimeOf(const Game &g) {
    return Weapons[g.weapon].reload;
}

// True between waves: no moving, shooting, or wave fighting until the countdown ends.
bool playerFrozen(const Game &g) {
    return g.intermission > 0.0f || g.dead;
}

// Armor soaks part of each hit until it runs out.
void hurtPlayer(Game &g, float dmg) {
    const float soaked = std::min(g.armor, dmg * ArmorAbsorb);
    g.armor -= soaked;
    g.health -= dmg - soaked;
}

float randRange(float a, float b) {
    return a + (b - a) * (GetRandomValue(0, 10000) / 10000.0f);
}

bool blockedAt(const Level &L, float x, float z, float r) {
    if (std::fabs(x) > L.terrain.half - 2.0f || std::fabs(z) > L.terrain.half - 2.0f) {
        return true;
    }
    const float ground = groundAt(L.terrain, x, z);
    for (const Box &b : L.solids) {
        if (b.max.y <= ground + StepHeight || b.min.y >= ground + EnemyHeight) {
            continue;
        }
        if (x > b.min.x - r && x < b.max.x + r && z > b.min.z - r && z < b.max.z + r) {
            return true;
        }
    }
    return false;
}

float terrainHitDist(const Terrain &t, const Ray &ray, float maxD) {
    for (float d = MarchStep; d < maxD; d += MarchStep) {
        const Vector3 p = Vector3Add(ray.position, Vector3Scale(ray.direction, d));
        if (p.y <= groundAt(t, p.x, p.z)) {
            return d;
        }
    }
    return maxD;
}

void pushOutOfBox(Vector3 &p, const Box &b, float r) {
    const Vector3 cp{std::clamp(p.x, b.min.x, b.max.x), std::clamp(p.y, b.min.y, b.max.y),
                     std::clamp(p.z, b.min.z, b.max.z)};
    const Vector3 n = Vector3Subtract(p, cp);
    const float d = Vector3Length(n);
    if (d > 1e-4f && d < r) {
        p = Vector3Add(cp, Vector3Scale(n, r / d));
    }
}

Corpse makeCorpse(const Enemy &e, Vector3 dir) {
    Corpse c{};
    c.p[PHead] = enemyPoint(e, 0.0f, HeadY);
    c.p[PChest] = enemyPoint(e, 0.0f, 1.2f);
    c.p[PHip] = enemyPoint(e, 0.0f, 0.85f);
    c.p[PLHand] = enemyPoint(e, 0.45f, 0.95f);
    c.p[PRHand] = enemyPoint(e, -0.45f, 0.95f);
    c.p[PLFoot] = enemyPoint(e, 0.2f, 0.1f);
    c.p[PRFoot] = enemyPoint(e, -0.2f, 0.1f);
    for (int i = 0; i < LinkCount; ++i) {
        c.rest[i] = Vector3Distance(c.p[BodyLinks[i].a], c.p[BodyLinks[i].b]);
    }
    const Vector3 kick = Vector3Add(Vector3Scale(dir, 6.0f), Vector3{0.0f, 3.0f, 0.0f});
    for (int i = 0; i < PartCount; ++i) {
        c.v[i] = Vector3Add(kick, Vector3{randRange(-1.0f, 1.0f), randRange(0.0f, 1.0f), randRange(-1.0f, 1.0f)});
    }
    return c;
}

void updateCorpse(const Level &L, Corpse &c, float dt) {
    if (c.asleep) {
        c.age += dt;
        return;
    }
    constexpr int Iter = 4;
    constexpr float PartRadius = 0.15f;
    Vector3 old[PartCount];
    for (int i = 0; i < PartCount; ++i) {
        old[i] = c.p[i];
        c.v[i].y -= Gravity * dt;
        c.p[i] = Vector3Add(c.p[i], Vector3Scale(c.v[i], dt));
    }
    for (int it = 0; it < Iter; ++it) {
        for (int l = 0; l < LinkCount; ++l) {
            Vector3 &pa = c.p[BodyLinks[l].a];
            Vector3 &pb = c.p[BodyLinks[l].b];
            const Vector3 d = Vector3Subtract(pb, pa);
            const float len = Vector3Length(d);
            if (len < 1e-5f) {
                continue;
            }
            const Vector3 corr = Vector3Scale(d, 0.5f * (len - c.rest[l]) / len);
            pa = Vector3Add(pa, corr);
            pb = Vector3Subtract(pb, corr);
        }
        for (int i = 0; i < PartCount; ++i) {
            for (const Box &box : L.solids) {
                pushOutOfBox(c.p[i], box, PartRadius);
            }
            const float ground = groundAt(L.terrain, c.p[i].x, c.p[i].z) + PartRadius;
            if (c.p[i].y < ground) {
                c.p[i].y = ground;
            }
        }
    }
    for (int i = 0; i < PartCount; ++i) {
        c.v[i] = Vector3Scale(Vector3Subtract(c.p[i], old[i]), (1.0f / dt) * 0.97f);
    }
    float fastest = 0.0f;
    for (int i = 0; i < PartCount; ++i) {
        fastest = std::max(fastest, Vector3Length(c.v[i]));
    }
    c.still = fastest < 0.05f ? c.still + dt : 0.0f;
    c.asleep = c.still > 0.5f;
    c.age += dt;
}

// Keeps at most MaxCorpses bodies; the oldest one is removed first.
void addCorpse(Game &g, const Corpse &c) {
    g.corpses.push_back(c);
    if (g.corpses.size() > MaxCorpses) {
        g.corpses.erase(g.corpses.begin());
    }
}

void pickDir(Enemy &e) {
    const float a = randRange(0.0f, 2.0f * Pi);
    e.dir = {std::cos(a), 0.0f, std::sin(a)};
    e.turnTimer = randRange(1.5f, 3.5f);
}

// Deathmatch: everyone fights everyone. Team mode: only different teams fight.
bool hostile(Team a, Team b) {
    return !gSettings.teamMode || a != b;
}

// Team mode: only different teams fight, so teammates on either side never shoot each other.
bool botHostile(Team a, Team b) {
    return !gSettings.teamMode || a != b;
}

Look randomLook() {
    Look l;
    l.hair = GetRandomValue(0, HairStyleCount - 1);
    l.hairColor = GetRandomValue(0, static_cast<int>(std::size(HairColors)) - 1);
    l.hat = GetRandomValue(0, HatStyleCount - 1);
    l.hatColor = GetRandomValue(0, static_cast<int>(std::size(HatColors)) - 1);
    l.jewel = GetRandomValue(0, JewelStyleCount - 1);
    l.logo = GetRandomValue(0, LogoStyleCount - 1);
    return l;
}

// Spends enemy money on a gun: the priciest affordable half the time, otherwise a random affordable one.
int buyEnemyWeapon(int &money) {
    int options[EnemyLoadoutCount];
    int n = 0;
    for (int i = 0; i < EnemyLoadoutCount; ++i) {
        if (Weapons[EnemyLoadouts[i]].price <= money) {
            options[n++] = EnemyLoadouts[i];
        }
    }
    const int pick = GetRandomValue(0, 1) == 0 ? options[n - 1] : options[GetRandomValue(0, n - 1)];
    money -= Weapons[pick].price;
    return pick;
}

void addEnemy(Game &g, Vector3 p, Team team, int weapon) {
    Enemy e{p, {0.0f, 0.0f, 0.0f}, 0.0f};
    static int nextUid = 1;
    e.uid = nextUid++;
    e.team = team;
    e.weapon = weapon;
    e.mag = Weapons[weapon].mag;
    e.look = randomLook();
    pickDir(e);
    g.enemies.push_back(e);
}

// Foes start as far from the player's spawn point as the map allows.
void spawnFoe(Game &g, int weapon) {
    const float half = g.level.terrain.half;
    for (const float minDist : {SpawnClearDist, 8.0f}) {
        for (int tries = 0; tries < 500; ++tries) {
            const float x = randRange(-half + 4.0f, half - 4.0f);
            const float z = randRange(-half + 4.0f, half - 4.0f);
            if (blockedAt(g.level, x, z, EnemyRadius)) {
                continue;
            }
            const Vector3 p{x, groundAt(g.level.terrain, x, z), z};
            if (Vector3Distance(p, g.pos) < minDist) {
                continue;
            }
            // Team mode: the red team starts on the half of the map opposite the blue spawn.
            const bool sameHalf = x * g.level.spawn.x + z * g.level.spawn.z > 0.0f;
            if (gSettings.teamMode && sameHalf && tries < 400) {
                continue;
            }
            addEnemy(g, p, TeamRed, weapon);
            return;
        }
    }
}

// Teammates appear in a ring around the player.
void spawnAlly(Game &g, int weapon) {
    for (int tries = 0; tries < 60; ++tries) {
        const float ang = randRange(0.0f, 2.0f * Pi);
        const float r = randRange(2.0f, 5.0f);
        const float x = g.pos.x + std::cos(ang) * r;
        const float z = g.pos.z + std::sin(ang) * r;
        if (blockedAt(g.level, x, z, EnemyRadius)) {
            continue;
        }
        addEnemy(g, Vector3{x, groundAt(g.level.terrain, x, z), z}, TeamBlue, weapon);
        return;
    }
}

void spawnWave(Game &g) {
    ++g.wave;
    g.roundKillMoney = 0;
    // Purchases can only be returned before the wave they were made for starts.
    g.buyHistory.clear();
    // Guns on the floor from the last round are removed; fights start with a clean map.
    g.drops.clear();
    // Every wave starts back at the spawn area with full health and restocked chests.
    g.pos = g.level.spawn;
    g.pos.y = groundAt(g.level.terrain, g.pos.x, g.pos.z);
    g.vy = 0.0f;
    g.grounded = true;
    g.health = PlayerMaxHealth;
    g.waveBanner = WaveBannerTime;
    for (Chest &c : g.chests) {
        c.opened = false;
    }
    g.grenadeList.clear();
    g.blasts.clear();
    g.smokes.clear();
    g.decoys.clear();
    int count = (6 + g.wave * 3) * Maps[g.mapIndex].crowd;
    // Team mode: both teams get the same number of fighters.
    if (gSettings.teamMode && count % 2 != 0) {
        ++count;
    }
    const int allyTarget = gSettings.teamMode ? count / 2 : 0;
    g.enemies.erase(std::remove_if(g.enemies.begin(), g.enemies.end(),
                                   [](const Enemy &e) { return e.team == TeamBlue; }),
                    g.enemies.end());
    for (int i = 0; i < count; ++i) {
        // Each fighter brings their own money for this wave and buys a gun with it.
        int money = StartMoney + EnemyIncomePerWave * g.wave;
        const int weapon = buyEnemyWeapon(money);
        if (i < allyTarget) {
            spawnAlly(g, weapon);
        } else {
            spawnFoe(g, weapon);
        }
    }
}

Vector3 forwardOf(const Game &g) {
    return {std::cos(g.pitch) * std::sin(g.yaw), std::sin(g.pitch), std::cos(g.pitch) * std::cos(g.yaw)};
}

bool openFloorSpot(const Game &g, float minDist, Vector3 &out) {
    const float half = g.level.terrain.half;
    for (int tries = 0; tries < 300; ++tries) {
        const float x = randRange(-half + 4.0f, half - 4.0f);
        const float z = randRange(-half + 4.0f, half - 4.0f);
        if (blockedAt(g.level, x, z, EnemyRadius) || std::hypot(x - g.pos.x, z - g.pos.z) < minDist) {
            continue;
        }
        out = Vector3{x, groundAt(g.level.terrain, x, z), z};
        return true;
    }
    return false;
}

void spawnAmmoBox(Game &g) {
    Vector3 p{};
    if (openFloorSpot(g, 6.0f, p)) {
        g.ammoBoxes.push_back(AmmoBox{p});
    }
}

// A chest just outside some houses and a few on open ground, never overlapping.
void placeChests(Game &g) {
    for (const Prop &p : g.level.props) {
        if (p.kind != PropKind::House || GetRandomValue(0, 99) >= ChestHousePercent) {
            continue;
        }
        const float reach = std::max(p.w, p.d) / 2 + 1.2f;
        for (int tries = 0; tries < 16; ++tries) {
            const float a = randRange(0.0f, 6.2831853f);
            const float x = p.base.x + std::cos(a) * reach;
            const float z = p.base.z + std::sin(a) * reach;
            if (blockedAt(g.level, x, z, 0.5f)) {
                continue;
            }
            const bool overlaps = std::any_of(g.chests.begin(), g.chests.end(), [&](const Chest &c) {
                return std::hypot(c.pos.x - x, c.pos.z - z) < 2.0f;
            });
            if (overlaps) {
                continue;
            }
            g.chests.push_back(Chest{Vector3{x, groundAt(g.level.terrain, x, z), z}});
            break;
        }
    }
    for (int i = 0; i < WorldChestCount; ++i) {
        Vector3 p{};
        if (openFloorSpot(g, 6.0f, p)) {
            g.chests.push_back(Chest{p});
        }
    }
}

// Enemy navigation grid for ai.py (one char per NavCell square, rows along z).
// Bit 0: cell is open. Bit 1: edge to the +x neighbour is open. Bit 2: edge to the +z neighbour is open.
// Door boxes are ignored because enemies open doors themselves.
std::string buildNavGrid(const Level &L, int &cols) {
    const float half = L.terrain.half;
    cols = static_cast<int>(std::ceil(2.0f * half / NavCell));
    std::vector<char> skip(L.solids.size(), 0);
    for (const Door &d : L.doors) {
        if (d.solid < skip.size()) {
            skip[d.solid] = 1;
        }
    }
    std::vector<Box> walls;
    for (size_t k = 0; k < L.solids.size(); ++k) {
        const Box &b = L.solids[k];
        const float ground = groundAt(L.terrain, (b.min.x + b.max.x) / 2.0f, (b.min.z + b.max.z) / 2.0f);
        if (skip[k] || b.max.y <= ground + StepHeight || b.min.y >= ground + EnemyHeight) {
            continue;
        }
        walls.push_back(b);
    }
    auto inside = [&walls](float x, float z, float e) {
        for (const Box &b : walls) {
            if (x > b.min.x - e && x < b.max.x + e && z > b.min.z - e && z < b.max.z + e) {
                return true;
            }
        }
        return false;
    };
    auto coord = [half](int i) { return -half + (static_cast<float>(i) + 0.5f) * NavCell; };
    std::string grid;
    grid.reserve(static_cast<size_t>(cols) * static_cast<size_t>(cols));
    for (int j = 0; j < cols; ++j) {
        for (int i = 0; i < cols; ++i) {
            const float x = coord(i);
            const float z = coord(j);
            int flags = (std::fabs(x) <= half - 2.0f && std::fabs(z) <= half - 2.0f && !inside(x, z, 0.0f)) ? 1 : 0;
            for (int dir = 0; dir < 2; ++dir) {
                const bool hasNext = dir == 0 ? i + 1 < cols : j + 1 < cols;
                if (!hasNext) {
                    continue;
                }
                bool clear = true;
                for (int s = 1; s <= 3 && clear; ++s) {
                    const float t = s * 0.25f * NavCell;
                    clear = !inside(dir == 0 ? x + t : x, dir == 0 ? z : z + t, NavEdgeInset);
                }
                if (clear) {
                    flags |= dir == 0 ? 2 : 4;
                }
            }
            grid.push_back(static_cast<char>('0' + flags));
        }
    }
    return grid;
}

// Gives the enemy brain the walkable grid of the current map.
void sendMapToBrain(const Game &g) {
    int cols = 0;
    const std::string grid = buildNavGrid(g.level, cols);
    ai_set_map(reinterpret_cast<const unsigned char *>(grid.data()), cols, static_cast<double>(NavCell),
               static_cast<double>(g.level.terrain.half));
}

// Frees the terrain model.
void unloadLevel(Level &L) {
    if (!L.modelLoaded) {
        return;
    }
    UnloadModel(L.model);
}

void resetGame(Game &g, int mapIndex) {
    unloadLevel(g.level);
    const bool night = g.night;
    const std::vector<KillEntry> feed = g.killFeed;
    g = Game{};
    g.night = night;
    g.killFeed = feed;
    g.mapIndex = mapIndex;
    g.owned[WeaponPistol] = true;
    g.owned[WeaponKnife] = true;
    g.owned[WeaponTaser] = true;
    g.mags[WeaponPistol] = Weapons[WeaponPistol].mag;
    g.intermission = randRange(IntermissionMin, IntermissionMax);
    Maps[mapIndex].build(g.level);
    finishLevel(g.level);
    g.cars = g.level.cars;
    g.pos = g.level.spawn;
    g.pos.y = groundAt(g.level.terrain, g.pos.x, g.pos.z);
    g.cam.up = {0.0f, 1.0f, 0.0f};
    g.cam.fovy = 70.0f;
    g.cam.projection = CAMERA_PERSPECTIVE;
    g.cam.position = {g.pos.x, g.pos.y + g.height - 0.15f, g.pos.z};
    for (const Vector3 &p : g.level.ballSpawns) {
        g.balls.push_back(Ball{p, Vector3{0.0f, 0.0f, 0.0f}});
    }
    sendMapToBrain(g);
    placeChests(g);
    for (int i = 0; i < AmmoBoxMax / 2; ++i) {
        spawnAmmoBox(g);
    }
}

// Offline death: respawn at spawn with the starting loadout, keeping the wave's kill money.
void respawnPlayer(Game &g) {
    g.money = StartMoney + g.roundKillMoney;
    g.health = PlayerMaxHealth;
    g.lastHealth = PlayerMaxHealth;
    g.armor = 0.0f;
    g.pos = g.level.spawn;
    g.pos.y = groundAt(g.level.terrain, g.pos.x, g.pos.z);
    g.height = StandHeight;
    g.vy = 0.0f;
    g.grounded = true;
    g.cam.position = {g.pos.x, g.pos.y + g.height - 0.15f, g.pos.z};
    for (int w = 0; w < WeaponCount; ++w) {
        g.owned[w] = false;
        g.mags[w] = 0;
    }
    g.owned[WeaponPistol] = true;
    g.owned[WeaponKnife] = true;
    g.owned[WeaponTaser] = true;
    g.mags[WeaponPistol] = Weapons[WeaponPistol].mag;
    g.weapon = WeaponPistol;
    g.sidearm = WeaponPistol;
    g.primary = -1;
    g.reserve = StartReserve;
    for (int t = 0; t < ThrowCount; ++t) {
        g.stock[t] = 0;
    }
    g.reloadTimer = 0.0f;
    g.holding = false;
    g.scoped = false;
    g.shopOpen = false;
    g.lastHit = "You were killed";
    g.hitTextTimer = 2.0f;
}

Vector3 carCenter(const Car &c) {
    return c.alongX ? Vector3{c.pos, 0.0f, c.lane} : Vector3{c.lane, 0.0f, c.pos};
}

Box carBox(const Car &c, const Terrain &t) {
    const Vector3 p = carCenter(c);
    const float hx = c.alongX ? CarLength / 2 : CarWidth / 2;
    const float hz = c.alongX ? CarWidth / 2 : CarLength / 2;
    const float y = groundAt(t, p.x, p.z);
    return Box{Vector3{p.x - hx, y, p.z - hz}, Vector3{p.x + hx, y + CarHeight, p.z + hz}};
}

void pushOutOf(Vector3 &pos, float height, const Box &b) {
    if (pos.y + height <= b.min.y + 0.05f || pos.y >= b.max.y - StepHeight) {
        return;
    }
    const float hx = (b.max.x - b.min.x) / 2.0f + PlayerRadius;
    const float hz = (b.max.z - b.min.z) / 2.0f + PlayerRadius;
    const float dx = pos.x - (b.min.x + b.max.x) / 2.0f;
    const float dz = pos.z - (b.min.z + b.max.z) / 2.0f;
    if (std::fabs(dx) < hx && std::fabs(dz) < hz) {
        const float penX = hx - std::fabs(dx);
        const float penZ = hz - std::fabs(dz);
        if (penX < penZ) {
            pos.x += (dx >= 0.0f ? penX : -penX);
        } else {
            pos.z += (dz >= 0.0f ? penZ : -penZ);
        }
    }
}

bool signalGreenX() {
    return std::fmod(GetTime(), static_cast<double>(SignalCycle)) < SignalCycle / 2.0;
}

void updateCars(Game &g, float dt) {
    const bool greenX = signalGreenX();
    for (Car &c : g.cars) {
        float speed = c.speed;
        if (speed > 0.0f) {
            const float next = c.dir > 0.0f ? std::ceil(c.pos / RoadSpacing) * RoadSpacing
                                            : std::floor(c.pos / RoadSpacing) * RoadSpacing;
            const float dist = std::fabs(next - c.pos);
            const bool red = c.alongX ? !greenX : greenX;
            if (red && dist > 3.0f && dist < 9.0f) {
                speed = 0.0f;
            }
        }
        c.pos += c.dir * speed * dt;
        if (c.pos > RoadSpan) {
            c.pos -= 2.0f * RoadSpan;
        } else if (c.pos < -RoadSpan) {
            c.pos += 2.0f * RoadSpan;
        }
    }
}

void updatePlayer(Game &g, float dt) {
    const bool crouching = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_C);
    const float targetHeight = crouching ? CrouchHeight : StandHeight;
    g.height += (targetHeight - g.height) * std::min(1.0f, dt * 12.0f);

    const Vector3 fwd = forwardOf(g);
    const Vector3 flat = Vector3Normalize(Vector3{fwd.x, 0.0f, fwd.z});
    const Vector3 right = Vector3Normalize(Vector3CrossProduct(flat, Vector3{0.0f, 1.0f, 0.0f}));

    const bool locked = playerFrozen(g);
    Vector3 move{0.0f, 0.0f, 0.0f};
    if (IsKeyDown(KEY_W)) move = Vector3Add(move, flat);
    if (IsKeyDown(KEY_S)) move = Vector3Subtract(move, flat);
    if (IsKeyDown(KEY_D)) move = Vector3Add(move, right);
    if (IsKeyDown(KEY_A)) move = Vector3Subtract(move, right);

    float speed = WalkSpeed;
    if (crouching) {
        speed = CrouchSpeed;
    } else if (IsKeyDown(KEY_LEFT_SHIFT)) {
        speed = SprintSpeed;
    }
    // Light loads run faster; a grenade in hand weighs 0.6 kg.
    const float load = g.holding ? 0.6f : Weapons[g.weapon].weight;
    speed *= std::clamp(1.0f + (2.5f - load) * 0.12f, 0.6f, 1.3f);

    g.playerMove = Vector3{0.0f, 0.0f, 0.0f};
    if (!locked && Vector3Length(move) > 0.0f) {
        move = Vector3Scale(Vector3Normalize(move), speed);
        g.playerMove = move;
        g.pos.x += move.x * dt;
        g.pos.z += move.z * dt;
    }

    for (const Box &b : g.level.solids) {
        pushOutOf(g.pos, g.height, b);
    }
    for (const Car &c : g.cars) {
        pushOutOf(g.pos, g.height, carBox(c, g.level.terrain));
    }

    float support = groundAt(g.level.terrain, g.pos.x, g.pos.z);
    for (const Box &b : g.level.solids) {
        const bool under = g.pos.x > b.min.x - 0.2f && g.pos.x < b.max.x + 0.2f &&
                           g.pos.z > b.min.z - 0.2f && g.pos.z < b.max.z + 0.2f;
        if (under && b.max.y <= g.pos.y + StepHeight && b.max.y > support) {
            support = b.max.y;
        }
    }
    if (g.grounded && !crouching && !locked && IsKeyPressed(KEY_SPACE)) {
        g.vy = JumpSpeed;
        g.grounded = false;
    }

    if (g.vy <= 0.0f && g.pos.y <= support + StepHeight) {
        g.pos.y = support;
        g.vy = 0.0f;
        g.grounded = true;
    } else {
        g.vy -= Gravity * dt;
        g.pos.y += g.vy * dt;
        g.grounded = false;
        if (g.vy <= 0.0f && g.pos.y <= support) {
            g.pos.y = support;
            g.vy = 0.0f;
            g.grounded = true;
        }
    }
}

void kickBall(Game &g) {
    if (g.kickTimer > 0.0f) {
        return;
    }
    const Vector3 fwd = forwardOf(g);
    const Vector3 flat = Vector3Normalize(Vector3{fwd.x, 0.0f, fwd.z});
    for (Ball &b : g.balls) {
        const Vector3 d{b.pos.x - g.pos.x, 0.0f, b.pos.z - g.pos.z};
        if (Vector3Length(d) > PlayerRadius + BallRadius + 0.8f || std::fabs(b.pos.y - g.pos.y) > g.height) {
            continue;
        }
        if (Vector3DotProduct(Vector3Normalize(d), flat) < 0.2f) {
            continue;
        }
        b.vel = Vector3Add(Vector3Scale(flat, KickSpeed), Vector3{0.0f, 3.0f, 0.0f});
        g.kickTimer = KickCooldown;
        return;
    }
}

void collideBall(Ball &b, const Box &box) {
    const Vector3 p = b.pos;
    const Vector3 cp{std::clamp(p.x, box.min.x, box.max.x), std::clamp(p.y, box.min.y, box.max.y),
                     std::clamp(p.z, box.min.z, box.max.z)};
    Vector3 n = Vector3Subtract(p, cp);
    const float dist = Vector3Length(n);
    if (dist >= BallRadius) {
        return;
    }
    if (dist > 1e-4f) {
        n = Vector3Scale(n, 1.0f / dist);
        b.pos = Vector3Add(cp, Vector3Scale(n, BallRadius));
    } else {
        const float faces[] = {box.max.x - p.x, p.y - box.min.y, box.max.y - p.y, p.z - box.min.z, box.max.z - p.z};
        const Vector3 dirs[] = {Vector3{1.0f, 0.0f, 0.0f}, Vector3{0.0f, -1.0f, 0.0f}, Vector3{0.0f, 1.0f, 0.0f},
                                Vector3{0.0f, 0.0f, -1.0f}, Vector3{0.0f, 0.0f, 1.0f}};
        float best = p.x - box.min.x;
        n = Vector3{-1.0f, 0.0f, 0.0f};
        for (int i = 0; i < 5; ++i) {
            if (faces[i] < best) {
                best = faces[i];
                n = dirs[i];
            }
        }
        b.pos = Vector3Add(p, Vector3Scale(n, best + BallRadius));
    }
    const float vn = Vector3DotProduct(b.vel, n);
    if (vn < 0.0f) {
        b.vel = Vector3Subtract(b.vel, Vector3Scale(n, (1.0f + BallBounce) * vn));
    }
}

void stepBall(const Level &L, Ball &b, float h) {
    b.vel.y -= BallGravity * h;
    b.pos = Vector3Add(b.pos, Vector3Scale(b.vel, h));
    for (const Box &box : L.solids) {
        collideBall(b, box);
    }
    const float ground = groundAt(L.terrain, b.pos.x, b.pos.z);
    if (b.pos.y - BallRadius < ground) {
        b.pos.y = ground + BallRadius;
        if (b.vel.y < 0.0f) {
            b.vel.y = std::fabs(b.vel.y) < 0.8f ? 0.0f : -b.vel.y * BallBounce;
        }
        const float e = 0.3f;
        const float gx = (groundAt(L.terrain, b.pos.x + e, b.pos.z) - groundAt(L.terrain, b.pos.x - e, b.pos.z)) / (2.0f * e);
        const float gz = (groundAt(L.terrain, b.pos.x, b.pos.z + e) - groundAt(L.terrain, b.pos.x, b.pos.z - e)) / (2.0f * e);
        b.vel.x -= gx * SlopeGravity * h;
        b.vel.z -= gz * SlopeGravity * h;
        const float drag = std::exp(-RollDrag * h);
        b.vel.x *= drag;
        b.vel.z *= drag;
    }
}

void updateBalls(Game &g, float dt) {
    constexpr int Steps = 4;
    const float reach = PlayerRadius + BallRadius;
    for (Ball &b : g.balls) {
        const Vector3 d{b.pos.x - g.pos.x, 0.0f, b.pos.z - g.pos.z};
        const float dist = Vector3Length(d);
        if (dist > 0.0f && dist < reach && std::fabs(b.pos.y - g.pos.y) < g.height) {
            const Vector3 n = Vector3Scale(d, 1.0f / dist);
            b.pos = Vector3Add(b.pos, Vector3Scale(n, reach - dist));
            const float target = std::max(Vector3DotProduct(g.playerMove, n), 0.0f);
            const float vn = Vector3DotProduct(b.vel, n);
            if (vn < target) {
                b.vel = Vector3Add(b.vel, Vector3Scale(n, target - vn));
            }
        }
        for (int s = 0; s < Steps; ++s) {
            stepBall(g.level, b, dt / Steps);
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

struct ShotProfile {
    float thumpHz;     // body pitch: higher sounds sharper
    float crackDecay;  // higher is a shorter, sharper crack
    float boom;        // low thump level
    float tail;        // noise tail level
    float length;      // seconds
};

// Per weapon, in Weapon enum order.
constexpr ShotProfile ShotProfiles[WeaponCount] = {
    {180.0f, 900.0f, 0.4f, 0.35f, 0.22f},   // pistol
    {160.0f, 900.0f, 0.4f, 0.35f, 0.22f},   // knife (unused)
    {110.0f, 600.0f, 0.8f, 0.6f, 0.35f},    // AK-47
    {220.0f, 1000.0f, 0.3f, 0.25f, 0.18f},  // SMG
    {70.0f, 500.0f, 1.2f, 0.9f, 0.5f},      // AWP
    {240.0f, 1100.0f, 0.25f, 0.2f, 0.15f},  // Mac-10
    {150.0f, 750.0f, 0.5f, 0.4f, 0.26f},    // M4A1
    {95.0f, 550.0f, 1.0f, 0.7f, 0.4f},      // Deagle
    {200.0f, 950.0f, 0.3f, 0.3f, 0.17f},    // P90
    {85.0f, 480.0f, 0.9f, 0.8f, 0.45f},     // Scout
    {200.0f, 850.0f, 0.35f, 0.3f, 0.2f},    // Glock-18
    {150.0f, 700.0f, 0.6f, 0.5f, 0.25f},    // USP-S
    {210.0f, 950.0f, 0.3f, 0.25f, 0.18f},   // P250
    {230.0f, 1000.0f, 0.25f, 0.2f, 0.16f},  // Five-SeveN
    {90.0f, 520.0f, 1.1f, 0.8f, 0.42f},     // R8 Revolver
    {170.0f, 850.0f, 0.35f, 0.3f, 0.2f},    // FAMAS
    {140.0f, 700.0f, 0.5f, 0.4f, 0.24f},    // Galil AR
    {200.0f, 900.0f, 0.3f, 0.3f, 0.18f},    // MP5
    {100.0f, 500.0f, 1.1f, 0.9f, 0.4f},     // Nova
    {120.0f, 600.0f, 0.7f, 0.6f, 0.3f},     // M249
    {900.0f, 1400.0f, 0.2f, 0.5f, 0.12f},   // Taser
};

Sound makeShotSound(const ShotProfile &p, unsigned int seed) {
    constexpr unsigned int rate = 44100;
    const int frames = static_cast<int>(rate * p.length);
    std::vector<short> samples(frames);
    unsigned int rng = seed;
    float prev = 0.0f;
    float lp = 0.0f;
    for (int i = 0; i < frames; ++i) {
        const float t = i / static_cast<float>(rate);
        rng = rng * 1664525u + 1013904223u;
        const float noise = (rng >> 16) / 32768.0f - 1.0f;
        const float bright = noise - prev;
        prev = noise;
        lp += (noise - lp) * 0.12f;
        const float crack = bright * std::exp(-t * p.crackDecay) * 1.1f;
        const float body = std::sin(2.0f * Pi * p.thumpHz * t) * std::exp(-t * 18.0f) * 0.9f;
        const float boom = std::sin(2.0f * Pi * 55.0f * t) * std::exp(-t * 9.0f) * p.boom;
        const float tail = lp * std::exp(-t * 20.0f) * p.tail;
        const float s = std::tanh((crack + body + boom + tail) * 1.3f);
        samples[i] = static_cast<short>(s * 30000.0f);
    }
    Wave wave{};
    wave.frameCount = static_cast<unsigned int>(frames);
    wave.sampleRate = rate;
    wave.sampleSize = 16;
    wave.channels = 1;
    wave.data = samples.data();
    return LoadSoundFromWave(wave);
}

void playShot(Audio &a, int weapon) {
    if (!a.ready) {
        return;
    }
    PlaySound(a.shots[weapon][a.nextShot]);
    a.nextShot = (a.nextShot + 1) % ShotSoundCount;
}

Sound makeStepSound(unsigned int seed) {
    constexpr unsigned int rate = 22050;
    const int frames = static_cast<int>(rate * 0.16f);
    std::vector<short> samples(frames);
    unsigned int s = seed;
    auto rnd = [&s]() {
        s = s * 1664525u + 1013904223u;
        return (s >> 16) / 32768.0f - 1.0f;
    };
    // Each step gets its own thud pitch so repeats don't sound identical.
    const float thudHz = 60.0f + 35.0f * (rnd() * 0.5f + 0.5f);
    float lp = 0.0f;
    float rumble = 0.0f;
    for (int i = 0; i < frames; ++i) {
        const float t = i / static_cast<float>(rate);
        const float noise = rnd();
        lp += (noise - lp) * 0.25f;
        rumble += (noise - rumble) * 0.03f;
        const float scuff = (lp - rumble) * std::exp(-t * 45.0f) * 0.6f;
        const float thud = std::sin(2.0f * Pi * thudHz * t) * std::exp(-t * 28.0f);
        const float click = noise * std::exp(-t * 400.0f) * 0.5f;
        samples[i] = static_cast<short>(std::tanh((thud + scuff + click) * 1.2f) * 20000.0f);
    }
    Wave wave{};
    wave.frameCount = static_cast<unsigned int>(frames);
    wave.sampleRate = rate;
    wave.sampleSize = 16;
    wave.channels = 1;
    wave.data = samples.data();
    return LoadSoundFromWave(wave);
}

// Quiet while music plays, loud when muted.
void playStep(Audio &a) {
    if (!a.ready) {
        return;
    }
    SetSoundVolume(a.steps[a.nextStep], a.playlist.muted ? 0.8f : 0.04f);
    PlaySound(a.steps[a.nextStep]);
    a.nextStep = (a.nextStep + 1) % StepSoundCount;
}

// Shows the name of whatever is in hand for two seconds.
void announceHeld(Game &g) {
    if (g.holding) {
        g.holdText = std::string("Holding: ") + ThrowNames[g.throwSel];
    } else {
        g.holdText = TextFormat("Holding: %s  (%.1f kg)", Weapons[g.weapon].name, Weapons[g.weapon].weight);
    }
    g.holdTimer = 2.0f;
}

bool smoked(const Game &g, Vector3 a, Vector3 b);

void shoot(Game &g, Audio &a) {
    int &mag = activeMag(g);
    if (g.holding || g.fireTimer > 0.0f || g.reloadTimer > 0.0f || mag <= 0) {
        return;
    }
    --mag;
    g.fireTimer = g.rapid && Weapons[g.weapon].autoFire ? Weapons[g.weapon].autoCooldown : Weapons[g.weapon].semiCooldown;
    g.kick = 1.0f;
    g.muzzleTimer = MuzzleTime;
    g.fireGlow = FireTime;
    ++g.shots;
    playShot(a, g.weapon);
    g.noisePos = g.pos;
    g.noiseTimer = NoiseMemory;

    const Basis b = gunBasis(g);
    const Vector3 muzzle = muzzlePoint(b);
    const Ray ray{g.cam.position, b.fwd};

    float wallDist = MaxShotDist;
    for (const Box &w : g.level.solids) {
        const RayCollision rc = GetRayCollisionBox(ray, BoundingBox{w.min, w.max});
        if (rc.hit && rc.distance > 0.0f && rc.distance < wallDist) {
            wallDist = rc.distance;
        }
    }
    wallDist = std::min(wallDist, terrainHitDist(g.level.terrain, ray, wallDist));

    Prop *tvHit = nullptr;
    for (Prop &p : g.level.props) {
        if (p.kind != PropKind::Tv || p.broken) {
            continue;
        }
        const BoundingBox box{Vector3{p.base.x - p.w / 2, p.base.y, p.base.z - p.d / 2},
                              Vector3{p.base.x + p.w / 2, p.base.y + p.h, p.base.z + p.d / 2}};
        const RayCollision rc = GetRayCollisionBox(ray, box);
        if (rc.hit && rc.distance > 0.0f && rc.distance <= wallDist + 1e-3f) {
            tvHit = &p;
            break;
        }
    }

    int hitIndex = -1;
    float hitDist = wallDist;
    Target hit{};
    for (int i = 0; i < static_cast<int>(g.enemies.size()); ++i) {
        if (!hostile(g.localTeam, g.enemies[i].team)) {
            continue;
        }
        for (const Target &t : targetsOf(g.enemies[i])) {
            const RayCollision rc = GetRayCollisionSphere(ray, t.center, t.radius);
            if (rc.hit && rc.distance < hitDist) {
                hitDist = rc.distance;
                hitIndex = i;
                hit = t;
            }
        }
    }

    if (tvHit && hitIndex < 0) {
        if (a.ready) {
            SetSoundVolume(a.crack, 0.9f);
            SetSoundPan(a.crack, 0.5f);
            PlaySound(a.crack);
        }
        if (++tvHit->damage >= TvHits) {
            tvHit->broken = true;
        }
    }

    if (hitIndex >= 0) {
        if (a.ready) {
            SetSoundVolume(a.hitMarker, 0.6f);
            SetSoundPan(a.hitMarker, 0.5f);
            PlaySound(a.hitMarker);
        }
        Enemy &victim = g.enemies[hitIndex];
        ++g.hits;
        g.money += hit.points;
        g.roundKillMoney += hit.points;
        g.lastHit = std::string(hit.name) + " +" + std::to_string(hit.points);
        g.hitTextTimer = 1.0f;
        if (victim.netId != 0) {
            // Remote players take damage on their own client; the server relays it and reports deaths.
            const bool head = std::string(hit.name) == "HEAD";
            g.netOut.push_back(hitLine(victim.netId, Weapons[g.weapon].playerDamage * (head ? 2.0f : 1.0f), head, g.weapon));
        } else {
            victim.hp -= Weapons[g.weapon].hits;
            if (victim.hp <= 0) {
                g.freshKill = true;
                addKill(g, KillEntry{"You", GOLD, Weapons[g.weapon].name, botName(victim), teamColor(victim.team),
                                     std::string(hit.name) == "HEAD", victim.blind > 0.0f,
                                     smoked(g, g.cam.position, hit.center)});
                dropEnemyGun(g, victim);
                addCorpse(g, makeCorpse(victim, b.fwd));
                g.enemies.erase(g.enemies.begin() + hitIndex);
            }
        }
        if (!gModsLocked) {
            for (const ModHitFn hook : modHitHooks()) {
                hook(Vector3Add(g.cam.position, Vector3Scale(b.fwd, hitDist)));
            }
        }
    }

    g.tracerFrom = muzzle;
    g.tracerTo = Vector3Add(g.cam.position, Vector3Scale(b.fwd, hitDist));
    g.tracerTimer = TracerTime;
    g.pitch = std::min(g.pitch + 0.01f, 1.5f);
}

float wallDistAlong(const Level &L, Vector3 from, Vector3 dir, float maxD);

// Taser: a hitscan shot that stuns the first hostile in line of fire. Mouse button 8 selects it.
constexpr int TaserMouseButton = 7;  // raylib counts mouse buttons from 0, so button 8 is 7
constexpr float TaserRange = 14.0f;
constexpr float TaserStunTime = 4.0f;

void zap(Game &g, Audio &a) {
    if (g.fireTimer > 0.0f) {
        return;
    }
    g.fireTimer = Weapons[WeaponTaser].semiCooldown;
    g.kick = 1.0f;
    g.muzzleTimer = MuzzleTime;
    ++g.shots;
    playShot(a, WeaponTaser);
    g.noisePos = g.pos;
    g.noiseTimer = NoiseMemory;

    const Basis b = gunBasis(g);
    const Ray ray{g.cam.position, b.fwd};
    const float wallDist = wallDistAlong(g.level, g.cam.position, b.fwd, TaserRange);
    int best = -1;
    float bestDist = wallDist;
    for (int i = 0; i < static_cast<int>(g.enemies.size()); ++i) {
        const Enemy &e = g.enemies[static_cast<size_t>(i)];
        if (e.hp <= 0 || e.netId != 0 || !hostile(g.localTeam, e.team)) {
            continue;
        }
        const RayCollision rc = GetRayCollisionSphere(ray, enemyPoint(e, 0.0f, 0.9f), TorsoRadius);
        if (rc.hit && rc.distance > 0.0f && rc.distance < bestDist) {
            bestDist = rc.distance;
            best = i;
        }
    }
    g.tracerFrom = muzzlePoint(b);
    g.tracerTo = Vector3Add(g.cam.position, Vector3Scale(b.fwd, best >= 0 ? bestDist : wallDist));
    g.tracerTimer = TracerTime;
    if (best >= 0) {
        if (a.ready) {
            SetSoundVolume(a.hitMarker, 0.6f);
            SetSoundPan(a.hitMarker, 0.5f);
            PlaySound(a.hitMarker);
        }
        g.enemies[static_cast<size_t>(best)].stun = TaserStunTime;
        ++g.hits;
        g.lastHit = "Taser stun";
        g.hitTextTimer = 1.0f;
    }
}

// Knife swing: kills the nearest hostile within reach and cone in front of the player.
constexpr float SaberSwingTime = 0.35f;  // seconds for one lightsaber arc

// True when the lightsaber knife is the one in hand.
bool saberHeld(const Game &g) {
    return g.weapon == WeaponKnife && !g.holding && gInv.equipKnife == KnifeLightsaber;
}

void stab(Game &g) {
    if (g.fireTimer > 0.0f) {
        return;
    }
    g.fireTimer = KnifeCooldown;
    g.kick = 1.0f;
    const Vector3 fwd = forwardOf(g);
    int best = -1;
    float bestDist = KnifeRange;
    for (int i = 0; i < static_cast<int>(g.enemies.size()); ++i) {
        const Enemy &e = g.enemies[static_cast<size_t>(i)];
        if (e.hp <= 0 || !hostile(g.localTeam, e.team)) {
            continue;
        }
        const Vector3 to = Vector3Subtract(Vector3{e.pos.x, e.pos.y + 1.0f, e.pos.z}, g.cam.position);
        const float d = Vector3Length(to);
        if (d < bestDist && Vector3DotProduct(to, fwd) >= KnifeCone * d) {
            best = i;
            bestDist = d;
        }
    }
    if (best < 0) {
        return;
    }
    Enemy &victim = g.enemies[static_cast<size_t>(best)];
    if (victim.netId != 0) {
        g.netOut.push_back(hitLine(victim.netId, PlayerMaxHealth, false, WeaponKnife));
        ++g.hits;
        g.lastHit = "Knife";
        g.hitTextTimer = 1.0f;
        return;
    }
    g.freshKill = true;
    addKill(g, KillEntry{"You", GOLD, "Knife", botName(victim), teamColor(victim.team), false,
                         victim.blind > 0.0f,
                         smoked(g, g.cam.position, Vector3{victim.pos.x, victim.pos.y + 1.0f, victim.pos.z})});
    victim.hp = 0;
    ++g.hits;
    g.money += KnifePoints;
    g.roundKillMoney += KnifePoints;
    g.lastHit = "Knife +" + std::to_string(KnifePoints);
    g.hitTextTimer = 1.0f;
}

short toPcm(float s) {
    return static_cast<short>(std::clamp(s, -1.0f, 1.0f) * 30000.0f);
}

Sound toSound(std::vector<short> &samples, unsigned int rate) {
    Wave wave{};
    wave.frameCount = static_cast<unsigned int>(samples.size());
    wave.sampleRate = rate;
    wave.sampleSize = 16;
    wave.channels = 1;
    wave.data = samples.data();
    return LoadSoundFromWave(wave);
}

// Two short syllables sweeping from lo to hi Hz.
Sound makeChirp(float lo, float hi) {
    constexpr unsigned int rate = 22050;
    std::vector<short> samples(static_cast<size_t>(rate * 0.2f));
    float phase = 0.0f;
    for (size_t i = 0; i < samples.size(); ++i) {
        const float t = i / static_cast<float>(rate);
        const bool note = t < 0.07f || (t >= 0.1f && t < 0.17f);
        if (!note) {
            samples[i] = 0;
            continue;
        }
        const float local = t < 0.1f ? t : t - 0.1f;
        const float f = lo + (hi - lo) * (local / 0.07f) + 250.0f * std::sin(2.0f * Pi * 35.0f * t);
        phase += 2.0f * Pi * f / rate;
        samples[i] = toPcm(std::sin(phase) * std::sin(Pi * local / 0.07f) * 0.6f);
    }
    return toSound(samples, rate);
}

// Insect click train, quiet and short.
Sound makeBug() {
    constexpr unsigned int rate = 22050;
    std::vector<short> samples(static_cast<size_t>(rate * 0.5f));
    for (size_t i = 0; i < samples.size(); ++i) {
        const float t = i / static_cast<float>(rate);
        const float env = std::sin(Pi * t / 0.5f);
        const float pulse = std::sin(2.0f * Pi * 24.0f * t) > 0.5f ? 1.0f : 0.0f;
        samples[i] = toPcm(std::sin(2.0f * Pi * 3300.0f * t) * pulse * env * 0.5f);
    }
    return toSound(samples, rate);
}

// Wing beats: filtered noise pulsing about 9 times per second.
Sound makeFlap() {
    constexpr unsigned int rate = 22050;
    const float len = 0.9f;
    std::vector<short> samples(static_cast<size_t>(rate * len));
    unsigned int seed = 777u;
    float lp = 0.0f;
    for (size_t i = 0; i < samples.size(); ++i) {
        const float t = i / static_cast<float>(rate);
        seed = seed * 1664525u + 1013904223u;
        const float noise = (seed >> 16) / 32768.0f - 1.0f;
        lp += (noise - lp) * 0.25f;
        const float beat = std::pow(std::max(0.0f, std::sin(2.0f * Pi * 9.0f * t)), 4.0f);
        samples[i] = toPcm(lp * beat * std::sin(Pi * t / len) * 1.5f);
    }
    return toSound(samples, rate);
}

// Seamless 6 second wind bed: low-pass noise with a slow swell and faded ends.
Sound makeWind() {
    constexpr unsigned int rate = 22050;
    const float len = 6.0f;
    std::vector<short> samples(static_cast<size_t>(rate * len));
    unsigned int seed = 4242u;
    float lp = 0.0f;
    for (size_t i = 0; i < samples.size(); ++i) {
        const float t = i / static_cast<float>(rate);
        seed = seed * 1664525u + 1013904223u;
        const float noise = (seed >> 16) / 32768.0f - 1.0f;
        lp += (noise - lp) * 0.02f;
        const float swell = 0.6f + 0.4f * std::sin(2.0f * Pi * t / len);
        const float fade = std::min(1.0f, std::min(t, len - t) / 0.5f);
        samples[i] = toPcm(lp * swell * fade * 6.0f);
    }
    return toSound(samples, rate);
}

// Short body-hit thump with a noise crack.
Sound makeHurt() {
    constexpr unsigned int rate = 22050;
    const float len = 0.3f;
    std::vector<short> samples(static_cast<size_t>(rate * len));
    unsigned int seed = 9001u;
    float phase = 0.0f;
    for (size_t i = 0; i < samples.size(); ++i) {
        const float t = i / static_cast<float>(rate);
        seed = seed * 1664525u + 1013904223u;
        const float noise = (seed >> 16) / 32768.0f - 1.0f;
        const float freq = 180.0f - 120.0f * (t / len);
        phase += 2.0f * Pi * freq / rate;
        const float env = std::exp(-t * 14.0f);
        samples[i] = toPcm((std::sin(phase) * 0.8f + noise * 0.4f * std::exp(-t * 60.0f)) * env);
    }
    return toSound(samples, rate);
}

// Short sharp tick played when a bullet hits an enemy.
Sound makeHitMarker() {
    constexpr unsigned int rate = 22050;
    const float len = 0.09f;
    std::vector<short> samples(static_cast<size_t>(rate * len));
    for (size_t i = 0; i < samples.size(); ++i) {
        const float t = i / static_cast<float>(rate);
        const float env = std::exp(-t * 40.0f);
        samples[i] = toPcm((std::sin(2.0f * Pi * 1800.0f * t) * 0.6f + std::sin(2.0f * Pi * 2700.0f * t) * 0.3f) * env);
    }
    return toSound(samples, rate);
}

// Glass-like crack for shots into a TV: noise snap, high ring and low rumble.
Sound makeCrack() {
    constexpr unsigned int rate = 22050;
    const float len = 0.45f;
    std::vector<short> samples(static_cast<size_t>(rate * len));
    unsigned int seed = 4242u;
    for (size_t i = 0; i < samples.size(); ++i) {
        const float t = i / static_cast<float>(rate);
        seed = seed * 1664525u + 1013904223u;
        const float noise = (seed >> 16) / 32768.0f - 1.0f;
        const float snap = noise * std::exp(-t * 35.0f) * 0.7f;
        const float ring = std::sin(2.0f * Pi * 2400.0f * t) * std::exp(-t * 22.0f) * 0.35f;
        const float rumble = std::sin(2.0f * Pi * 140.0f * t) * std::exp(-t * 9.0f) * 0.4f;
        samples[i] = toPcm(snap + ring + rumble);
    }
    return toSound(samples, rate);
}

// Two-tone ding for confirming a kill.
Sound makeKill() {
    constexpr unsigned int rate = 22050;
    std::vector<short> samples(static_cast<size_t>(rate * 0.35f));
    for (size_t i = 0; i < samples.size(); ++i) {
        const float t = i / static_cast<float>(rate);
        const float tone = std::sin(2.0f * Pi * 1320.0f * t) * 0.5f +
                           std::sin(2.0f * Pi * 1980.0f * t) * 0.35f * std::exp(-t * 12.0f);
        samples[i] = toPcm(tone * std::exp(-t * 8.0f) * 0.7f);
    }
    return toSound(samples, rate);
}

// Explosion: a falling sub thump over a noisy rumble with a short crack.
Sound makeBoom() {
    constexpr unsigned int rate = 22050;
    std::vector<short> samples(static_cast<size_t>(rate * 1.2f));
    unsigned int seed = 31337u;
    float lp = 0.0f;
    float phase = 0.0f;
    for (size_t i = 0; i < samples.size(); ++i) {
        const float t = i / static_cast<float>(rate);
        seed = seed * 1664525u + 1013904223u;
        const float noise = (seed >> 16) / 32768.0f - 1.0f;
        lp += (noise - lp) * 0.08f;
        phase += 2.0f * Pi * (35.0f + 70.0f * std::exp(-t * 9.0f)) / rate;
        const float sub = std::sin(phase) * std::exp(-t * 4.0f) * 0.9f;
        const float rumble = lp * std::exp(-t * 3.0f) * 2.0f;
        const float crack = noise * std::exp(-t * 60.0f) * 0.5f;
        samples[i] = toPcm(sub + rumble + crack);
    }
    return toSound(samples, rate);
}

// Flashbang: a bright crack with a short ring.
Sound makeBang() {
    constexpr unsigned int rate = 22050;
    std::vector<short> samples(static_cast<size_t>(rate * 0.7f));
    unsigned int seed = 2020u;
    for (size_t i = 0; i < samples.size(); ++i) {
        const float t = i / static_cast<float>(rate);
        seed = seed * 1664525u + 1013904223u;
        const float noise = (seed >> 16) / 32768.0f - 1.0f;
        const float crack = noise * std::exp(-t * 25.0f) * 0.9f;
        const float ring = std::sin(2.0f * Pi * 3900.0f * t) * std::exp(-t * 5.0f) * 0.4f;
        samples[i] = toPcm(crack + ring);
    }
    return toSound(samples, rate);
}

// Tinnitus whine for a player blinded by a flashbang.
Sound makeRing() {
    constexpr unsigned int rate = 22050;
    const float len = 2.5f;
    std::vector<short> samples(static_cast<size_t>(rate * len));
    for (size_t i = 0; i < samples.size(); ++i) {
        const float t = i / static_cast<float>(rate);
        const float wobble = 0.6f + 0.4f * std::sin(2.0f * Pi * 2.0f * t);
        const float fade = std::min(1.0f, std::min(t, len - t) / 0.4f);
        const float tone = std::sin(2.0f * Pi * 4100.0f * t) * 0.3f + std::sin(2.0f * Pi * 4700.0f * t) * 0.15f;
        samples[i] = toPcm(tone * wobble * fade);
    }
    return toSound(samples, rate);
}

// Smoke canister: high-passed noise with a quick swell and slow fade.
Sound makeHiss() {
    constexpr unsigned int rate = 22050;
    std::vector<short> samples(static_cast<size_t>(rate * 2.0f));
    unsigned int seed = 5150u;
    float lp = 0.0f;
    for (size_t i = 0; i < samples.size(); ++i) {
        const float t = i / static_cast<float>(rate);
        seed = seed * 1664525u + 1013904223u;
        const float noise = (seed >> 16) / 32768.0f - 1.0f;
        lp += (noise - lp) * 0.15f;
        const float env = std::min(1.0f, t / 0.15f) * std::exp(-t * 1.6f);
        samples[i] = toPcm((noise - lp) * env * 1.8f);
    }
    return toSound(samples, rate);
}

// Cash register: two quick ticks.
Sound makeCash() {
    constexpr unsigned int rate = 22050;
    std::vector<short> samples(static_cast<size_t>(rate * 0.3f));
    for (size_t i = 0; i < samples.size(); ++i) {
        const float t = i / static_cast<float>(rate);
        float s = std::sin(2.0f * Pi * 1500.0f * t) * std::exp(-t * 45.0f) * 0.6f;
        const float t2 = t - 0.07f;
        if (t2 > 0.0f) {
            s += std::sin(2.0f * Pi * 2300.0f * t2) * std::exp(-t2 * 40.0f) * 0.7f;
        }
        samples[i] = toPcm(s);
    }
    return toSound(samples, rate);
}

// Metal clink for buying armor: inharmonic partials that ring out.
Sound makeClink() {
    constexpr unsigned int rate = 22050;
    std::vector<short> samples(static_cast<size_t>(rate * 0.6f));
    for (size_t i = 0; i < samples.size(); ++i) {
        const float t = i / static_cast<float>(rate);
        const float ring = std::sin(2.0f * Pi * 2100.0f * t) * 0.35f + std::sin(2.0f * Pi * 3170.0f * t) * 0.25f +
                           std::sin(2.0f * Pi * 4520.0f * t) * 0.15f;
        samples[i] = toPcm(ring * std::exp(-t * 9.0f) * 0.7f);
    }
    return toSound(samples, rate);
}

// Short click for the case reel.
Sound makeTick() {
    constexpr unsigned int rate = 22050;
    std::vector<short> samples(static_cast<size_t>(rate * 0.025f));
    for (size_t i = 0; i < samples.size(); ++i) {
        const float t = i / static_cast<float>(rate);
        const float s = (std::sin(2.0f * Pi * 2600.0f * t) * 0.6f + std::sin(2.0f * Pi * 4100.0f * t) * 0.3f) *
                        std::exp(-t * 260.0f);
        samples[i] = toPcm(s);
    }
    return toSound(samples, rate);
}

// Lightsaber idle hum: exactly one second of sawtooth-ish drone with a 5 Hz wobble, so it loops without a seam.
Sound makeSaberHum() {
    constexpr unsigned int rate = 22050;
    std::vector<short> samples(rate);
    for (size_t i = 0; i < samples.size(); ++i) {
        const float t = i / static_cast<float>(rate);
        const float drone = std::sin(2.0f * Pi * 110.0f * t) + 0.5f * std::sin(2.0f * Pi * 220.0f * t) +
                            0.25f * std::sin(2.0f * Pi * 330.0f * t);
        const float wobble = 1.0f + 0.12f * std::sin(2.0f * Pi * 5.0f * t);
        samples[i] = toPcm(drone * wobble * 0.22f);
    }
    return toSound(samples, rate);
}

// Ignite: a rising hum sweep with a short flare at the start.
Sound makeSaberIgnite() {
    constexpr unsigned int rate = 22050;
    const float len = 0.7f;
    std::vector<short> samples(static_cast<size_t>(rate * len));
    float phase = 0.0f;
    for (size_t i = 0; i < samples.size(); ++i) {
        const float t = i / static_cast<float>(rate);
        const float f = 90.0f + 320.0f * std::pow(t / len, 1.5f);
        phase += 2.0f * Pi * f / static_cast<float>(rate);
        const float env = std::min(1.0f, t / 0.05f) * (1.0f - t / len);
        const float tone = std::sin(phase) + 0.4f * std::sin(2.0f * phase);
        samples[i] = toPcm(tone * env * 0.35f);
    }
    return toSound(samples, rate);
}

// Swing whoosh: filtered noise with a falling pitch, peaking in the middle of the arc.
Sound makeSaberSwing() {
    constexpr unsigned int rate = 22050;
    const float len = 0.35f;
    std::vector<short> samples(static_cast<size_t>(rate * len));
    unsigned int seed = 4242u;
    float lp = 0.0f;
    float phase = 0.0f;
    for (size_t i = 0; i < samples.size(); ++i) {
        const float t = i / static_cast<float>(rate);
        seed = seed * 1664525u + 1013904223u;
        const float noise = static_cast<float>(seed >> 8) / static_cast<float>(1u << 24) * 2.0f - 1.0f;
        lp += 0.12f * (noise - lp);
        const float f = 420.0f - 180.0f * (t / len);
        phase += 2.0f * Pi * f / static_cast<float>(rate);
        const float env = std::sin(Pi * t / len);
        samples[i] = toPcm((lp * 1.6f + 0.15f * std::sin(phase)) * env * 0.5f);
    }
    return toSound(samples, rate);
}

Sound makeSting(float root, int notes) {
    constexpr unsigned int rate = 22050;
    constexpr float step = 0.11f;
    constexpr float ratios[] = {1.0f, 1.25f, 1.5f, 2.0f};
    const float len = step * static_cast<float>(notes) + 0.3f;
    std::vector<short> samples(static_cast<size_t>(rate * len));
    for (size_t i = 0; i < samples.size(); ++i) {
        const float t = i / static_cast<float>(rate);
        const int n = std::min(static_cast<int>(t / step), notes - 1);
        const float local = t - static_cast<float>(n) * step;
        const float f = root * ratios[n];
        const float tone = std::sin(2.0f * Pi * f * local) + 0.3f * std::sin(4.0f * Pi * f * local);
        samples[i] = toPcm(tone * std::exp(-local * 7.0f) * 0.45f);
    }
    return toSound(samples, rate);
}

float randPan() {
    return randRange(0.2f, 0.8f);
}

void playAt(Sound s, float volume, float pan) {
    SetSoundVolume(s, volume);
    SetSoundPan(s, pan);
    PlaySound(s);
}

// Plays a world sound: quieter with distance and panned by its side of the camera.
void playSpatial(const Game &g, const Audio &a, Sound s, Vector3 at, float volume, float range) {
    if (!a.ready) {
        return;
    }
    const Vector3 off = Vector3Subtract(at, g.cam.position);
    const float dist = Vector3Length(off);
    if (dist > range) {
        return;
    }
    const Vector3 right = Vector3CrossProduct(forwardOf(g), Vector3{0.0f, 1.0f, 0.0f});
    const float side = dist > 0.0f ? Vector3DotProduct(off, right) / dist : 0.0f;
    playAt(s, (1.0f - dist / range) * volume, 0.5f + 0.5f * side);
}

// Kill confirmation: KILL popup and a ding.
void onKill(Game &g, const Audio &a) {
    g.killFlash = KillFlashTime;
    if (a.ready) {
        playAt(a.kill, 0.5f, 0.5f);
    }
}

void onPlayerHurt(Game &g, const Audio &a) {
    if (a.ready) {
        playAt(a.hurt, 0.8f, 0.5f);
    }
    g.hurtTimer = HurtFlashTime;
    g.shake = std::max(g.shake, 0.6f);
    for (int i = 0; i < 4; ++i) {
        g.splats.push_back(BloodSplat{randRange(0.1f, 0.9f), randRange(0.1f, 0.9f), randRange(16.0f, 36.0f), 0.0f});
    }
    if (g.splats.size() > MaxSplats) {
        g.splats.erase(g.splats.begin(), g.splats.begin() + static_cast<long>(g.splats.size() - MaxSplats));
    }
}

void spawnFlock(Game &g) {
    std::vector<Vector3> roofs;
    for (const Prop &p : g.level.props) {
        if (p.kind == PropKind::House) {
            roofs.push_back(Vector3{p.base.x, p.base.y + p.h, p.base.z});
        }
    }
    const bool overHouse = !roofs.empty() && GetRandomValue(0, 1) == 0;
    const float half = g.level.terrain.half;
    Vector3 c{randRange(-half * 0.5f, half * 0.5f), randRange(18.0f, 26.0f), randRange(-half * 0.5f, half * 0.5f)};
    if (overHouse) {
        c = roofs[GetRandomValue(0, static_cast<int>(roofs.size()) - 1)];
    }
    const float a = randRange(0.0f, 2.0f * Pi);
    const Vector3 vel{std::cos(a) * 8.0f, 0.0f, std::sin(a) * 8.0f};
    const int count = GetRandomValue(3, 5);
    for (int i = 0; i < count; ++i) {
        const float y = overHouse ? c.y + randRange(4.0f, 7.0f) : c.y;
        const Vector3 pos{c.x + randRange(-2.0f, 2.0f), y, c.z + randRange(-2.0f, 2.0f)};
        g.birds.push_back(Bird{pos, vel, 0.0f, 14.0f, randRange(0.0f, 2.0f * Pi)});
    }
}

void updateBirds(Game &g, float dt) {
    for (Bird &b : g.birds) {
        b.pos = Vector3Add(b.pos, Vector3Scale(b.vel, dt));
        b.age += dt;
    }
    g.birds.erase(std::remove_if(g.birds.begin(), g.birds.end(), [](const Bird &b) { return b.age > b.life; }),
                  g.birds.end());
}

// Quiet wind always; chirps and flocks by day, more bugs at night.
// Ignites the lightsaber when it comes into hand and keeps its hum looping while it is held.
void updateSaberAudio(const Game &g, Audio &a) {
    if (!a.ready) {
        return;
    }
    const bool on = saberHeld(g);
    if (on && !a.saberLit) {
        a.saberLit = true;
        PlaySound(a.saberIgnite);
    } else if (!on && a.saberLit) {
        a.saberLit = false;
        StopSound(a.saberHum);
    }
    if (on && !IsSoundPlaying(a.saberHum)) {
        PlaySound(a.saberHum);
    }
}

void updateAmbience(Game &g, Audio &a, float dt) {
    if (!a.ready) {
        return;
    }
    if (!IsSoundPlaying(a.wind)) {
        PlaySound(a.wind);
    }
    SetSoundVolume(a.wind, g.night ? 0.25f : 0.12f);

    a.bugTimer -= dt;
    a.chirpTimer -= dt;
    a.flockTimer -= dt;
    if (a.bugTimer <= 0.0f) {
        playAt(a.bug, randRange(0.05f, 0.1f), randPan());
        a.bugTimer = g.night ? randRange(3.0f, 8.0f) : randRange(12.0f, 25.0f);
    }
    if (g.night) {
        return;
    }
    if (a.chirpTimer <= 0.0f) {
        playAt(a.chirps[GetRandomValue(0, ChirpCount - 1)], randRange(0.08f, 0.18f), randPan());
        a.chirpTimer = randRange(1.5f, 5.0f);
    }
    if (a.flockTimer <= 0.0f) {
        spawnFlock(g);
        playAt(a.flap, randRange(0.1f, 0.2f), randPan());
        a.flockTimer = randRange(20.0f, 40.0f);
    }
}

// Walls and furniture only; returns the nearest hit up to maxD.
float wallDistAlong(const Level &L, Vector3 from, Vector3 dir, float maxD) {
    const Ray ray{from, dir};
    float best = maxD;
    for (const Box &w : L.solids) {
        const RayCollision rc = GetRayCollisionBox(ray, BoundingBox{w.min, w.max});
        if (rc.hit && rc.distance > 0.0f && rc.distance < best) {
            best = rc.distance;
        }
    }
    return best;
}

// Solids close enough to an enemy to block any sight line of length FoeRange from it.
void nearSolids(const Level &L, Vector3 at, std::vector<const Box *> &out) {
    out.clear();
    for (const Box &b : L.solids) {
        const float dx = std::max({b.min.x - at.x, 0.0f, at.x - b.max.x});
        const float dz = std::max({b.min.z - at.z, 0.0f, at.z - b.max.z});
        if (dx * dx + dz * dz <= FoeRange * FoeRange) {
            out.push_back(&b);
        }
    }
}

bool lineBlocked(const std::vector<const Box *> &near, Vector3 a, Vector3 b) {
    const float len = Vector3Distance(a, b);
    if (len <= 0.0f) {
        return false;
    }
    const Ray ray{a, Vector3Scale(Vector3Subtract(b, a), 1.0f / len)};
    for (const Box *w : near) {
        const RayCollision rc = GetRayCollisionBox(ray, BoundingBox{w->min, w->max});
        if (rc.hit && rc.distance > 0.0f && rc.distance < len) {
            return true;
        }
    }
    return false;
}

// E opens the nearest unopened chest: ammo, health, or rarely a grenade.
bool openNearChest(Game &g) {
    for (Chest &c : g.chests) {
        if (c.opened || std::hypot(c.pos.x - g.pos.x, c.pos.z - g.pos.z) > ChestReach) {
            continue;
        }
        c.opened = true;
        if (GetRandomValue(0, 99) < ChestGrenadePercent) {
            ++g.stock[ThrowFrag];
            g.lastHit = "Chest: grenade +1";
        } else if (GetRandomValue(0, 99) < ChestHealthPercent) {
            const int amount = GetRandomValue(ChestHealthMin, ChestHealthMax);
            g.health = std::min(PlayerMaxHealth, g.health + static_cast<float>(amount));
            g.lastHit = "Chest: health +" + std::to_string(amount);
        } else {
            const int amount = GetRandomValue(8, 16);
            g.reserve += amount;
            g.lastHit = "Chest: ammo +" + std::to_string(amount);
        }
        g.hitTextTimer = 1.5f;
        return true;
    }
    return false;
}

constexpr float DoorReach = 2.2f;
constexpr float DoorOpenAngle = 95.0f;
constexpr float DoorSpeed = 150.0f;

// Index of the nearest door within reach of the player, or -1.
int nearDoor(const Game &g) {
    int best = -1;
    float bestD = DoorReach;
    for (size_t i = 0; i < g.level.doors.size(); ++i) {
        const Vector3 &c = g.level.doors[i].center;
        const float d = std::hypot(c.x - g.pos.x, c.z - g.pos.z);
        if (d < bestD) {
            bestD = d;
            best = static_cast<int>(i);
        }
    }
    return best;
}

// E toggles the nearest door; the panel swings and its solid box is removed while open.
void updateDoors(Game &g, float dt) {
    if (!playerFrozen(g) && IsKeyPressed(KEY_E) && nearDroppedWeapon(g) < 0 && !openNearChest(g)) {
        const int i = nearDoor(g);
        if (i >= 0) {
            g.level.doors[i].open = !g.level.doors[i].open;
        }
    }
    const Box gone{Vector3{0.0f, -1000.0f, 0.0f}, Vector3{0.0f, -1000.0f, 0.0f}};
    for (Door &d : g.level.doors) {
        const float target = d.open ? DoorOpenAngle : 0.0f;
        const float step = DoorSpeed * dt;
        if (std::fabs(target - d.angle) <= step) {
            d.angle = target;
        } else {
            d.angle += std::copysign(step, target - d.angle);
        }
        g.level.solids[d.solid] = d.angle < 1.0f ? d.shut : gone;
    }
}

void reapEnemies(Game &g) {
    for (size_t i = 0; i < g.enemies.size();) {
        if (g.enemies[i].hp <= 0) {
            dropEnemyGun(g, g.enemies[i]);
            addCorpse(g, makeCorpse(g.enemies[i], g.enemies[i].dir));
            g.enemies.erase(g.enemies.begin() + static_cast<long>(i));
        } else {
            ++i;
        }
    }
}

void updateReload(Game &g, float dt) {
    if (g.reloadTimer > 0.0f) {
        g.reloadTimer -= dt;
        if (g.reloadTimer <= 0.0f) {
            g.reloadTimer = 0.0f;
            int &mag = activeMag(g);
            const int take = std::min(magSizeOf(g) - mag, g.reserve);
            mag += take;
            g.reserve -= take;
        }
    } else if (!playerFrozen(g) && IsKeyPressed(KEY_R) && g.weapon != WeaponKnife && g.weapon != WeaponTaser && activeMag(g) < magSizeOf(g) && g.reserve > 0) {
        g.reloadTimer = reloadTimeOf(g);
    }
}

// Ammo boxes respawn slowly while fewer than the cap are on the map.
void updateAmmoBoxes(Game &g, float dt) {
    g.ammoRespawn -= dt;
    if (g.ammoRespawn <= 0.0f) {
        g.ammoRespawn = AmmoBoxRespawn;
        if (static_cast<int>(g.ammoBoxes.size()) < AmmoBoxMax) {
            spawnAmmoBox(g);
        }
    }
    for (size_t i = 0; i < g.ammoBoxes.size();) {
        const Vector3 p = g.ammoBoxes[i].pos;
        const bool near = std::hypot(p.x - g.pos.x, p.z - g.pos.z) < AmmoPickupReach && std::fabs(p.y - g.pos.y) < 2.0f;
        if (near) {
            g.reserve += AmmoBoxAmount;
            g.lastHit = "Ammo +" + std::to_string(AmmoBoxAmount);
            g.hitTextTimer = 1.0f;
            g.ammoBoxes.erase(g.ammoBoxes.begin() + static_cast<long>(i));
        } else {
            ++i;
        }
    }
}

// E picks up the nearest dropped gun; the gun in its slot is left behind.
void updateDrops(Game &g) {
    if (playerFrozen(g) || !IsKeyPressed(KEY_E)) {
        return;
    }
    const int i = nearDroppedWeapon(g);
    if (i < 0) {
        return;
    }
    const DroppedWeapon d = g.drops[static_cast<size_t>(i)];
    g.drops.erase(g.drops.begin() + static_cast<long>(i));
    const int held = isSidearm(d.weapon) ? g.sidearm : g.primary;
    if (held >= 0) {
        releaseWeapon(g, held);
    }
    if (isSidearm(d.weapon)) {
        g.sidearm = d.weapon;
    } else {
        g.primary = d.weapon;
    }
    g.owned[d.weapon] = true;
    g.mags[d.weapon] = d.mag;
    g.weapon = d.weapon;
    g.reloadTimer = 0.0f;
    g.scoped = false;
    g.lastHit = "Picked up " + std::string(Weapons[d.weapon].name);
    g.hitTextTimer = 1.5f;
}

void throwGrenade(Game &g) {
    int &left = g.stock[g.throwSel];
    if (left <= 0) {
        return;
    }
    --left;
    if (left <= 0) {
        g.holding = false;
    }
    const Vector3 fwd = forwardOf(g);
    const Vector3 p = Vector3Add(g.cam.position, Vector3Scale(fwd, 0.6f));
    const Vector3 v = Vector3Add(Vector3Scale(fwd, GrenadeSpeed), Vector3{0.0f, 4.0f, 0.0f});
    // Frags and flashbangs take the full fuse; smoke and decoys go off sooner.
    const bool slow = g.throwSel == ThrowFrag || g.throwSel == ThrowFlash;
    g.grenadeList.push_back(Grenade{p, v, slow ? GrenadeFuse : FuseShort, g.throwSel});
}

// True when no wall stands between two points.
bool sightClear(const Game &g, Vector3 a, Vector3 b) {
    const Vector3 d = Vector3Subtract(b, a);
    const float len = Vector3Length(d);
    if (len <= 0.001f) {
        return true;
    }
    return wallDistAlong(g.level, a, Vector3Scale(d, 1.0f / len), len) >= len - 0.3f;
}

// Flashbang: blinds foes and whites out the player when they can see the flash.
void flashBang(Game &g, Audio *a, Vector3 at) {
    g.blasts.push_back(Blast{at, 0.0f, true});
    if (a) {
        playSpatial(g, *a, a->bang, at, 1.0f, 40.0f);
    }
    for (Enemy &e : g.enemies) {
        const Vector3 eye{e.pos.x, e.pos.y + 1.4f, e.pos.z};
        if (e.hp > 0 && Vector3Distance(eye, at) < FlashRadius && sightClear(g, at, eye)) {
            e.blind = FlashBlindTime;
        }
    }
    if (Vector3Distance(g.cam.position, at) < FlashRadius && sightClear(g, at, g.cam.position)) {
        g.flashed = std::max(g.flashed, FlashPlayerTime);
        g.flashAt = at;
        if (a && a->ready) {
            playAt(a->ring, 0.6f, 0.5f);
        }
    }
}

void explode(Game &g, Vector3 at, Audio *a) {
    g.blasts.push_back(Blast{at, 0.0f, false});
    if (a) {
        playSpatial(g, *a, a->boom, at, 1.0f, 40.0f);
    }
    for (Enemy &e : g.enemies) {
        if (e.hp > 0 && hostile(g.localTeam, e.team) && std::hypot(e.pos.x - at.x, e.pos.z - at.z) < GrenadeRadius) {
            if (e.netId != 0) {
                const float d = std::hypot(e.pos.x - at.x, e.pos.z - at.z);
                g.netOut.push_back(hitLine(e.netId, GrenadeDamage * (1.0f - d / GrenadeRadius), false, -1));
                continue;
            }
            addKill(g, KillEntry{"You", GOLD, "HE grenade", botName(e), teamColor(e.team), false,
                                 e.blind > 0.0f, false});
            e.hp = 0;
            g.money += 40;
            g.roundKillMoney += 40;
        }
    }
    reapEnemies(g);
    const Vector3 chest{g.pos.x, g.pos.y + g.height * 0.5f, g.pos.z};
    const float d = Vector3Distance(chest, at);
    if (d < GrenadeRadius) {
        hurtPlayer(g, GrenadeDamage * (1.0f - d / GrenadeRadius));
        g.shake = std::max(g.shake, 1.0f - d / GrenadeRadius);
    }
}

// Runs a grenade's effect when its fuse ends; the kind decides what it does.
void detonate(Game &g, Audio *a, const Grenade &gr) {
    switch (gr.kind) {
    case ThrowFlash:
        flashBang(g, a, gr.pos);
        break;
    case ThrowSmoke:
        g.smokes.push_back(SmokeCloud{gr.pos, 0.0f});
        if (a) {
            playSpatial(g, *a, a->hiss, gr.pos, 0.7f, 30.0f);
        }
        break;
    case ThrowDecoy:
        g.decoys.push_back(Decoy{gr.pos, DecoyLife, 0.0f});
        break;
    default:
        explode(g, gr.pos, a);
        break;
    }
}

void updateGrenades(Game &g, Audio &a, float dt) {
    if (!playerFrozen(g) && IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE)) {
        throwGrenade(g);
    }
    for (Grenade &gr : g.grenadeList) {
        gr.vel.y -= GrenadeGravity * dt;
        gr.pos = Vector3Add(gr.pos, Vector3Scale(gr.vel, dt));
        for (const Box &b : g.level.solids) {
            pushOutOfBox(gr.pos, b, 0.1f);
        }
        const float ground = groundAt(g.level.terrain, gr.pos.x, gr.pos.z) + 0.1f;
        if (gr.pos.y < ground) {
            gr.pos.y = ground;
            gr.vel.y = -gr.vel.y * 0.4f;
            gr.vel.x *= 0.6f;
            gr.vel.z *= 0.6f;
        }
        gr.fuse -= dt;
    }
    for (size_t i = 0; i < g.grenadeList.size();) {
        if (g.grenadeList[i].fuse <= 0.0f) {
            const Grenade gr = g.grenadeList[i];
            g.grenadeList.erase(g.grenadeList.begin() + static_cast<long>(i));
            detonate(g, &a, gr);
        } else {
            ++i;
        }
    }
    for (Blast &b : g.blasts) {
        b.age += dt;
    }
    g.blasts.erase(std::remove_if(g.blasts.begin(), g.blasts.end(), [](const Blast &b) { return b.age > BlastTime; }),
                   g.blasts.end());
    for (FoeTracer &t : g.foeTracers) {
        t.age += dt;
    }
    g.foeTracers.erase(std::remove_if(g.foeTracers.begin(), g.foeTracers.end(),
                                      [](const FoeTracer &t) { return t.age > FoeTracerTime; }),
                       g.foeTracers.end());
}

constexpr int TargetNone = -1;
constexpr int TargetPlayer = -2;

// Enemy gunfire, panned and attenuated by distance from the camera.
void playFoeShot(const Game &g, Audio &a, Vector3 at, int weapon) {
    const Vector3 off = Vector3Subtract(at, g.cam.position);
    const float dist = Vector3Length(off);
    if (!a.ready || dist > FoeSoundRange) {
        return;
    }
    const Vector3 right = Vector3CrossProduct(forwardOf(g), Vector3{0.0f, 1.0f, 0.0f});
    const float side = dist > 0.0f ? Vector3DotProduct(off, right) / dist : 0.0f;
    playAt(a.foeShots[weapon][a.nextFoeShot], (1.0f - dist / FoeSoundRange) * 0.5f, 0.5f + 0.5f * side);
    a.nextFoeShot = (a.nextFoeShot + 1) % ShotSoundCount;
}

// Smoke clouds fade out and decoys beep fake gunfire until their time runs out.
void updateLingering(Game &g, Audio &a, float dt) {
    for (SmokeCloud &s : g.smokes) {
        s.age += dt;
    }
    g.smokes.erase(std::remove_if(g.smokes.begin(), g.smokes.end(),
                                  [](const SmokeCloud &s) { return s.age > SmokeLife; }),
                   g.smokes.end());
    for (Decoy &d : g.decoys) {
        d.life -= dt;
        d.beep -= dt;
        if (d.beep <= 0.0f) {
            d.beep = DecoyBeepTime;
            playFoeShot(g, a, d.pos, WeaponAK);
        }
    }
    g.decoys.erase(std::remove_if(g.decoys.begin(), g.decoys.end(),
                                  [](const Decoy &d) { return d.life <= 0.0f; }),
                   g.decoys.end());
}

float segmentDist(Vector3 a, Vector3 b, Vector3 p) {
    const Vector3 ab = Vector3Subtract(b, a);
    const float len2 = Vector3DotProduct(ab, ab);
    const float t = len2 > 0.0f ? std::clamp(Vector3DotProduct(Vector3Subtract(p, a), ab) / len2, 0.0f, 1.0f) : 0.0f;
    return Vector3Distance(p, Vector3Add(a, Vector3Scale(ab, t)));
}

// How squarely the player faces a point: 1 when dead ahead, 0 about 60 degrees off or more.
float lookAt(const Game &g, Vector3 at) {
    const Vector3 d = Vector3Subtract(at, g.cam.position);
    if (Vector3Length(d) < 0.001f) {
        return 1.0f;
    }
    return std::clamp((Vector3DotProduct(forwardOf(g), Vector3Normalize(d)) - 0.5f) / 0.3f, 0.0f, 1.0f);
}

// True when smoke hangs on the sight line between two points.
bool smoked(const Game &g, Vector3 a, Vector3 b) {
    return std::any_of(g.smokes.begin(), g.smokes.end(),
                       [&](const SmokeCloud &s) { return segmentDist(a, b, s.pos) < SmokeRadius; });
}

// True when a friendly unit or the player stands between the shooter and its target.
bool friendlyInLine(const Game &g, size_t self, int target, Vector3 eye, Vector3 aim) {
    const Team team = g.enemies[self].team;
    for (size_t j = 0; j < g.enemies.size(); ++j) {
        const Enemy &o = g.enemies[j];
        if (j == self || static_cast<int>(j) == target || o.hp <= 0 || botHostile(team, o.team)) {
            continue;
        }
        if (segmentDist(eye, aim, Vector3{o.pos.x, o.pos.y + 1.4f, o.pos.z}) < FoeBodyRadius) {
            return true;
        }
    }
    const Vector3 pt{g.pos.x, g.pos.y + g.height * 0.7f, g.pos.z};
    return target != TargetPlayer && !hostile(team, TeamBlue) && segmentDist(eye, aim, pt) < FoeBodyRadius;
}

struct Candidate {
    int target;
    float dist;
    bool clear;
    Vector3 aim;
};

// Every hostile unit this enemy can see within FoeRange, flagged when its line of fire is free of friends.
void collectTargets(const Game &g, size_t self, Vector3 eye, std::vector<Candidate> &out) {
    out.clear();
    const Team team = g.enemies[self].team;
    std::vector<const Box *> near;
    bool built = false;
    auto clearSight = [&](Vector3 pt) {
        if (!built) {
            nearSolids(g.level, eye, near);
            built = true;
        }
        return !lineBlocked(near, eye, pt) && !smoked(g, eye, pt);
    };
    if (g.health > 0.0f && hostile(team, TeamBlue)) {
        const Vector3 pt{g.pos.x, g.pos.y + g.height * 0.7f, g.pos.z};
        const float d = Vector3Distance(eye, pt);
        if (d < FoeRange && clearSight(pt)) {
            out.push_back(Candidate{TargetPlayer, d, false, pt});
        }
    }
    for (size_t j = 0; j < g.enemies.size(); ++j) {
        const Enemy &o = g.enemies[j];
        if (j == self || o.hp <= 0 || !botHostile(team, o.team)) {
            continue;
        }
        const Vector3 pt{o.pos.x, o.pos.y + 1.4f, o.pos.z};
        const float d = Vector3Distance(eye, pt);
        if (d < FoeRange && clearSight(pt)) {
            out.push_back(Candidate{static_cast<int>(j), d, false, pt});
        }
    }
    for (Candidate &c : out) {
        c.clear = !friendlyInLine(g, self, c.target, eye, c.aim);
    }
}

// Closest decoy to a point, or nullptr when none is down.
const Decoy *nearestDecoy(const Game &g, Vector3 at) {
    const Decoy *best = nullptr;
    float bestDist = 0.0f;
    for (const Decoy &d : g.decoys) {
        const float dist = Vector3Distance(at, d.pos);
        if (best == nullptr || dist < bestDist) {
            best = &d;
            bestDist = dist;
        }
    }
    return best;
}

constexpr float LootReach = 14.0f;  // how far a bot will walk to reach a gun on the floor
constexpr float LootTouch = 1.0f;   // distance at which a bot takes the gun

// Index of the nearest floor gun worth more than this enemy's current one, or -1.
int lootFor(const Game &g, const Enemy &e) {
    int best = -1;
    float bestDist = LootReach;
    for (size_t k = 0; k < g.drops.size(); ++k) {
        const DroppedWeapon &d = g.drops[k];
        if (Weapons[d.weapon].price <= Weapons[e.weapon].price) {
            continue;
        }
        const float dist = std::hypot(d.pos.x - e.pos.x, d.pos.z - e.pos.z);
        if (dist < bestDist) {
            best = static_cast<int>(k);
            bestDist = dist;
        }
    }
    return best;
}

// Moves one enemy along its brain-chosen direction and fires at its chosen target.
void applyBrain(Game &g, Audio &a, size_t i, const std::vector<Candidate> &cands, double mx, double mz,
                int target, bool fire, float dt) {
    Enemy &e = g.enemies[i];
    if (e.hp <= 0) {
        return;
    }
    e.aimingSniper = false;
    e.flash = std::max(0.0f, e.flash - dt);
    if (e.stun > 0.0f) {
        e.stun -= dt;
        return;
    }
    if (e.blind > 0.0f) {
        e.blind -= dt;
        return;
    }
    const Vector3 eye{e.pos.x, e.pos.y + 1.4f, e.pos.z};
    e.dir = Vector3{static_cast<float>(mx), 0.0f, static_cast<float>(mz)};
    if (target == TargetNone && e.team == TeamRed) {
        // With nothing in sight, foes walk toward a heard gunshot, else the nearest decoy.
        const Vector3 toNoise{g.noisePos.x - e.pos.x, 0.0f, g.noisePos.z - e.pos.z};
        const float noiseDist = Vector3Length(toNoise);
        const Decoy *lure = nearestDecoy(g, e.pos);
        if (g.noiseTimer > 0.0f && noiseDist < FoeSoundRange && noiseDist > 2.0f) {
            e.dir = Vector3Normalize(toNoise);
        } else if (lure != nullptr && Vector3Distance(lure->pos, e.pos) > 1.5f) {
            e.dir = Vector3Normalize(Vector3{lure->pos.x - e.pos.x, 0.0f, lure->pos.z - e.pos.z});
        }
    }
    // With nothing in sight, a fighter walks to a better gun on the floor and swaps to it.
    if (target == TargetNone) {
        const int loot = lootFor(g, e);
        if (loot >= 0) {
            const DroppedWeapon d = g.drops[static_cast<size_t>(loot)];
            const Vector3 toGun{d.pos.x - e.pos.x, 0.0f, d.pos.z - e.pos.z};
            if (Vector3Length(toGun) < LootTouch) {
                if (e.weapon != WeaponPistol) {
                    g.drops.push_back(DroppedWeapon{e.pos, e.weapon, e.mag});
                }
                e.weapon = d.weapon;
                e.mag = d.mag > 0 ? d.mag : Weapons[d.weapon].mag;
                e.reloadTimer = 0.0f;
                g.drops.erase(g.drops.begin() + loot);
            } else {
                e.dir = Vector3Normalize(toGun);
            }
        }
    }
    const float speed = target == -1 ? EnemySearchSpeed : EnemySpeed;
    const float nx = e.pos.x + e.dir.x * speed * dt;
    const float nz = e.pos.z + e.dir.z * speed * dt;
    if (!blockedAt(g.level, nx, nz, EnemyRadius)) {
        e.pos.x = nx;
        e.pos.z = nz;
    } else if (!blockedAt(g.level, nx, e.pos.z, EnemyRadius)) {
        e.pos.x = nx;
    } else if (!blockedAt(g.level, e.pos.x, nz, EnemyRadius)) {
        e.pos.z = nz;
    } else {
        e.dir = Vector3Scale(e.dir, -1.0f);
    }
    e.pos.y = groundAt(g.level.terrain, e.pos.x, e.pos.z);

    if (e.reloadTimer > 0.0f) {
        e.reloadTimer -= dt;
        if (e.reloadTimer <= 0.0f) {
            e.reloadTimer = 0.0f;
            e.mag = Weapons[e.weapon].mag;
        }
        return;
    }

    const WeaponDef &def = Weapons[e.weapon];
    // An AWP holder with a clear line on the player lights up its muzzle so the player can see the threat.
    for (const Candidate &c : cands) {
        if (c.target == TargetPlayer && c.clear && e.weapon == WeaponAWP) {
            e.aimingSniper = true;
        }
    }
    const Candidate *shot = nullptr;
    for (const Candidate &c : cands) {
        if (fire && c.target == target && c.clear) {
            shot = &c;
        }
    }
    if (shot == nullptr) {
        return;
    }
    playFoeShot(g, a, eye, e.weapon);
    if (--e.mag <= 0) {
        e.reloadTimer = def.reload;
    }
    const Vector3 toT = Vector3Subtract(shot->aim, eye);
    const Vector3 jitter{randRange(-def.spread, def.spread), randRange(-def.spread, def.spread),
                         randRange(-def.spread, def.spread)};
    const Vector3 dir = Vector3Normalize(Vector3Add(Vector3Normalize(toT), jitter));
    const float proj = Vector3DotProduct(toT, dir);
    const Vector3 perp = Vector3Subtract(toT, Vector3Scale(dir, proj));
    const float wallD = wallDistAlong(g.level, eye, dir, FoeRange * 1.5f);
    const bool hit = proj > 0.0f && proj < wallD && Vector3Length(perp) < FoeBodyRadius;
    const float endD = hit ? proj : wallD;
    g.foeTracers.push_back(FoeTracer{eye, Vector3Add(eye, Vector3Scale(dir, endD)), 0.0f});
    e.flash = FireTime;
    e.flashPos = eye;
    e.flashDir = dir;
    e.lastShot = hit ? 1 : 0;
    if (hit) {
        const Color killerCol = teamColor(e.team);
        if (target == TargetPlayer) {
            const bool wasAlive = g.health > 0.0f;
            hurtPlayer(g, def.playerDamage);
            if (wasAlive && g.health <= 0.0f) {
                addKill(g, KillEntry{botName(e), killerCol, def.name, "You", GOLD, false, false, false});
            }
        } else {
            Enemy &victim = g.enemies[static_cast<size_t>(target)];
            const bool wasAlive = victim.hp > 0;
            victim.hp -= def.hits;
            if (wasAlive && victim.hp <= 0) {
                addKill(g, KillEntry{botName(e), killerCol, def.name, botName(victim),
                                     teamColor(victim.team), false, victim.blind > 0.0f,
                                     smoked(g, eye, Vector3{victim.pos.x, victim.pos.y + 1.4f, victim.pos.z})});
            }
        }
    }
}

// Sends every enemy's sight lines to the Rust brain and applies the returned moves and shots.
void updateEnemies(Game &g, Audio &a, float dt) {
    g.noiseTimer = std::max(0.0f, g.noiseTimer - dt);
    if (g.online) {
        // Online matches have no bots; the other players are updated from the network.
        return;
    }
    if (g.enemies.empty()) {
        return;
    }
    // Enemies open any house door they walk up to, so they can go inside.
    for (const Enemy &e : g.enemies) {
        for (Door &d : g.level.doors) {
            if (!d.open && Vector3Distance(e.pos, d.center) < DoorReach) {
                d.open = true;
            }
        }
    }
    const size_t count = g.enemies.size();
    std::vector<std::vector<Candidate>> targets(count);
    std::vector<AiUnit> units(count);
    std::vector<AiCand> cands;
    for (size_t i = 0; i < count; ++i) {
        Enemy &e = g.enemies[i];
        const Vector3 eye{e.pos.x, e.pos.y + 1.4f, e.pos.z};
        collectTargets(g, i, eye, targets[i]);
        AiUnit &u = units[i];
        u.uid = e.uid;
        u.x = e.pos.x;
        u.z = e.pos.z;
        u.dirX = e.dir.x;
        u.dirZ = e.dir.z;
        u.team = static_cast<int32_t>(e.team);
        u.weapon = e.weapon;
        u.last = e.lastShot;
        u.candStart = static_cast<int32_t>(cands.size());
        u.candCount = static_cast<int32_t>(targets[i].size());
        for (const Candidate &c : targets[i]) {
            cands.push_back(AiCand{c.target, static_cast<double>(c.dist), c.clear ? 1 : 0,
                                   static_cast<double>(c.aim.x), static_cast<double>(c.aim.z)});
        }
        e.lastShot = -1;
    }

    std::vector<AiReply> replies(count);
    ai_think(units.data(), static_cast<int32_t>(count), cands.data(), static_cast<double>(dt), replies.data());
    for (size_t i = 0; i < count; ++i) {
        applyBrain(g, a, i, targets[i], replies[i].moveX, replies[i].moveZ, replies[i].target, replies[i].fire == 1,
                   dt);
    }
}

constexpr float ShopWidth = 860.0f;
constexpr float ShopHeight = 640.0f;
constexpr float ShopRowH = 30.0f;
constexpr float ShopListX = 20.0f;  // list offset inside the panel
constexpr float ShopListY = 110.0f;
constexpr float ShopListW = 540.0f;
constexpr float ShopListH = 430.0f;
constexpr float ShopTabY = 58.0f;
constexpr float ShopTabGap = 6.0f;
constexpr float ShopTabW = (ShopListW - 4.0f * ShopTabGap) / 5.0f;
constexpr float ShopBackY = ShopHeight - 58.0f;

Rectangle shopPanel() {
    return Rectangle{(GetScreenWidth() - ShopWidth) * 0.5f, GetScreenHeight() * 0.5f - ShopHeight * 0.5f, ShopWidth,
                     ShopHeight};
}

Rectangle shopListArea() {
    const Rectangle p = shopPanel();
    return Rectangle{p.x + ShopListX, p.y + ShopListY, ShopListW, ShopListH};
}

Rectangle shopTabRect(int c) {
    const Rectangle p = shopPanel();
    return Rectangle{p.x + ShopListX + static_cast<float>(c) * (ShopTabW + ShopTabGap), p.y + ShopTabY, ShopTabW, 28.0f};
}

Rectangle shopBackRect() {
    const Rectangle p = shopPanel();
    return Rectangle{p.x + ShopListX, p.y + ShopBackY, 380.0f, 30.0f};
}

// One line of the buy list: a category header (row -1) or an item (row index).
struct ShopLine {
    int cat;
    int row;
};

std::vector<ShopLine> shopLines() {
    std::vector<ShopLine> lines;
    int row = 0;
    for (int c = 0; c < ShopCatCount; ++c) {
        lines.push_back(ShopLine{c, -1});
        for (int k = 0; k < ShopCatSize[c]; ++k) {
            lines.push_back(ShopLine{c, row++});
        }
    }
    return lines;
}

int shopHeaderLine(int c) {
    const std::vector<ShopLine> lines = shopLines();
    for (int li = 0; li < static_cast<int>(lines.size()); ++li) {
        if (lines[static_cast<size_t>(li)].row < 0 && lines[static_cast<size_t>(li)].cat == c) {
            return li;
        }
    }
    return 0;
}

int shopCatOf(int row) {
    int first = 0;
    for (int c = 0; c < ShopCatCount; ++c) {
        if (row < first + ShopCatSize[c]) {
            return c;
        }
        first += ShopCatSize[c];
    }
    return CatGear;
}

float shopMaxScroll() {
    const float total = static_cast<float>(shopLines().size()) * ShopRowH;
    return std::max(0.0f, total - ShopListH);
}

// Screen rectangle of a list line, shifted by the scroll.
Rectangle shopLineRect(const Game &g, int line) {
    const Rectangle area = shopListArea();
    return Rectangle{area.x, area.y + static_cast<float>(line) * ShopRowH - g.shopScroll, area.width, ShopRowH};
}

// True when a list line is fully inside the visible list window.
bool shopLineFull(const Game &g, int line) {
    const Rectangle r = shopLineRect(g, line);
    const Rectangle area = shopListArea();
    return r.y >= area.y - 0.5f && r.y + ShopRowH <= area.y + area.height + 0.5f;
}

// Number key for each buy-menu row: the fully visible items, top to bottom, get keys 1-9, 0, -.
void shopKeyMap(const Game &g, int (&keyOf)[ShopCount]) {
    for (int i = 0; i < ShopCount; ++i) {
        keyOf[i] = -1;
    }
    const std::vector<ShopLine> lines = shopLines();
    int n = 0;
    for (int li = 0; li < static_cast<int>(lines.size()); ++li) {
        const ShopLine &l = lines[static_cast<size_t>(li)];
        if (l.row >= 0 && n < ShopKeyCount && shopLineFull(g, li)) {
            keyOf[l.row] = n++;
        }
    }
}

void placeOnlineSpawn(Game &g);

// Online: starts a round with every player back at spawn.
void startOnlineRound(Game &g) {
    g.roundLive = true;
    g.roundPlayers = 0;
    g.roundMsg.clear();
    g.dead = false;
    g.deathSent = false;
    g.lastAttacker = 0;
    g.lastWeapon = -1;
    g.lastHead = false;
    placeOnlineSpawn(g);
}

// Online: ends the round when one player, or one team, is left alive.
void checkOnlineRoundEnd(Game &g) {
    int alive = 0;
    int aliveBlue = 0;
    int aliveRed = 0;
    int lastFd = g.localFd;
    if (!g.dead) {
        ++alive;
        if (g.localTeam == TeamBlue) {
            ++aliveBlue;
        } else {
            ++aliveRed;
        }
    }
    for (const Enemy &e : g.enemies) {
        if (e.netId == 0) {
            continue;
        }
        ++alive;
        if (e.team == TeamBlue) {
            ++aliveBlue;
        } else {
            ++aliveRed;
        }
        lastFd = e.netId;
    }
    g.roundPlayers = std::max(g.roundPlayers, alive);
    if (g.roundPlayers < 2) {
        return;
    }
    if (gSettings.teamMode) {
        if (aliveBlue > 0 && aliveRed > 0) {
            return;
        }
        g.roundMsg = aliveBlue > 0 ? "Blue team won the round" : aliveRed > 0 ? "Red team won the round" : "No one won the round";
    } else {
        if (alive > 1) {
            return;
        }
        if (alive == 0) {
            g.roundMsg = "No one won the round";
        } else if (lastFd == g.localFd) {
            g.roundMsg = "You won the round";
        } else {
            g.roundMsg = playerName(g, lastFd) + " won the round";
        }
    }
    g.roundLive = false;
    g.intermission = RoundEndTime + RoundBuyTime;
}

// Counts down between waves, spawns the next wave, and pays out when a round is won.
void updateRound(Game &g, Audio &a, float dt) {
    if (g.online) {
        // Online rounds: a countdown runs between rounds, then the next round starts.
        if (g.intermission > 0.0f) {
            g.intermission -= dt;
            if (g.intermission <= 0.0f) {
                g.intermission = 0.0f;
                startOnlineRound(g);
            }
        } else if (g.roundLive) {
            checkOnlineRoundEnd(g);
        }
        return;
    }
    if (g.intermission > 0.0f) {
        g.intermission -= dt;
        if (g.intermission <= 0.0f) {
            g.intermission = 0.0f;
            spawnWave(g);
            if (a.ready) {
                playAt(a.waveStart, 0.6f, 0.5f);
            }
        }
        return;
    }
    if (std::none_of(g.enemies.begin(), g.enemies.end(), [](const Enemy &e) { return e.team == TeamRed; })) {
        g.money += RoundWinMoney;
        g.lastHit = "Round won +$" + std::to_string(RoundWinMoney);
        g.hitTextTimer = 2.5f;
        g.intermission = randRange(IntermissionMin, IntermissionMax);
        if (a.ready) {
            playAt(a.roundWon, 0.7f, 0.5f);
        }
    }
}

// Buys a gun into its slot; the one it replaces is left on the floor.
void buyWeapon(Game &g, int w) {
    const WeaponDef &def = Weapons[w];
    if (g.owned[w]) {
        return;
    }
    if (g.money < def.price) {
        g.lastHit = "Not enough money for " + std::string(def.name);
        g.hitTextTimer = 2.0f;
        return;
    }
    g.money -= def.price;
    if (isSidearm(w)) {
        releaseWeapon(g, g.sidearm);
        g.sidearm = w;
    } else {
        if (g.primary >= 0) {
            releaseWeapon(g, g.primary);
        }
        g.primary = w;
    }
    g.owned[w] = true;
    g.mags[w] = def.mag;
    g.weapon = w;
    g.reloadTimer = 0.0f;
    g.lastHit = "Bought " + std::string(def.name);
    g.hitTextTimer = 2.0f;
}

// Gear rows: armor and ammo, then the four throwables in ThrowNames order.
int throwOf(int gear) {
    return gear - GearFrag;
}

bool gearFull(const Game &g, int gear) {
    if (gear == GearArmor) {
        return g.armor >= ArmorMax;
    }
    if (gear == GearAmmo) {
        return false;
    }
    return g.stock[throwOf(gear)] >= ThrowCap;
}

// Price and name for a buy-menu row: weapons first, then gear.
int rowPrice(int i) {
    return i < ShopWeaponCount ? Weapons[ShopItems[i]].price : Gears[i - ShopWeaponCount].price;
}

const char *rowName(int i) {
    return i < ShopWeaponCount ? Weapons[ShopItems[i]].name : Gears[i - ShopWeaponCount].name;
}

bool rowFull(const Game &g, int i) {
    if (i < ShopWeaponCount) {
        return g.owned[ShopItems[i]];
    }
    return gearFull(g, i - ShopWeaponCount);
}

void buyGear(Game &g, const Audio &a, int gear) {
    const GearDef &def = Gears[gear];
    if (gearFull(g, gear)) {
        g.lastHit = std::string(def.name) + " is full";
        g.hitTextTimer = 2.0f;
        return;
    }
    if (g.money < def.price) {
        g.lastHit = "Not enough money for " + std::string(def.name);
        g.hitTextTimer = 2.0f;
        return;
    }
    g.money -= def.price;
    if (gear == GearArmor) {
        g.armor = ArmorMax;
        if (a.ready) {
            playAt(a.clink, 0.6f, 0.5f);
        }
    } else if (gear == GearAmmo) {
        g.reserve += AmmoPackAmount;
    } else {
        ++g.stock[throwOf(gear)];
    }
    g.lastHit = "Bought " + std::string(def.name);
    g.hitTextTimer = 2.0f;
}

// Copies the state a purchase can change, so the purchase can be undone.
BuySnapshot takeSnapshot(const Game &g) {
    BuySnapshot s;
    s.weapon = g.weapon;
    std::copy(std::begin(g.mags), std::end(g.mags), s.mags);
    std::copy(std::begin(g.owned), std::end(g.owned), s.owned);
    s.primary = g.primary;
    s.sidearm = g.sidearm;
    s.reserve = g.reserve;
    s.armor = g.armor;
    std::copy(std::begin(g.stock), std::end(g.stock), s.stock);
    s.reloadTimer = g.reloadTimer;
    s.drops = g.drops;
    return s;
}

// Buy-menu portrait grid: 4 columns, up to 8 visible; LEFT/RIGHT cycles the rest.
constexpr int ShopPortraitCols = 4;
constexpr int ShopPortraitMax = 8;

Rectangle shopPortraitRect(int slot) {
    const Rectangle p = shopPanel();
    const int col = slot % ShopPortraitCols;
    const int row = slot / ShopPortraitCols;
    return Rectangle{p.x + 580.0f + static_cast<float>(col) * 62.0f, p.y + 492.0f + static_cast<float>(row) * 62.0f,
                     56.0f, 56.0f};
}

// Index of a teammate by uid, or -1 when it is gone.
int allyIndex(const Game &g, int uid) {
    for (size_t k = 0; k < g.enemies.size(); ++k) {
        if (g.enemies[k].uid == uid && g.enemies[k].team == TeamBlue) {
            return static_cast<int>(k);
        }
    }
    return -1;
}

const Enemy *allyOf(const Game &g, int uid) {
    const int k = allyIndex(g, uid);
    return k < 0 ? nullptr : &g.enemies[static_cast<size_t>(k)];
}

// Buy-menu targets: the player (-1) first, then each living teammate in team mode.
std::vector<int> buyTargets(const Game &g) {
    std::vector<int> out{-1};
    if (!gSettings.teamMode) {
        return out;
    }
    for (const Enemy &e : g.enemies) {
        if (e.team == TeamBlue && e.hp > 0 && e.netId == 0) {
            out.push_back(e.uid);
        }
    }
    return out;
}

// Gives a teammate the gun in this buy-menu row; its old gun is dropped at its feet.
void buyForAlly(Game &g, int uid, int row) {
    const int k = allyIndex(g, uid);
    if (k < 0 || g.enemies[static_cast<size_t>(k)].hp <= 0) {
        g.lastHit = "That teammate is down";
        g.hitTextTimer = 2.0f;
        return;
    }
    Enemy &ally = g.enemies[static_cast<size_t>(k)];
    const int w = ShopItems[row];
    if (ally.weapon == w) {
        g.lastHit = botName(ally) + " already has " + rowName(row);
        g.hitTextTimer = 2.0f;
        return;
    }
    if (g.money < rowPrice(row)) {
        g.lastHit = std::string("Not enough money for ") + rowName(row);
        g.hitTextTimer = 2.0f;
        return;
    }
    g.money -= rowPrice(row);
    if (ally.weapon != WeaponPistol) {
        g.drops.push_back(DroppedWeapon{ally.pos, ally.weapon, ally.mag});
    }
    ally.weapon = w;
    ally.mag = Weapons[w].mag;
    ally.reloadTimer = 0.0f;
    g.lastHit = std::string("Gave ") + rowName(row) + " to " + botName(ally);
    g.hitTextTimer = 2.0f;
}

// Buys whatever a buy-menu row holds and records it so it can be returned this wave break.
void buyItem(Game &g, const Audio &a, int i) {
    if (g.shopAlly >= 0) {
        if (i < ShopWeaponCount) {
            buyForAlly(g, g.shopAlly, i);
        } else {
            g.lastHit = "Gear can only be bought for yourself";
            g.hitTextTimer = 2.0f;
        }
        return;
    }
    const int before = g.money;
    BuySnapshot snap = takeSnapshot(g);
    if (i < ShopWeaponCount) {
        buyWeapon(g, ShopItems[i]);
    } else {
        buyGear(g, a, i - ShopWeaponCount);
    }
    if (g.money < before) {
        snap.price = before - g.money;
        snap.name = rowName(i);
        g.buyHistory.push_back(std::move(snap));
        if (a.ready) {
            playAt(a.cash, 0.5f, 0.5f);
        }
    }
}

// Gives back the newest purchase of this wave break and restores the state from before it.
bool returnLastBuy(Game &g, const Audio &a) {
    if (g.buyHistory.empty()) {
        return false;
    }
    BuySnapshot s = std::move(g.buyHistory.back());
    g.buyHistory.pop_back();
    g.money += s.price;
    g.weapon = s.weapon;
    std::copy(std::begin(s.mags), std::end(s.mags), g.mags);
    std::copy(std::begin(s.owned), std::end(s.owned), g.owned);
    g.primary = s.primary;
    g.sidearm = s.sidearm;
    g.reserve = s.reserve;
    g.armor = s.armor;
    std::copy(std::begin(s.stock), std::end(s.stock), g.stock);
    g.reloadTimer = s.reloadTimer;
    g.drops = std::move(s.drops);
    g.lastHit = "Returned " + s.name + " +$" + std::to_string(s.price);
    g.hitTextTimer = 2.0f;
    if (a.ready) {
        playAt(a.cash, 0.5f, 0.5f);
    }
    return true;
}

// B opens the buy menu between waves; it closes on B, ESC, or when the wave starts.
void updateShop(Game &g, const Audio &a) {
    const bool shopTime = g.online ? (g.intermission > 0.0f && g.intermission <= RoundBuyTime) : g.intermission > 0.0f;
    if (!shopTime) {
        if (g.shopOpen) {
            g.shopOpen = false;
            DisableCursor();
        }
        return;
    }
    if (IsKeyPressed(KEY_B)) {
        g.shopOpen = !g.shopOpen;
        if (g.shopOpen) {
            EnableCursor();
        } else {
            DisableCursor();
        }
    }
    if (!g.shopOpen) {
        return;
    }

    const Vector2 m = GetMousePosition();
    const float wheel = GetMouseWheelMove();
    if (wheel != 0.0f && CheckCollisionPointRec(m, shopPanel())) {
        g.shopScroll -= wheel * ShopRowH * 2.0f;
    }
    if (IsKeyPressed(KEY_UP)) {
        g.shopScroll -= ShopRowH;
    }
    if (IsKeyPressed(KEY_DOWN)) {
        g.shopScroll += ShopRowH;
    }
    g.shopScroll = std::clamp(g.shopScroll, 0.0f, shopMaxScroll());

    // Return the newest purchase: Backspace or the arrow button.
    if (IsKeyPressed(KEY_BACKSPACE) ||
        (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(m, shopBackRect()))) {
        returnLastBuy(g, a);
    }

    // Number keys buy the visible items.
    int keyOf[ShopCount];
    shopKeyMap(g, keyOf);
    for (int i = 0; i < ShopCount; ++i) {
        if (keyOf[i] >= 0 && IsKeyPressed(ShopKeys[keyOf[i]])) {
            g.shopSel = i;
            buyItem(g, a, i);
        }
    }

    // Pick who the buy menu buys for: LEFT/RIGHT cycle, or click a portrait.
    const std::vector<int> targets = buyTargets(g);
    if (std::find(targets.begin(), targets.end(), g.shopAlly) == targets.end()) {
        g.shopAlly = -1;
    }
    const int n = static_cast<int>(targets.size());
    const int at = static_cast<int>(std::find(targets.begin(), targets.end(), g.shopAlly) - targets.begin());
    int step = 0;
    if (IsKeyPressed(KEY_RIGHT)) {
        step = 1;
    }
    if (IsKeyPressed(KEY_LEFT)) {
        step = -1;
    }
    if (step != 0) {
        g.shopAlly = targets[static_cast<size_t>((at + step + n) % n)];
    }
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        for (int s = 0; s < n && s < ShopPortraitMax; ++s) {
            if (CheckCollisionPointRec(m, shopPortraitRect(s))) {
                g.shopAlly = targets[static_cast<size_t>(s)];
            }
        }
    }

    // Category tabs jump the list to their header.
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        for (int c = 0; c < ShopCatCount; ++c) {
            if (CheckCollisionPointRec(m, shopTabRect(c))) {
                g.shopScroll = std::clamp(static_cast<float>(shopHeaderLine(c)) * ShopRowH, 0.0f, shopMaxScroll());
            }
        }
    }

    // Hover selects an item for the preview; clicking buys it.
    const std::vector<ShopLine> lines = shopLines();
    const Rectangle area = shopListArea();
    if (!CheckCollisionPointRec(m, area)) {
        return;
    }
    for (int li = 0; li < static_cast<int>(lines.size()); ++li) {
        const int row = lines[static_cast<size_t>(li)].row;
        if (row < 0 || !CheckCollisionPointRec(m, shopLineRect(g, li))) {
            continue;
        }
        g.shopSel = row;
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            buyItem(g, a, row);
        }
    }
}

constexpr float PreviewPx = 260.0f;
RenderTexture2D gShopView{};

void drawWeaponParts(int weapon, bool muzzle);
void setGunGloss(bool on);
void drawThrowParts(int kind);
void drawArmorParts();
void drawAmmoParts();

// Spins the selected buy-menu item in a small render target and draws it into box.
void drawShopPreview(const Game &g, Rectangle box) {
    if (gShopView.id == 0) {
        gShopView = LoadRenderTexture(static_cast<int>(PreviewPx), static_cast<int>(PreviewPx));
    }
    const int sel = g.shopSel;
    const float spin = static_cast<float>(GetTime()) * 1.2f * RAD2DEG;
    Camera3D cam{};
    cam.position = Vector3{0.0f, 0.35f, 1.9f};
    cam.target = Vector3{0.0f, 0.0f, 0.0f};
    cam.up = Vector3{0.0f, 1.0f, 0.0f};
    cam.fovy = 36.0f;
    cam.projection = CAMERA_PERSPECTIVE;
    BeginTextureMode(gShopView);
    ClearBackground(Color{20, 20, 24, 255});
    BeginMode3D(cam);
    rlPushMatrix();
    if (sel < ShopWeaponCount) {
        rlTranslatef(0.0f, 0.0f, -0.15f);
        rlRotatef(spin, 0.0f, 1.0f, 0.0f);
        drawWeaponParts(ShopItems[sel], false);
    } else {
        rlRotatef(spin, 0.0f, 1.0f, 0.0f);
        const int gear = sel - ShopWeaponCount;
        if (gear == GearArmor) {
            drawArmorParts();
        } else if (gear == GearAmmo) {
            drawAmmoParts();
        } else {
            drawThrowParts(throwOf(gear));
        }
    }
    rlPopMatrix();
    EndMode3D();
    EndTextureMode();
    DrawTexturePro(gShopView.texture, Rectangle{0.0f, 0.0f, PreviewPx, -PreviewPx}, box, Vector2{0.0f, 0.0f}, 0.0f,
                   WHITE);
}

// Buy menu drawing: category tabs, scrolling list, preview with stats, and the return button.
// Face of a fighter drawn from a Look with the given shirt color.
void drawLookPortrait(const Look &l, Color shirt, Rectangle r, bool selected) {
    DrawRectangleRec(r, Color{30, 30, 30, 255});
    const float cx = r.x + r.width * 0.5f;
    const float cy = r.y + r.height * 0.48f;
    const float rad = r.width * 0.22f;
    const float shirtY = r.y + r.height * 0.72f;
    const Color hair = HairColors[l.hairColor];
    const Color hat = HatColors[l.hatColor];
    const Color skin{235, 200, 170, 255};
    DrawRectangleRec(Rectangle{r.x + 6.0f, shirtY, r.width - 12.0f, r.y + r.height - shirtY - 2.0f}, shirt);
    if (l.hair == HairLong) {
        DrawRectangleRec(Rectangle{cx - rad, cy - rad * 0.3f, rad * 2.0f, rad * 1.8f}, hair);
    } else if (l.hair == HairMohawk) {
        DrawRectangleRec(Rectangle{cx - rad * 0.15f, cy - rad * 1.3f, rad * 0.3f, rad}, hair);
    }
    if (l.hair == HairShort || l.hair == HairLong) {
        DrawCircleV(Vector2{cx, cy - rad * 0.35f}, rad, hair);
    }
    DrawCircleV(Vector2{cx, cy}, rad, skin);
    DrawCircleV(Vector2{cx - rad * 0.4f, cy - rad * 0.1f}, rad * 0.12f, DARKGRAY);
    DrawCircleV(Vector2{cx + rad * 0.4f, cy - rad * 0.1f}, rad * 0.12f, DARKGRAY);
    DrawLineV(Vector2{cx - rad * 0.35f, cy + rad * 0.45f}, Vector2{cx + rad * 0.35f, cy + rad * 0.45f}, MAROON);
    if (l.hat == HatCap) {
        DrawRectangleRec(Rectangle{cx - rad * 1.05f, cy - rad * 1.05f, rad * 2.1f, rad * 0.55f}, hat);
        DrawRectangleRec(Rectangle{cx, cy - rad * 0.55f, rad * 1.2f, rad * 0.18f}, hat);
    } else if (l.hat == HatBeanie) {
        DrawCircleV(Vector2{cx, cy - rad * 0.65f}, rad * 1.05f, hat);
        DrawRectangleRec(Rectangle{cx - rad * 1.05f, cy - rad * 0.65f, rad * 2.1f, rad * 0.4f}, hat);
    } else if (l.hat == HatTop) {
        DrawRectangleRec(Rectangle{cx - rad * 0.6f, cy - rad * 1.8f, rad * 1.2f, rad * 1.2f}, hat);
        DrawRectangleRec(Rectangle{cx - rad * 1.0f, cy - rad * 0.7f, rad * 2.0f, rad * 0.2f}, hat);
    }
    if (l.jewel == JewelChain || l.jewel == JewelBoth) {
        DrawLineV(Vector2{cx - rad * 0.9f, shirtY + 1.0f}, Vector2{cx, shirtY + 8.0f}, GOLD);
        DrawLineV(Vector2{cx + rad * 0.9f, shirtY + 1.0f}, Vector2{cx, shirtY + 8.0f}, GOLD);
    }
    if (l.jewel == JewelEarring || l.jewel == JewelBoth) {
        DrawCircleV(Vector2{cx - rad, cy + rad * 0.2f}, 1.6f, GOLD);
        DrawCircleV(Vector2{cx + rad, cy + rad * 0.2f}, 1.6f, GOLD);
    }
    const float ly = shirtY + (r.y + r.height - shirtY) * 0.5f;
    std::vector<Vector2> dots;
    float size = 1.5f;
    if (l.logo == LogoStripes) {
        dots = {{0.0f, -4.0f}, {0.0f, 0.0f}, {0.0f, 4.0f}};
    } else if (l.logo == LogoRing) {
        for (int k = 0; k < 5; ++k) {
            const float a = static_cast<float>(k) * 2.0f * Pi / 5.0f;
            dots.push_back(Vector2{std::cos(a) * 4.5f, std::sin(a) * 4.5f});
        }
    } else if (l.logo == LogoCross) {
        dots = {{0.0f, 0.0f}, {-4.0f, 0.0f}, {4.0f, 0.0f}, {0.0f, -4.0f}, {0.0f, 4.0f}};
    } else if (l.logo == LogoBadge) {
        dots = {{0.0f, 0.0f}};
        size = 4.0f;
    }
    const Color mark = l.logo == LogoBadge ? GOLD : WHITE;
    for (const Vector2 &d : dots) {
        DrawCircleV(Vector2{cx + d.x, ly + d.y}, size, mark);
    }
    DrawRectangleLinesEx(r, selected ? 2.0f : 1.0f, selected ? GOLD : GRAY);
}

// Small face of a fighter; a null enemy is the player's own avatar.
void drawPortrait(const Enemy *e, Rectangle r, bool selected) {
    if (e == nullptr) {
        drawLookPortrait(gPlayerLook, ShirtColors[gPlayerLook.shirt], r, selected);
    } else {
        const Color shirt = e->team == TeamBlue ? Color{60, 110, 220, 255} : Color{200, 60, 60, 255};
        drawLookPortrait(e->look, shirt, r, selected);
    }
}

void drawShop(const Game &g) {
    const auto px = [](float v) { return static_cast<int>(v); };
    const Rectangle p = shopPanel();
    const Vector2 m = GetMousePosition();
    DrawRectangleRec(p, Color{0, 0, 0, 200});
    DrawRectangleLinesEx(p, 2.0f, RAYWHITE);
    DrawText("BUY MENU", px(p.x + 20.0f), px(p.y + 18.0f), 30, RAYWHITE);
    DrawText(TextFormat("Money: $%d", g.money), px(p.x + 20.0f), px(p.y + 54.0f), 20, GOLD);

    // Category tabs; the active one is the category of the selected item.
    const int active = shopCatOf(g.shopSel);
    for (int c = 0; c < ShopCatCount; ++c) {
        const Rectangle t = shopTabRect(c);
        Color fill = Color{35, 35, 35, 255};
        if (c == active) {
            fill = Color{90, 90, 110, 255};
        } else if (CheckCollisionPointRec(m, t)) {
            fill = Color{60, 60, 60, 255};
        }
        DrawRectangleRec(t, fill);
        DrawRectangleLinesEx(t, 1.0f, GRAY);
        const int tw = MeasureText(ShopCatNames[c], 18);
        DrawText(ShopCatNames[c], px(t.x + (t.width - static_cast<float>(tw)) * 0.5f), px(t.y + 5.0f), 18, RAYWHITE);
    }

    // Scrolling list, clipped to its area.
    const Rectangle area = shopListArea();
    DrawRectangleRec(area, Color{25, 25, 25, 255});
    const std::vector<ShopLine> lines = shopLines();
    int keyOf[ShopCount];
    shopKeyMap(g, keyOf);
    BeginScissorMode(px(area.x), px(area.y), px(area.width), px(area.height));
    for (int li = 0; li < static_cast<int>(lines.size()); ++li) {
        const Rectangle r = shopLineRect(g, li);
        if (r.y + ShopRowH < area.y || r.y > area.y + area.height) {
            continue;
        }
        const ShopLine &l = lines[static_cast<size_t>(li)];
        if (l.row < 0) {
            DrawText(ShopCatNames[l.cat], px(r.x + 4.0f), px(r.y + 2.0f), 22, GOLD);
            DrawLine(px(r.x), px(r.y + ShopRowH - 1.0f), px(r.x + r.width), px(r.y + ShopRowH - 1.0f), GOLD);
            continue;
        }
        const int i = l.row;
        const bool hover = CheckCollisionPointRec(m, r) && CheckCollisionPointRec(m, area);
        DrawRectangle(px(r.x), px(r.y + 1.0f), px(r.width), px(r.height - 2.0f),
                      hover ? Color{70, 70, 70, 255} : Color{40, 40, 40, 255});
        if (i == g.shopSel) {
            DrawRectangleLinesEx(Rectangle{r.x, r.y + 1.0f, r.width, r.height - 2.0f}, 1.0f, GOLD);
        }
        std::string label = keyOf[i] >= 0 ? std::string("[") + ShopKeyNames[keyOf[i]] + "] " : std::string("    ");
        label += rowName(i);
        if (i >= ShopWeaponCount && i - ShopWeaponCount >= GearFrag) {
            label += TextFormat("  (x%d)", g.stock[throwOf(i - ShopWeaponCount)]);
        }
        DrawText(label.c_str(), px(r.x + 10.0f), px(r.y + 6.0f), 20, RAYWHITE);
        std::string price;
        Color priceColor = GOLD;
        if (rowFull(g, i)) {
            price = i < ShopWeaponCount ? "OWNED" : "FULL";
            priceColor = GREEN;
        } else {
            price = "$" + std::to_string(rowPrice(i));
            priceColor = g.money >= rowPrice(i) ? GOLD : RED;
        }
        const int tw = MeasureText(price.c_str(), 20);
        DrawText(price.c_str(), px(r.x + r.width) - 10 - tw, px(r.y + 6.0f), 20, priceColor);
    }
    EndScissorMode();

    // Scroll bar, only when the list overflows.
    const float total = static_cast<float>(lines.size()) * ShopRowH;
    if (total > ShopListH) {
        const float thumb = ShopListH * ShopListH / total;
        const float ty = area.y + (g.shopScroll / (total - ShopListH)) * (ShopListH - thumb);
        DrawRectangleRec(Rectangle{area.x + area.width + 4.0f, area.y, 6.0f, area.height}, Color{50, 50, 50, 255});
        DrawRectangleRec(Rectangle{area.x + area.width + 4.0f, ty, 6.0f, thumb}, GRAY);
    }

    // Preview with stats.
    const Rectangle pv{p.x + 580.0f, p.y + ShopTabY, PreviewPx, PreviewPx};
    DrawRectangleRec(pv, Color{20, 20, 24, 255});
    drawShopPreview(g, pv);
    DrawRectangleLinesEx(pv, 2.0f, RAYWHITE);
    DrawText(rowName(g.shopSel), px(pv.x + 8.0f), px(pv.y + PreviewPx + 10.0f), 22, RAYWHITE);
    const float sy = pv.y + PreviewPx + 44.0f;
    if (g.shopSel < ShopWeaponCount) {
        const WeaponDef &def = Weapons[ShopItems[g.shopSel]];
        DrawText(TextFormat("Magazine: %d", def.mag), px(pv.x), px(sy), 20, RAYWHITE);
        DrawText(TextFormat("Reload: %.1f s", static_cast<double>(def.reload)), px(pv.x), px(sy + 26.0f), 20, RAYWHITE);
        DrawText(def.autoFire ? "Fire: full-auto" : "Fire: semi-auto", px(pv.x), px(sy + 52.0f), 20, RAYWHITE);
        DrawText(TextFormat("Weight: %.1f kg", static_cast<double>(def.weight)), px(pv.x), px(sy + 78.0f), 20, RAYWHITE);
    }

    // Who the purchases go to; in team mode each teammate gets a portrait.
    const std::vector<int> targets = buyTargets(g);
    const Enemy *buyer = allyOf(g, g.shopAlly);
    DrawText(("Buying for: " + (buyer != nullptr ? botName(*buyer) : std::string("You"))).c_str(),
             px(p.x + 580.0f), px(p.y + 470.0f), 18, RAYWHITE);
    for (int s = 0; s < static_cast<int>(targets.size()) && s < ShopPortraitMax; ++s) {
        const int uid = targets[static_cast<size_t>(s)];
        const Enemy *face = allyOf(g, uid);
        if (uid >= 0 && face == nullptr) {
            continue;
        }
        drawPortrait(face, shopPortraitRect(s), uid == g.shopAlly);
    }

    // Return button: the newest purchase of this wave break.
    const Rectangle back = shopBackRect();
    const bool canReturn = !g.buyHistory.empty();
    Color backFill = Color{40, 40, 40, 255};
    if (canReturn && CheckCollisionPointRec(m, back)) {
        backFill = Color{80, 80, 80, 255};
    }
    DrawRectangleRec(back, backFill);
    DrawRectangleLinesEx(back, 1.0f, canReturn ? RAYWHITE : GRAY);
    std::string backText = "< Nothing to return this wave";
    if (canReturn) {
        const BuySnapshot &last = g.buyHistory.back();
        backText = "< Return " + last.name + " (+$" + std::to_string(last.price) + ")";
    }
    DrawText(backText.c_str(), px(back.x + 10.0f), px(back.y + 6.0f), 18, canReturn ? RAYWHITE : GRAY);

    DrawText("Click or 1-9 / 0 / - to buy. Wheel or UP/DOWN scrolls. Backspace returns. B / ESC closes.",
             px(p.x + 20.0f), px(p.y + ShopHeight - 20.0f), 16, LIGHTGRAY);
}

// Faint name of the held item in the top half of the screen for two seconds after a switch.
void drawHoldLabel(const Game &g) {
    if (g.holdTimer <= 0.0f) {
        return;
    }
    const float alpha = std::min(1.0f, g.holdTimer / 0.5f) * 0.7f;
    const int w = MeasureText(g.holdText.c_str(), 28);
    DrawText(g.holdText.c_str(), (GetScreenWidth() - w) / 2, GetScreenHeight() / 4, 28, Fade(RAYWHITE, alpha));
}

void drawRoundOverlay(const Game &g) {
    drawHoldLabel(g);
    if (g.waveBanner > 0.0f) {
        const std::string msg = "WAVE " + std::to_string(g.wave);
        const int w = MeasureText(msg.c_str(), 60);
        DrawText(msg.c_str(), (GetScreenWidth() - w) / 2, GetScreenHeight() / 3, 60,
                 Fade(RAYWHITE, std::min(1.0f, g.waveBanner)));
    }
    if (g.intermission > 0.0f) {
        const bool buyOpen = !g.online || g.intermission <= RoundBuyTime;
        const int secs = static_cast<int>(std::ceil(g.intermission));
        std::string msg;
        if (!g.online) {
            msg = "Wave " + std::to_string(g.wave + 1) + " in " + std::to_string(secs) + " s";
        } else if (buyOpen) {
            msg = "Buy time: " + std::to_string(secs) + " s";
        } else {
            msg = g.roundMsg + " - buying in " + std::to_string(static_cast<int>(std::ceil(g.intermission - RoundBuyTime))) + " s";
        }
        const int w = MeasureText(msg.c_str(), 40);
        DrawText(msg.c_str(), (GetScreenWidth() - w) / 2, 120, 40, RAYWHITE);
        if (buyOpen && !g.shopOpen) {
            const int hw = MeasureText("Press B to buy weapons", 24);
            DrawText("Press B to buy weapons", (GetScreenWidth() - hw) / 2, 170, 24, GOLD);
        }
    }
    if (g.shopOpen) {
        drawShop(g);
    }
}

// Online: a player who runs out of health stays out until the next round starts.
void placeOnlineSpawn(Game &g) {
    Vector3 p = g.level.spawn;
    if (!gSettings.teamMode) {
        openFloorSpot(g, 0.0f, p);
    } else if (g.localTeam == TeamRed) {
        // Red spawns on the half of the map opposite the blue spawn.
        const float half = g.level.terrain.half;
        for (int tries = 0; tries < 400; ++tries) {
            const float x = randRange(-half + 4.0f, half - 4.0f);
            const float z = randRange(-half + 4.0f, half - 4.0f);
            if (blockedAt(g.level, x, z, EnemyRadius) || x * g.level.spawn.x + z * g.level.spawn.z > 0.0f) {
                continue;
            }
            p = Vector3{x, groundAt(g.level.terrain, x, z), z};
            break;
        }
    }
    g.pos = p;
    g.pos.y = groundAt(g.level.terrain, p.x, p.z);
    g.vy = 0.0f;
    g.grounded = true;
    g.health = PlayerMaxHealth;
    g.lastHealth = PlayerMaxHealth;
}

void updateOnlineLife(Game &g) {
    if (!g.dead && g.health <= 0.0f) {
        g.dead = true;
        g.deathSent = false;
        g.health = 0.0f;
        g.lastHit = "You were killed";
        g.hitTextTimer = 2.0f;
    }
}

void update(Game &g, Audio &a, float dt) {
    updateRound(g, a, dt);
    updateShop(g, a);

    // Zoom narrows the view, so scale look by it to keep on-screen mouse speed the same.
    const float zoomScale = std::tan(g.cam.fovy * 0.5f * DEG2RAD) / std::tan(BaseFov * 0.5f * DEG2RAD);
    const Vector2 d = g.shopOpen ? Vector2{0.0f, 0.0f} : GetMouseDelta();
    g.yaw -= d.x * 0.0025f * zoomScale;
    g.pitch = std::clamp(g.pitch - d.y * 0.0025f * zoomScale, -1.5f, 1.5f);

    updateDoors(g, dt);
    updatePlayer(g, dt);
    if (!g.online || g.netHost) {
        updateCars(g, dt);
    }

    const float stride = g.height < StandHeight - 0.2f ? CrouchStride : StepStride;
    if (g.grounded && Vector3Length(g.playerMove) > 0.0f) {
        g.stepDist += Vector3Length(g.playerMove) * dt;
        if (g.stepDist >= stride) {
            g.stepDist = 0.0f;
            playStep(a);
        }
    }

    g.cam.position = {g.pos.x, g.pos.y + g.height - 0.15f, g.pos.z};
    if (g.shake > 0.0f) {
        const float s = g.shake * 0.08f;
        g.cam.position.x += randRange(-s, s);
        g.cam.position.y += randRange(-s, s);
    }
    g.cam.target = Vector3Add(g.cam.position, forwardOf(g));

    // Scope only stays on while a scoped rifle is out and the player is free to act.
    if (playerFrozen(g) || g.holding || !Weapons[g.weapon].scope) {
        g.scoped = false;
    }
    const float fovTarget = g.scoped ? ScopeFov : BaseFov;
    g.cam.fovy += (fovTarget - g.cam.fovy) * std::min(1.0f, dt * 14.0f);

    g.fireTimer = std::max(0.0f, g.fireTimer - dt);
    g.tracerTimer = std::max(0.0f, g.tracerTimer - dt);
    g.muzzleTimer = std::max(0.0f, g.muzzleTimer - dt);
    g.fireGlow = std::max(0.0f, g.fireGlow - dt);
    g.hitTextTimer = std::max(0.0f, g.hitTextTimer - dt);
    g.hurtTimer = std::max(0.0f, g.hurtTimer - dt);
    g.shake = std::max(0.0f, g.shake - dt * 2.5f);
    g.flashed = std::max(0.0f, g.flashed - dt);
    g.killFlash = std::max(0.0f, g.killFlash - dt);
    for (KillEntry &k : g.killFeed) {
        k.age += dt;
    }
    g.killFeed.erase(std::remove_if(g.killFeed.begin(), g.killFeed.end(),
                                    [](const KillEntry &k) { return k.age > KillFeedTime; }),
                     g.killFeed.end());
    g.waveBanner = std::max(0.0f, g.waveBanner - dt);
    g.holdTimer = std::max(0.0f, g.holdTimer - dt);
    for (BloodSplat &s : g.splats) {
        s.age += dt;
    }
    g.splats.erase(std::remove_if(g.splats.begin(), g.splats.end(),
                                  [](const BloodSplat &s) { return s.age > SplatLife; }),
                   g.splats.end());
    g.kick = std::max(0.0f, g.kick - dt * 10.0f);
    g.kickTimer = std::max(0.0f, g.kickTimer - dt);
    g.saberSwing = std::max(0.0f, g.saberSwing - dt);

    updateReload(g, dt);
    updateAmmoBoxes(g, dt);
    updateDrops(g);
    updateGrenades(g, a, dt);
    updateLingering(g, a, dt);

    const bool locked = playerFrozen(g);
    if (!locked) {
        // ONE = primary, TWO = sidearm, THREE = knife.
        const bool tasterKey = IsMouseButtonPressed(TaserMouseButton);
        const bool weaponKey = IsKeyPressed(KEY_ONE) || IsKeyPressed(KEY_TWO) || IsKeyPressed(KEY_THREE) || tasterKey;
        int picked = g.weapon;
        if (IsKeyPressed(KEY_ONE) && g.primary >= 0) {
            picked = g.primary;
        } else if (IsKeyPressed(KEY_TWO)) {
            picked = g.sidearm;
        } else if (IsKeyPressed(KEY_THREE)) {
            picked = WeaponKnife;
        } else if (tasterKey) {
            picked = WeaponTaser;
        }
        if (picked != g.weapon) {
            g.weapon = picked;
            g.reloadTimer = 0.0f;
            g.scoped = false;
        }
        if (weaponKey) {
            g.holding = false;
            announceHeld(g);
        }
        if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) && Weapons[g.weapon].scope && !g.holding) {
            g.scoped = !g.scoped;
        }
        if (g.weapon == WeaponTaser) {
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                zap(g, a);
            }
        } else if (g.weapon == WeaponKnife) {
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                if (saberHeld(g)) {
                    g.saberSwing = SaberSwingTime;
                    if (a.ready) {
                        playAt(a.saberSwing, 0.8f, 0.5f);
                    }
                }
                stab(g);
            }
        } else {
            const bool wantFire = Weapons[g.weapon].autoFire && g.rapid ? IsMouseButtonDown(MOUSE_BUTTON_LEFT)
                                                                       : IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
            if (wantFire) {
                shoot(g, a);
            }
        }
        for (int k = 0; k < ThrowCount; ++k) {
            if (IsKeyPressed(KEY_FOUR + k)) {
                g.throwSel = k;
                g.holding = g.stock[k] > 0;
                announceHeld(g);
            }
        }
        if (IsKeyPressed(KEY_X)) {
            g.rapid = !g.rapid;
        }
        if (IsKeyPressed(KEY_G)) {
            kickBall(g);
        }
        if (IsKeyPressed(KEY_N)) {
            g.night = !g.night;
            g.flashlight = false;
            g.birds.clear();
        }
        if (g.night && IsKeyPressed(KEY_F)) {
            g.flashlight = !g.flashlight;
        }
    }

    if (!locked) {
        updateEnemies(g, a, dt);
    }
    reapEnemies(g);
    if (g.freshKill) {
        g.freshKill = false;
        onKill(g, a);
    }
    updateBalls(g, dt);
    for (Corpse &c : g.corpses) {
        updateCorpse(g.level, c, dt);
    }
    g.corpses.erase(std::remove_if(g.corpses.begin(), g.corpses.end(),
                                   [](const Corpse &c) { return c.age > CorpseTime; }),
                    g.corpses.end());
    updateBirds(g, dt);
    updateAmbience(g, a, dt);
    updateSaberAudio(g, a);
    if (g.health < g.lastHealth) {
        onPlayerHurt(g, a);
    }
    g.lastHealth = g.health;
    if (g.online) {
        updateOnlineLife(g);
        return;
    }
    if (g.health <= 0.0f) {
        respawnPlayer(g);
        return;
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
    SetMusicVolume(pl.music, pl.muted ? 0.0f : pl.volume);
    PlayMusicStream(pl.music);
}

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
    SetMusicVolume(pl.music, pl.muted ? 0.0f : pl.volume);
    const float length = GetMusicTimeLength(pl.music);
    if (!IsMusicStreamPlaying(pl.music) || GetMusicTimePlayed(pl.music) >= length - 0.1f) {
        loadTrack(pl, pickNext(pl));
    }
}

void handleMusicKeys(Playlist &pl) {
    const int n = static_cast<int>(pl.tracks.size());
    if (n > 0) {
        if (IsKeyPressed(KEY_RIGHT)) {
            loadTrack(pl, pickNext(pl));
        }
        if (IsKeyPressed(KEY_LEFT)) {
            loadTrack(pl, (std::max(pl.current, 0) + n - 1) % n);
        }
    }
    if (IsKeyPressed(KEY_UP) || IsKeyPressedRepeat(KEY_UP)) {
        pl.volume = std::min(1.0f, pl.volume + VolumeStep);
    }
    if (IsKeyPressed(KEY_DOWN) || IsKeyPressedRepeat(KEY_DOWN)) {
        pl.volume = std::max(0.0f, pl.volume - VolumeStep);
    }
    if (IsKeyPressed(KEY_M)) {
        pl.muted = !pl.muted;
    }
}

Font gMusicFont{};
bool gMusicFontOwned = false;
constexpr int MusicFontSize = 20;

// Glyphs come from the track file names, so new tracks need a rebuild.
void rebuildMusicFont(const Playlist &pl) {
    if (gMusicFontOwned) {
        UnloadFont(gMusicFont);
        gMusicFontOwned = false;
    }
    std::vector<int> cps;
    for (int c = 32; c < 127; ++c) {
        cps.push_back(c);
    }
    for (int c = 0x400; c < 0x500; ++c) {
        cps.push_back(c);
    }
    for (const std::string &t : pl.tracks) {
        int n = 0;
        int *codes = LoadCodepoints(GetFileNameWithoutExt(t.c_str()), &n);
        cps.insert(cps.end(), codes, codes + n);
        UnloadCodepoints(codes);
    }
    std::sort(cps.begin(), cps.end());
    cps.erase(std::unique(cps.begin(), cps.end()), cps.end());
    const char *const candidates[] = {
        "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/wenquanyi/wqy-zenhei/wqy-zenhei.ttc",
        "/usr/share/fonts/dejavu/DejaVuSans.ttf",
    };
    for (const char *path : candidates) {
        if (!FileExists(path)) {
            continue;
        }
        gMusicFont = LoadFontEx(path, MusicFontSize, cps.data(), static_cast<int>(cps.size()));
        if (gMusicFont.texture.id != 0) {
            gMusicFontOwned = true;
            return;
        }
    }
    gMusicFont = GetFontDefault();
}

void scanMusic(Playlist &pl, const std::string &dir) {
    if (!DirectoryExists(dir.c_str())) {
        MakeDirectory(dir.c_str());
    }
    FilePathList files = LoadDirectoryFilesEx(dir.c_str(), ".mp3;.MP3", false);
    for (unsigned int i = 0; i < files.count; ++i) {
        bool known = false;
        for (const std::string &t : pl.tracks) {
            known = known || t == files.paths[i];
        }
        if (!known) {
            pl.tracks.push_back(files.paths[i]);
        }
    }
    UnloadDirectoryFiles(files);
}

void rescanMusic(Playlist &pl) {
    scanMusic(pl, std::string(GetApplicationDirectory()) + "music");
    rebuildMusicFont(pl);
    if (!pl.loaded && !pl.tracks.empty()) {
        loadTrack(pl, pickNext(pl));
    }
}

// Rotates a model part around its own Z axis at zCenter, barrel length along Z.
void drawBarrelZ(float zCenter, float radius, float length, Color col) {
    rlPushMatrix();
    rlTranslatef(0.0f, 0.0f, zCenter);
    rlRotatef(90.0f, 1.0f, 0.0f, 0.0f);
    DrawCylinder(Vector3{0.0f, -length * 0.5f, 0.0f}, radius, radius, length, 10, col);
    rlPopMatrix();
}

// Gun and knife geometry centred on the origin, barrel toward +Z.
// Only true while drawing the player's own weapon, so enemies keep the default look.
bool gSkinsActive = false;

// Skin colour for the weapon in hand, or the fallback when no skin is equipped.
Color skinColor(int weapon, Color fallback) {
    static_assert(WeaponCount <= SkinSlots, "raise SkinSlots");
    if (!gSkinsActive) {
        return fallback;
    }
    if (weapon == WeaponKnife) {
        return gInv.equipKnife >= 0 ? knifeSkins()[gInv.equipKnife].color : fallback;
    }
    const int s = gInv.equipGun[weapon];
    return s >= 0 ? gunSkins()[s].color : fallback;
}

// Shows another player's gun and knife skins while their model is drawn.
struct RemoteSkinScope {
    int weapon;
    int gun;
    int knife;
    explicit RemoteSkinScope(const Enemy &e)
        : weapon(e.weapon), gun(gInv.equipGun[e.weapon]), knife(gInv.equipKnife) {
        gInv.equipGun[weapon] = e.gunSkin;
        gInv.equipKnife = e.knifeSkin;
        gSkinsActive = true;
    }
    ~RemoteSkinScope() {
        gInv.equipGun[weapon] = gun;
        gInv.equipKnife = knife;
        gSkinsActive = false;
    }
};

// Knife models by type (see knifeSkins()), centred on the grip with the blade toward +Z.
void drawKnifeModel(int type, Color grip) {
    const Color steel{205, 205, 215, 255};
    const Color edge{245, 245, 255, 255};
    const Color ringSteel{150, 150, 160, 255};
    const Vector3 zero{0.0f, 0.0f, 0.0f};
    if (type == 1) {
        // Butterfly: twin blades splayed from the handle.
        DrawCubeV(zero, Vector3{0.04f, 0.06f, 0.14f}, grip);
        for (int side = -1; side <= 1; side += 2) {
            rlPushMatrix();
            rlRotatef(static_cast<float>(side) * 12.0f, 0.0f, 1.0f, 0.0f);
            DrawCubeV(Vector3{0.0f, 0.0f, 0.2f}, Vector3{0.03f, 0.05f, 0.4f}, steel);
            DrawCubeV(Vector3{0.0f, -0.026f, 0.2f}, Vector3{0.006f, 0.004f, 0.4f}, edge);
            rlPopMatrix();
        }
    } else if (type == 2) {
        // Bayonet: long straight blade with a cross guard.
        DrawCubeV(zero, Vector3{0.03f, 0.05f, 0.14f}, grip);
        DrawCubeV(Vector3{0.0f, 0.0f, 0.08f}, Vector3{0.08f, 0.02f, 0.02f}, ringSteel);
        DrawCubeV(Vector3{0.0f, 0.0f, 0.3f}, Vector3{0.02f, 0.035f, 0.42f}, steel);
        DrawCubeV(Vector3{0.0f, -0.0175f, 0.3f}, Vector3{0.006f, 0.004f, 0.42f}, edge);
    } else if (type == 3) {
        // Huntsman: wide blade with a row of teeth on the back edge.
        DrawCubeV(zero, Vector3{0.036f, 0.07f, 0.16f}, grip);
        DrawCubeV(Vector3{0.0f, 0.0f, 0.3f}, Vector3{0.02f, 0.06f, 0.34f}, steel);
        for (int t = 0; t < 6; ++t) {
            DrawCubeV(Vector3{0.0f, 0.04f, 0.15f + static_cast<float>(t) * 0.05f}, Vector3{0.012f, 0.02f, 0.02f}, steel);
        }
    } else if (type == 4) {
        // Falchion: wide curved blade with no guard.
        DrawCubeV(zero, Vector3{0.034f, 0.07f, 0.14f}, grip);
        float z = 0.07f;
        float y = 0.0f;
        float ang = 0.0f;
        for (int s = 0; s < 8; ++s) {
            const float len = 0.05f;
            const float h = 0.1f - static_cast<float>(s) * 0.008f;
            rlPushMatrix();
            rlTranslatef(0.0f, y + std::sin(ang) * len * 0.5f, z + std::cos(ang) * len * 0.5f);
            rlRotatef(-ang * RAD2DEG, 1.0f, 0.0f, 0.0f);
            DrawCubeV(zero, Vector3{0.02f, h, len * 1.05f}, steel);
            DrawCubeV(Vector3{0.0f, -h * 0.5f, 0.0f}, Vector3{0.007f, 0.004f, len * 1.05f}, edge);
            rlPopMatrix();
            z += std::cos(ang) * len;
            y += std::sin(ang) * len;
            ang += 0.1f;
        }
    } else if (type == 5) {
        // Flip: short blade folded out of a thick handle.
        DrawCubeV(zero, Vector3{0.04f, 0.09f, 0.22f}, grip);
        DrawCubeV(Vector3{0.0f, 0.0f, 0.2f}, Vector3{0.02f, 0.05f, 0.18f}, steel);
        DrawCubeV(Vector3{0.0f, -0.026f, 0.2f}, Vector3{0.006f, 0.004f, 0.18f}, edge);
    } else if (type == KnifeLightsaber) {
        // Lightsaber: dark hilt with an emitter ring and a glowing blade along +Z.
        const Color hilt{70, 70, 78, 255};
        const Color core{235, 250, 255, 255};
        const Color halo{60, 160, 255, 140};
        DrawCubeV(zero, Vector3{0.04f, 0.04f, 0.2f}, hilt);
        DrawCubeV(Vector3{0.0f, 0.0f, 0.12f}, Vector3{0.05f, 0.05f, 0.03f}, ringSteel);
        rlPushMatrix();
        rlTranslatef(0.0f, 0.0f, 0.125f);
        rlRotatef(90.0f, 1.0f, 0.0f, 0.0f);
        DrawCylinder(zero, 0.024f, 0.024f, 0.8f, 10, halo);
        DrawCylinder(zero, 0.012f, 0.012f, 0.8f, 10, core);
        rlPopMatrix();
    } else {
        // Karambit: dark scales, steel bolster, finger ring, tapered curved blade with a cutting edge.
        const Color scales{70, 46, 30, 255};
        DrawCubeV(zero, Vector3{0.034f, 0.07f, 0.16f}, grip);
        DrawCubeV(Vector3{0.019f, 0.0f, 0.0f}, Vector3{0.006f, 0.062f, 0.14f}, scales);
        DrawCubeV(Vector3{-0.019f, 0.0f, 0.0f}, Vector3{0.006f, 0.062f, 0.14f}, scales);
        DrawCubeV(Vector3{0.0f, 0.0f, 0.085f}, Vector3{0.044f, 0.078f, 0.02f}, ringSteel);
        for (int k = 0; k < 16; ++k) {
            const float a = static_cast<float>(k) * (2.0f * Pi / 16.0f);
            DrawSphere(Vector3{std::cos(a) * 0.05f, -0.08f + std::sin(a) * 0.05f, -0.1f}, 0.0095f, ringSteel);
        }
        float z = 0.095f;
        float y = 0.0f;
        float ang = 0.0f;
        for (int s = 0; s < 10; ++s) {
            const float len = 0.04f;
            const float h = 0.05f * (1.0f - static_cast<float>(s) / 12.0f) + 0.012f;
            rlPushMatrix();
            rlTranslatef(0.0f, y + std::sin(ang) * len * 0.5f, z + std::cos(ang) * len * 0.5f);
            rlRotatef(-ang * RAD2DEG, 1.0f, 0.0f, 0.0f);
            DrawCubeV(zero, Vector3{0.018f, h, len * 1.05f}, steel);
            DrawCubeV(Vector3{0.0f, -h * 0.5f, 0.0f}, Vector3{0.0065f, 0.004f, len * 1.05f}, edge);
            rlPopMatrix();
            z += std::cos(ang) * len;
            y += std::sin(ang) * len;
            ang += 0.12f;
        }
    }
}

void drawWeaponParts(int weapon, bool muzzle) {
    const Color steel{30, 30, 32, 255};
    const Color mag{50, 50, 50, 255};
    const Color wood = skinColor(weapon, Color{110, 70, 35, 255});
    if (weapon == WeaponTaser) {
        // Taser: black housing, yellow band, grip, and two prongs at the front.
        const Color housing{28, 28, 30, 255};
        const Color yellow{245, 205, 30, 255};
        DrawCubeV(Vector3{0.0f, 0.0f, 0.0f}, Vector3{0.07f, 0.1f, 0.26f}, housing);
        DrawCubeV(Vector3{0.0f, 0.052f, 0.0f}, Vector3{0.072f, 0.012f, 0.26f}, yellow);
        DrawCubeV(Vector3{0.0f, -0.11f, -0.07f}, Vector3{0.06f, 0.14f, 0.07f}, housing);
        DrawCubeV(Vector3{-0.02f, 0.0f, 0.2f}, Vector3{0.012f, 0.012f, 0.1f}, steel);
        DrawCubeV(Vector3{0.02f, 0.0f, 0.2f}, Vector3{0.012f, 0.012f, 0.1f}, steel);
        if (muzzle) {
            DrawSphere(Vector3{0.0f, 0.0f, 0.27f}, 0.03f, Color{120, 200, 255, 255});
        }
        return;
    }
    if (weapon == WeaponKnife) {
        const int type = gSkinsActive && gInv.equipKnife >= 0 ? gInv.equipKnife : 0;
        drawKnifeModel(type, skinColor(WeaponKnife, Color{24, 24, 26, 255}));
    } else if (weapon == WeaponPistol) {
        DrawCubeV(Vector3{0.0f, 0.03f, 0.12f}, Vector3{0.06f, 0.07f, 0.26f}, steel);
        DrawCubeV(Vector3{0.0f, -0.07f, 0.04f}, Vector3{0.05f, 0.13f, 0.07f}, wood);
        if (muzzle) {
            DrawSphere(Vector3{0.0f, 0.03f, 0.27f}, 0.07f, Color{255, 200, 60, 230});
        }
    } else if (weapon == WeaponSMG) {
        DrawCubeV(Vector3{0.0f, 0.0f, 0.0f}, Vector3{0.07f, 0.08f, 0.34f}, steel);
        DrawCubeV(Vector3{0.0f, -0.12f, 0.02f}, Vector3{0.045f, 0.16f, 0.06f}, mag);
        DrawCubeV(Vector3{0.0f, -0.02f, -0.27f}, Vector3{0.05f, 0.09f, 0.14f}, wood);
        DrawCubeV(Vector3{0.0f, 0.0f, 0.22f}, Vector3{0.03f, 0.03f, 0.16f}, steel);
        if (muzzle) {
            DrawSphere(Vector3{0.0f, 0.0f, 0.34f}, 0.08f, Color{255, 200, 60, 230});
        }
    } else if (weapon == WeaponAWP) {
        DrawCubeV(Vector3{0.0f, 0.0f, 0.0f}, Vector3{0.07f, 0.1f, 0.5f}, steel);
        DrawCubeV(Vector3{0.0f, 0.0f, 0.38f}, Vector3{0.03f, 0.03f, 0.4f}, steel);
        DrawCubeV(Vector3{0.0f, -0.02f, -0.36f}, Vector3{0.06f, 0.1f, 0.3f}, wood);
        DrawCubeV(Vector3{0.0f, 0.08f, 0.02f}, Vector3{0.05f, 0.05f, 0.14f}, mag);
        DrawCubeV(Vector3{0.0f, 0.12f, 0.02f}, Vector3{0.04f, 0.06f, 0.26f}, Color{20, 20, 20, 255});  // scope
        if (muzzle) {
            DrawSphere(Vector3{0.0f, 0.0f, 0.6f}, 0.1f, Color{255, 200, 60, 230});
        }
    } else if (weapon == WeaponMac10) {
        DrawCubeV(Vector3{0.0f, 0.0f, 0.0f}, Vector3{0.07f, 0.09f, 0.3f}, steel);
        DrawCubeV(Vector3{0.0f, 0.05f, -0.02f}, Vector3{0.06f, 0.02f, 0.24f}, mag);
        DrawCubeV(Vector3{0.0f, -0.13f, 0.0f}, Vector3{0.05f, 0.16f, 0.06f}, mag);
        DrawCubeV(Vector3{0.0f, -0.09f, -0.1f}, Vector3{0.05f, 0.12f, 0.06f}, steel);
        drawBarrelZ(0.25f, 0.03f, 0.14f, steel);  // suppressor
        if (muzzle) {
            DrawSphere(Vector3{0.0f, 0.0f, 0.34f}, 0.07f, Color{255, 200, 60, 230});
        }
    } else if (weapon == WeaponM4) {
        DrawCubeV(Vector3{0.0f, 0.0f, 0.0f}, Vector3{0.07f, 0.09f, 0.44f}, steel);
        DrawCubeV(Vector3{0.0f, 0.085f, 0.02f}, Vector3{0.03f, 0.04f, 0.2f}, mag);      // carry handle
        DrawCubeV(Vector3{0.0f, -0.01f, 0.3f}, Vector3{0.065f, 0.065f, 0.2f}, mag);     // handguard
        drawBarrelZ(0.52f, 0.02f, 0.26f, steel);
        DrawCubeV(Vector3{0.0f, -0.13f, 0.05f}, Vector3{0.05f, 0.2f, 0.09f}, mag);      // magazine
        DrawCubeV(Vector3{0.0f, -0.12f, -0.14f}, Vector3{0.05f, 0.14f, 0.06f}, wood);   // grip
        DrawCubeV(Vector3{0.0f, -0.02f, -0.38f}, Vector3{0.05f, 0.1f, 0.26f}, mag);     // stock
        DrawCubeV(Vector3{0.0f, 0.0f, -0.23f}, Vector3{0.04f, 0.05f, 0.14f}, steel);    // buffer tube
        if (muzzle) {
            DrawSphere(Vector3{0.0f, 0.0f, 0.67f}, 0.08f, Color{255, 200, 60, 230});
        }
    } else if (weapon == WeaponDeagle) {
        const Color gun{70, 70, 78, 255};
        DrawCubeV(Vector3{0.0f, 0.04f, 0.1f}, Vector3{0.065f, 0.09f, 0.3f}, gun);
        DrawCubeV(Vector3{0.0f, -0.01f, 0.05f}, Vector3{0.06f, 0.04f, 0.2f}, steel);
        DrawCubeV(Vector3{0.0f, -0.1f, -0.02f}, Vector3{0.055f, 0.14f, 0.07f}, wood);
        if (muzzle) {
            DrawSphere(Vector3{0.0f, 0.04f, 0.27f}, 0.08f, Color{255, 200, 60, 230});
        }
    } else if (weapon == WeaponP90) {
        const Color body{45, 42, 30, 255};
        DrawCubeV(Vector3{0.0f, 0.0f, 0.0f}, Vector3{0.08f, 0.1f, 0.42f}, body);
        DrawCubeV(Vector3{0.0f, 0.085f, 0.0f}, Vector3{0.05f, 0.04f, 0.22f}, Color{100, 100, 110, 255});
        DrawCubeV(Vector3{0.0f, -0.09f, 0.12f}, Vector3{0.04f, 0.1f, 0.05f}, mag);
        drawBarrelZ(0.36f, 0.02f, 0.2f, steel);
        DrawCubeV(Vector3{0.0f, -0.01f, -0.3f}, Vector3{0.07f, 0.09f, 0.14f}, body);
        if (muzzle) {
            DrawSphere(Vector3{0.0f, 0.0f, 0.48f}, 0.08f, Color{255, 200, 60, 230});
        }
    } else if (weapon == WeaponScout) {
        DrawCubeV(Vector3{0.0f, 0.0f, 0.0f}, Vector3{0.065f, 0.08f, 0.3f}, steel);
        drawBarrelZ(0.42f, 0.025f, 0.6f, steel);
        DrawCubeV(Vector3{0.0f, 0.1f, 0.05f}, Vector3{0.04f, 0.05f, 0.2f}, Color{20, 20, 20, 255});
        DrawCubeV(Vector3{0.0f, -0.03f, -0.35f}, Vector3{0.06f, 0.11f, 0.32f}, wood);
        DrawCubeV(Vector3{0.0f, -0.1f, 0.02f}, Vector3{0.045f, 0.12f, 0.06f}, mag);
        DrawSphere(Vector3{0.045f, 0.03f, -0.06f}, 0.02f, steel);
        if (muzzle) {
            DrawSphere(Vector3{0.0f, 0.0f, 0.74f}, 0.09f, Color{255, 200, 60, 230});
        }
    } else if (weapon == WeaponGlock) {
        DrawCubeV(Vector3{0.0f, 0.03f, 0.1f}, Vector3{0.055f, 0.06f, 0.24f}, steel);
        DrawCubeV(Vector3{0.0f, -0.07f, 0.02f}, Vector3{0.05f, 0.13f, 0.06f}, mag);
        if (muzzle) {
            DrawSphere(Vector3{0.0f, 0.03f, 0.23f}, 0.06f, Color{255, 200, 60, 230});
        }
    } else if (weapon == WeaponUsp) {
        DrawCubeV(Vector3{0.0f, 0.03f, 0.12f}, Vector3{0.06f, 0.075f, 0.28f}, steel);
        DrawCubeV(Vector3{0.0f, 0.03f, 0.3f}, Vector3{0.035f, 0.035f, 0.12f}, Color{20, 20, 20, 255});  // suppressor
        DrawCubeV(Vector3{0.0f, -0.07f, 0.03f}, Vector3{0.05f, 0.13f, 0.07f}, mag);
        if (muzzle) {
            DrawSphere(Vector3{0.0f, 0.03f, 0.37f}, 0.06f, Color{255, 200, 60, 230});
        }
    } else if (weapon == WeaponP250) {
        DrawCubeV(Vector3{0.0f, 0.03f, 0.1f}, Vector3{0.06f, 0.07f, 0.22f}, steel);
        DrawCubeV(Vector3{0.0f, -0.07f, 0.03f}, Vector3{0.05f, 0.13f, 0.07f}, wood);
        if (muzzle) {
            DrawSphere(Vector3{0.0f, 0.03f, 0.22f}, 0.07f, Color{255, 200, 60, 230});
        }
    } else if (weapon == WeaponFiveSeven) {
        DrawCubeV(Vector3{0.0f, 0.035f, 0.13f}, Vector3{0.055f, 0.065f, 0.3f}, Color{25, 25, 28, 255});
        DrawCubeV(Vector3{0.0f, -0.08f, 0.03f}, Vector3{0.05f, 0.14f, 0.07f}, mag);
        if (muzzle) {
            DrawSphere(Vector3{0.0f, 0.035f, 0.29f}, 0.06f, Color{255, 200, 60, 230});
        }
    } else if (weapon == WeaponRevolver) {
        drawBarrelZ(0.0f, 0.045f, 0.09f, steel);  // cylinder
        DrawCubeV(Vector3{0.0f, 0.02f, 0.1f}, Vector3{0.06f, 0.08f, 0.16f}, steel);
        drawBarrelZ(0.25f, 0.018f, 0.2f, steel);
        DrawCubeV(Vector3{0.0f, -0.08f, -0.02f}, Vector3{0.05f, 0.14f, 0.07f}, wood);
        if (muzzle) {
            DrawSphere(Vector3{0.0f, 0.02f, 0.36f}, 0.07f, Color{255, 200, 60, 230});
        }
    } else if (weapon == WeaponFamas) {
        const Color body{55, 60, 40, 255};
        DrawCubeV(Vector3{0.0f, 0.0f, 0.0f}, Vector3{0.07f, 0.09f, 0.4f}, body);
        DrawCubeV(Vector3{0.0f, -0.12f, 0.1f}, Vector3{0.05f, 0.14f, 0.07f}, mag);
        DrawCubeV(Vector3{0.0f, -0.14f, -0.14f}, Vector3{0.05f, 0.2f, 0.1f}, mag);  // magazine behind the grip
        drawBarrelZ(0.46f, 0.02f, 0.24f, steel);
        if (muzzle) {
            DrawSphere(Vector3{0.0f, 0.0f, 0.6f}, 0.08f, Color{255, 200, 60, 230});
        }
    } else if (weapon == WeaponGalil) {
        DrawCubeV(Vector3{0.0f, 0.0f, 0.0f}, Vector3{0.07f, 0.09f, 0.42f}, Color{55, 70, 50, 255});
        DrawCubeV(Vector3{0.0f, -0.01f, 0.3f}, Vector3{0.065f, 0.065f, 0.2f}, Color{40, 50, 35, 255});  // handguard
        drawBarrelZ(0.5f, 0.02f, 0.26f, steel);
        DrawCubeV(Vector3{0.0f, -0.14f, 0.02f}, Vector3{0.05f, 0.2f, 0.08f}, mag);
        DrawCubeV(Vector3{0.0f, -0.02f, -0.36f}, Vector3{0.06f, 0.1f, 0.28f}, wood);
        if (muzzle) {
            DrawSphere(Vector3{0.0f, 0.0f, 0.68f}, 0.08f, Color{255, 200, 60, 230});
        }
    } else if (weapon == WeaponMp5) {
        DrawCubeV(Vector3{0.0f, 0.0f, 0.0f}, Vector3{0.07f, 0.08f, 0.34f}, steel);
        DrawCubeV(Vector3{0.0f, -0.12f, 0.02f}, Vector3{0.045f, 0.16f, 0.06f}, mag);
        DrawCubeV(Vector3{0.0f, -0.02f, -0.28f}, Vector3{0.04f, 0.06f, 0.2f}, mag);
        drawBarrelZ(0.25f, 0.018f, 0.16f, steel);
        if (muzzle) {
            DrawSphere(Vector3{0.0f, 0.0f, 0.34f}, 0.07f, Color{255, 200, 60, 230});
        }
    } else if (weapon == WeaponNova) {
        DrawCubeV(Vector3{0.0f, 0.0f, 0.0f}, Vector3{0.07f, 0.09f, 0.3f}, wood);
        drawBarrelZ(0.36f, 0.024f, 0.5f, steel);
        DrawCubeV(Vector3{0.0f, -0.03f, 0.22f}, Vector3{0.065f, 0.06f, 0.14f}, wood);  // pump
        DrawCubeV(Vector3{0.0f, -0.03f, -0.36f}, Vector3{0.06f, 0.1f, 0.3f}, wood);
        if (muzzle) {
            DrawSphere(Vector3{0.0f, 0.0f, 0.63f}, 0.09f, Color{255, 200, 60, 230});
        }
    } else if (weapon == WeaponM249) {
        DrawCubeV(Vector3{0.0f, 0.0f, 0.0f}, Vector3{0.08f, 0.1f, 0.5f}, Color{50, 55, 40, 255});
        drawBarrelZ(0.5f, 0.028f, 0.34f, steel);
        DrawCubeV(Vector3{0.0f, -0.12f, -0.04f}, Vector3{0.07f, 0.12f, 0.12f}, mag);  // box magazine
        DrawCubeV(Vector3{0.03f, -0.13f, 0.34f}, Vector3{0.015f, 0.12f, 0.015f}, steel);  // bipod
        DrawCubeV(Vector3{-0.03f, -0.13f, 0.34f}, Vector3{0.015f, 0.12f, 0.015f}, steel);
        DrawCubeV(Vector3{0.0f, -0.02f, -0.4f}, Vector3{0.06f, 0.08f, 0.24f}, wood);
        if (muzzle) {
            DrawSphere(Vector3{0.0f, 0.0f, 0.72f}, 0.09f, Color{255, 200, 60, 230});
        }
    } else {
        DrawCubeV(Vector3{0.0f, 0.0f, 0.0f}, Vector3{0.07f, 0.09f, 0.42f}, steel);
        DrawCubeV(Vector3{0.0f, 0.02f, 0.52f}, Vector3{0.03f, 0.03f, 0.3f}, steel);
        DrawCubeV(Vector3{0.0f, 0.065f, 0.35f}, Vector3{0.025f, 0.03f, 0.25f}, steel);
        DrawCubeV(Vector3{0.0f, -0.01f, 0.3f}, Vector3{0.065f, 0.065f, 0.2f}, wood);
        DrawCubeV(Vector3{0.0f, -0.13f, 0.05f}, Vector3{0.05f, 0.2f, 0.09f}, mag);
        DrawCubeV(Vector3{0.0f, -0.12f, -0.14f}, Vector3{0.05f, 0.14f, 0.06f}, wood);
        DrawCubeV(Vector3{0.0f, 0.0f, -0.4f}, Vector3{0.06f, 0.1f, 0.3f}, wood);
        DrawCubeV(Vector3{0.0f, 0.075f, 0.05f}, Vector3{0.03f, 0.03f, 0.08f}, steel);
        if (muzzle) {
            DrawSphere(Vector3{0.0f, 0.02f, 0.72f}, 0.09f, Color{255, 200, 60, 230});
        }
    }
}

// Grenade model for the hand and the buy preview, centred on the origin.
void drawThrowParts(int kind) {
    const Color steel{30, 30, 32, 255};
    const Vector3 zero{0.0f, 0.0f, 0.0f};
    if (kind == ThrowFrag) {
        DrawSphere(zero, 0.07f, Color{70, 120, 70, 255});
        DrawCubeV(Vector3{0.0f, 0.08f, 0.0f}, Vector3{0.03f, 0.03f, 0.05f}, steel);
    } else if (kind == ThrowFlash) {
        DrawSphere(zero, 0.07f, Color{235, 235, 215, 255});
        DrawCubeV(Vector3{0.0f, 0.075f, 0.0f}, Vector3{0.04f, 0.03f, 0.04f}, steel);
    } else if (kind == ThrowSmoke) {
        DrawCylinder(Vector3{0.0f, -0.07f, 0.0f}, 0.05f, 0.05f, 0.14f, 12, Color{150, 150, 160, 255});
    } else {
        DrawCubeV(zero, Vector3{0.12f, 0.12f, 0.12f}, Color{235, 140, 60, 255});
    }
}

void drawArmorParts() {
    DrawCubeV(Vector3{0.0f, 0.0f, 0.0f}, Vector3{0.3f, 0.36f, 0.1f}, Color{60, 90, 150, 255});
    DrawCubeV(Vector3{0.0f, 0.02f, 0.07f}, Vector3{0.2f, 0.24f, 0.03f}, Color{90, 120, 180, 255});
}

void drawAmmoParts() {
    DrawCubeV(Vector3{0.0f, 0.0f, 0.0f}, Vector3{0.3f, 0.18f, 0.18f}, Color{110, 95, 55, 255});
    DrawCubeV(Vector3{0.0f, 0.1f, 0.0f}, Vector3{0.31f, 0.03f, 0.19f}, Color{220, 180, 40, 255});
}

// Lightsaber blade shader: a pulsing glow for the night halo around the blade.
const char *SaberFs = R"(#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
uniform float uTime;
out vec4 finalColor;
void main() {
    float pulse = 0.85 + 0.15 * sin(uTime * 18.0);
    finalColor = vec4(fragColor.rgb * pulse, fragColor.a * pulse);
}
)";

Shader gSaber{};

// Additive halo around the blade. The caller has the weapon matrix active; the blade points along +Z.
void drawSaberGlow() {
    const Vector3 zero{0.0f, 0.0f, 0.0f};
    const float t = static_cast<float>(GetTime());
    BeginBlendMode(BLEND_ADDITIVE);
    BeginShaderMode(gSaber);
    SetShaderValue(gSaber, GetShaderLocation(gSaber, "uTime"), &t, SHADER_UNIFORM_FLOAT);
    rlPushMatrix();
    rlTranslatef(0.0f, 0.0f, 0.125f);
    rlRotatef(90.0f, 1.0f, 0.0f, 0.0f);
    DrawCylinder(zero, 0.05f, 0.05f, 0.85f, 12, Color{60, 140, 255, 45});
    DrawCylinder(zero, 0.03f, 0.03f, 0.85f, 12, Color{90, 170, 255, 90});
    rlPopMatrix();
    EndShaderMode();
    EndBlendMode();
}

void drawWeapon(const Game &g) {
    if (g.scoped) {
        return;
    }
    const Basis b = gunBasis(g);
    const float m[16] = {
        b.side.x, b.side.y, b.side.z, 0.0f,
        b.up.x, b.up.y, b.up.z, 0.0f,
        b.fwd.x, b.fwd.y, b.fwd.z, 0.0f,
        b.origin.x, b.origin.y, b.origin.z, 1.0f};

    // Hold per weapon: the pistol is one-handed and tipped slightly down, the knife is one-handed with
    // the blade raised, and the lightsaber is raised and swings in an arc after each attack.
    const bool saber = saberHeld(g);
    float pitch = 0.0f;
    float yaw = 0.0f;
    if (!g.holding) {
        if (g.weapon == WeaponPistol) {
            pitch = 0.12f;
        } else if (g.weapon == WeaponKnife) {
            pitch = saber ? -0.35f : -0.6f;
        }
    }
    if (saber && g.saberSwing > 0.0f) {
        const float p = 1.0f - g.saberSwing / SaberSwingTime;
        yaw = std::sin(p * Pi) * 1.1f;
    }
    const bool oneHanded = !g.holding && (g.weapon == WeaponPistol || g.weapon == WeaponKnife);

    rlPushMatrix();
    rlMultMatrixf(m);
    rlPushMatrix();
    rlRotatef(yaw * RAD2DEG, 0.0f, 1.0f, 0.0f);
    rlRotatef(pitch * RAD2DEG, 1.0f, 0.0f, 0.0f);
    setGunGloss(true);
    gSkinsActive = true;
    if (g.holding) {
        drawThrowParts(g.throwSel);
    } else {
        drawWeaponParts(g.weapon, g.muzzleTimer > 0.0f);
    }
    if (saber && g.night) {
        drawSaberGlow();
    }
    gSkinsActive = false;
    setGunGloss(false);
    rlPopMatrix();

    // Blocky hands and sleeves: right hand on the grip, left hand on the foregrip for two-handed weapons.
    const Color skin{235, 200, 170, 255};
    const Color sleeve{45, 50, 60, 255};
    DrawCubeV(Vector3{0.12f, -0.2f, -0.28f}, Vector3{0.12f, 0.12f, 0.3f}, sleeve);
    DrawCubeV(Vector3{0.0f, -0.11f, -0.07f}, Vector3{0.09f, 0.11f, 0.1f}, skin);
    if (!oneHanded) {
        DrawCubeV(Vector3{-0.15f, -0.18f, -0.17f}, Vector3{0.12f, 0.12f, 0.4f}, sleeve);
        DrawCubeV(Vector3{0.0f, -0.06f, 0.16f}, Vector3{0.09f, 0.1f, 0.1f}, skin);
    }
    rlPopMatrix();
}

void drawQuad(Vector3 a, Vector3 b, Vector3 c, Vector3 d, Color col) {
    DrawTriangle3D(a, b, c, col);
    DrawTriangle3D(a, c, d, col);
}

struct View {
    Vector3 pos;
    Vector3 fwd;
};

bool inView(const View &v, Vector3 c, float r) {
    const Vector3 to = Vector3Subtract(c, v.pos);
    const float d = Vector3Length(to);
    if (d > drawDistance() + r) {
        return false;
    }
    if (d <= r) {
        return true;
    }
    const float cosA = std::clamp(Vector3DotProduct(to, v.fwd) / d, -1.0f, 1.0f);
    return std::acos(cosA) - std::asin(r / d) < ViewCone;
}

bool visibleTo(const View *v, Vector3 c, float r) {
    return v == nullptr || inView(*v, c, r);
}

Vector3 propCenter(const Prop &p) {
    return Vector3{p.base.x, p.base.y + p.h / 2, p.base.z};
}

float propRadius(const Prop &p) {
    return 0.5f * std::sqrt(p.w * p.w + p.h * p.h + p.d * p.d);
}

Texture2D gStatic{};
std::vector<Color> gStaticPix;

void updateStatic() {
    if (gStatic.id == 0) {
        const Image img = GenImageColor(StaticW, StaticH, BLACK);
        gStatic = LoadTextureFromImage(img);
        UnloadImage(img);
        SetTextureFilter(gStatic, TEXTURE_FILTER_POINT);
        gStaticPix.assign(static_cast<size_t>(StaticW * StaticH), BLACK);
    }
    for (Color &px : gStaticPix) {
        const unsigned char v = static_cast<unsigned char>(GetRandomValue(0, 255));
        px = Color{v, v, v, 255};
    }
    UpdateTexture(gStatic, gStaticPix.data());
}

void setEmissive(bool on);

void drawTvScreen(const Prop &p) {
    if (gStatic.id == 0) {
        return;
    }
    const Vector3 c = propCenter(p);
    const float hw = p.w * 0.4f, hh = p.h * 0.375f;
    setEmissive(true);
    rlSetTexture(gStatic.id);
    rlBegin(RL_QUADS);
    rlColor4ub(255, 255, 255, 255);
    for (float side : {-1.0f, 1.0f}) {
        const float z = c.z + side * (p.d / 2 + 0.02f);
        rlTexCoord2f(0.0f, 1.0f);
        rlVertex3f(c.x - hw, c.y - hh, z);
        rlTexCoord2f(1.0f, 1.0f);
        rlVertex3f(c.x + hw, c.y - hh, z);
        rlTexCoord2f(1.0f, 0.0f);
        rlVertex3f(c.x + hw, c.y + hh, z);
        rlTexCoord2f(0.0f, 0.0f);
        rlVertex3f(c.x - hw, c.y + hh, z);
    }
    rlEnd();
    rlSetTexture(0);
    setEmissive(false);
}

// Crack lines radiating from the impact point, one more web per hit.
void drawTvCracks(const Prop &p) {
    if (p.damage <= 0 || p.broken) {
        return;
    }
    const Vector3 c = propCenter(p);
    const float hw = p.w * 0.4f, hh = p.h * 0.375f;
    const float ix = c.x + hw * 0.2f, iy = c.y + hh * 0.15f;
    const Color crack{15, 15, 15, 255};
    for (float side : {-1.0f, 1.0f}) {
        const float z = c.z + side * (p.d / 2 + 0.04f);
        for (int k = 0; k < p.damage * 6; ++k) {
            const float ang = k * 0.9f + (side > 0.0f ? 0.3f : 0.0f);
            const float len = hw * (0.25f + 0.15f * static_cast<float>(k % 4));
            const float ex = std::clamp(ix + std::cos(ang) * len, c.x - hw, c.x + hw);
            const float ey = std::clamp(iy + std::sin(ang) * len, c.y - hh, c.y + hh);
            DrawLine3D(Vector3{ix, iy, z}, Vector3{ex, ey, z}, crack);
        }
    }
}

void drawBox(Vector3 c, Vector3 s, Color col, Color wire) {
    DrawCubeV(c, s, col);
    DrawCubeWiresV(c, s, wire);
}

void drawProp(const Prop &p) {
    const Vector3 o = p.base;
    const Vector3 c{o.x, o.y + p.h / 2, o.z};
    const Vector3 s{p.w, p.h, p.d};
    const Color wire{30, 30, 30, 90};
    switch (p.kind) {
    case PropKind::Crate:
    case PropKind::Wall:
    case PropKind::Net:
    case PropKind::Pack:
    case PropKind::Shelf:
        drawBox(c, s, p.color, wire);
        break;
    case PropKind::Flat:
        DrawCubeV(c, s, p.color);
        break;
    case PropKind::House: {
        const float hw = p.w / 2, hd = p.d / 2, top = o.y + p.h, rh = p.d * 0.4f;
        const float x0 = o.x - hw, x1 = o.x + hw;
        const float zA = o.z - hd, zB = o.z + hd, zC = o.z;
        const Vector3 ridgeA{x0, top + rh, zC}, ridgeB{x1, top + rh, zC};
        drawQuad(Vector3{x0, top, zA}, Vector3{x1, top, zA}, ridgeB, ridgeA, p.accent);
        drawQuad(Vector3{x0, top, zB}, Vector3{x1, top, zB}, ridgeB, ridgeA, p.accent);
        DrawTriangle3D(Vector3{x0, top, zA}, Vector3{x0, top, zB}, ridgeA, p.accent);
        DrawTriangle3D(Vector3{x1, top, zA}, Vector3{x1, top, zB}, ridgeB, p.accent);
        break;
    }
    case PropKind::Barn: {
        DrawCubeV(c, s, p.color);
        const float hw = p.w / 2, hd = p.d / 2, top = o.y + p.h, rh = p.d * 0.4f;
        const float x0 = o.x - hw, x1 = o.x + hw;
        const float zA = o.z - hd, zB = o.z + hd, zC = o.z;
        const Vector3 ridgeA{x0, top + rh, zC}, ridgeB{x1, top + rh, zC};
        drawQuad(Vector3{x0, top, zA}, Vector3{x1, top, zA}, ridgeB, ridgeA, p.accent);
        drawQuad(Vector3{x0, top, zB}, Vector3{x1, top, zB}, ridgeB, ridgeA, p.accent);
        DrawTriangle3D(Vector3{x0, top, zA}, Vector3{x0, top, zB}, ridgeA, p.accent);
        DrawTriangle3D(Vector3{x1, top, zA}, Vector3{x1, top, zB}, ridgeB, p.accent);
        break;
    }
    case PropKind::Silo: {
        const Vector3 top{o.x, o.y + p.h, o.z};
        DrawCylinderEx(o, top, p.w / 2, p.w / 2, 16, p.color);
        DrawSphere(top, p.w / 2, p.accent);
        break;
    }
    case PropKind::Tree: {
        DrawCylinderEx(o, Vector3{o.x, o.y + p.h * 0.4f, o.z}, 0.25f, 0.2f, 8, p.accent);
        DrawSphere(Vector3{o.x, o.y + p.h * 0.65f, o.z}, p.w, p.color);
        break;
    }
    case PropKind::Couch:
        drawBox(c, s, p.color, wire);
        DrawCubeV(Vector3{c.x, o.y + p.h * 0.85f, c.z}, Vector3{p.w * 0.95f, p.h * 0.3f, p.d * 0.8f}, p.accent);
        break;
    case PropKind::Tv:
        drawBox(c, s, p.color, wire);
        DrawCubeV(Vector3{c.x, c.y, c.z}, Vector3{p.w * 0.8f, p.h * 0.75f, p.d + 0.02f},
                  p.broken ? Color{8, 8, 8, 255} : p.accent);
        if (!p.broken) {
            drawTvScreen(p);
        }
        drawTvCracks(p);
        break;
    case PropKind::Table:
        DrawCubeV(Vector3{c.x, o.y + p.h - 0.05f, c.z}, Vector3{p.w, 0.1f, p.d}, p.color);
        DrawCubeV(Vector3{c.x, o.y + (p.h - 0.1f) / 2, c.z}, Vector3{p.w * 0.1f, p.h - 0.1f, p.d * 0.1f}, p.accent);
        break;
    case PropKind::Bed:
        drawBox(c, s, p.color, wire);
        DrawCubeV(Vector3{c.x, o.y + p.h + 0.05f, c.z}, Vector3{p.w * 0.9f, 0.15f, p.d * 0.9f}, p.accent);
        break;
    case PropKind::Fridge:
        drawBox(c, s, p.color, wire);
        DrawCubeV(Vector3{c.x, c.y, c.z}, Vector3{p.w + 0.02f, 0.05f, p.d + 0.02f}, p.accent);
        break;
    case PropKind::Plant:
        DrawCylinderEx(o, Vector3{o.x, o.y + 0.5f, o.z}, p.w / 2, p.w / 2 * 0.8f, 10, p.color);
        DrawSphere(Vector3{o.x, o.y + p.h * 0.75f, o.z}, p.w * 0.9f, p.accent);
        break;
    case PropKind::Window:
        DrawCubeV(c, s, p.color);
        break;
    case PropKind::Person:
        DrawCylinderEx(o, Vector3{o.x, o.y + 1.2f, o.z}, 0.22f, 0.2f, 8, p.color);
        DrawSphere(Vector3{o.x, o.y + 1.5f, o.z}, 0.2f, p.accent);
        break;
    }
}

void drawRectLines(float y, float x0, float x1, float z0, float z1, Color col) {
    const Vector3 a{x0, y, z0}, b{x1, y, z0}, c{x1, y, z1}, d{x0, y, z1};
    DrawLine3D(a, b, col);
    DrawLine3D(b, c, col);
    DrawLine3D(c, d, col);
    DrawLine3D(d, a, col);
}

void drawPitch(const Pitch &pt) {
    const Color white{245, 245, 245, 255};
    const float y = pt.center.y + 0.03f;
    const float hw = pt.w / 2, hd = pt.d / 2;
    drawRectLines(y, pt.center.x - hw, pt.center.x + hw, pt.center.z - hd, pt.center.z + hd, white);
    DrawLine3D(Vector3{pt.center.x, y, pt.center.z - hd}, Vector3{pt.center.x, y, pt.center.z + hd}, white);
    DrawCircle3D(Vector3{pt.center.x, y, pt.center.z}, 3.0f, Vector3{1.0f, 0.0f, 0.0f}, 90.0f, white);
    for (float sign : {-1.0f, 1.0f}) {
        const float edge = pt.center.x + sign * hw;
        const float inner = edge - sign * 5.5f;
        drawRectLines(y, std::min(edge, inner), std::max(edge, inner), pt.center.z - 7.0f, pt.center.z + 7.0f, white);
    }
}

Color dimmed(Color c, float k) {
    return Color{static_cast<unsigned char>(c.r * k), static_cast<unsigned char>(c.g * k),
                 static_cast<unsigned char>(c.b * k), c.a};
}

Color skyOf(const Game &g) {
    return g.night ? Color{12, 14, 30, 255} : g.level.sky;
}

void drawLamps(const Level &L, bool night, const View *view = nullptr) {
    const Color pole{60, 60, 65, 255};
    for (const Vector3 &p : L.lamps) {
        if (!visibleTo(view, Vector3{p.x, p.y + LampHeight / 2, p.z}, LampHeight / 2 + 1.3f)) {
            continue;
        }
        const Vector3 top{p.x, p.y + LampHeight, p.z};
        DrawCylinderEx(p, top, 0.08f, 0.08f, 6, pole);
        if (!night) {
            DrawSphereEx(top, 0.25f, 6, 6, Color{200, 200, 190, 255});
            continue;
        }
        // The lit lamp head is emissive so the shader does not darken it.
        setEmissive(true);
        DrawSphereEx(top, 0.25f, 6, 6, Color{255, 235, 160, 255});
        setEmissive(false);
        DrawSphere(top, 1.2f, Color{255, 220, 120, 40});
        DrawCircle3D(Vector3{p.x, p.y + 0.05f, p.z}, 4.0f, Vector3{1.0f, 0.0f, 0.0f}, 90.0f,
                     Color{255, 220, 120, 35});
    }
}

void drawBirds(const Game &g) {
    const Color ink{30, 30, 35, 255};
    for (const Bird &b : g.birds) {
        const Vector3 fwd = Vector3Normalize(b.vel);
        const Vector3 side = Vector3Normalize(Vector3CrossProduct(fwd, Vector3{0.0f, 1.0f, 0.0f}));
        const float flap = std::sin(b.phase + b.age * 9.0f) * 0.25f;
        const Vector3 lift{0.0f, flap, 0.0f};
        const Vector3 wl = Vector3Add(Vector3Add(b.pos, Vector3Scale(side, 0.5f)), lift);
        const Vector3 wr = Vector3Add(Vector3Subtract(b.pos, Vector3Scale(side, 0.5f)), lift);
        DrawLine3D(wl, b.pos, ink);
        DrawLine3D(wr, b.pos, ink);
        DrawLine3D(Vector3Subtract(b.pos, Vector3Scale(fwd, 0.3f)), Vector3Add(b.pos, Vector3Scale(fwd, 0.3f)), ink);
    }
}

void drawGroundShadow(float x, float z, float y, float w, float d) {
    DrawPlane(Vector3{x, y + ShadowLift, z}, Vector2{w, d}, ShadowColor);
}

void drawShadowDisc(Vector3 p, float r) {
    const Vector3 lo{p.x, p.y + ShadowLift, p.z};
    DrawCylinderEx(lo, Vector3{p.x, lo.y + 0.01f, p.z}, r, r, 16, ShadowColor);
}

void drawPropShadows(const Level &L, const View *view) {
    for (const Prop &p : L.props) {
        if (p.kind == PropKind::Flat || p.kind == PropKind::Window ||
            !visibleTo(view, propCenter(p), propRadius(p))) {
            continue;
        }
        if (p.kind == PropKind::Tree) {
            drawShadowDisc(p.base, p.w * 0.9f);
        } else if (p.kind == PropKind::Silo) {
            drawShadowDisc(p.base, p.w * 0.55f);
        } else {
            drawGroundShadow(p.base.x, p.base.z, p.base.y, p.w * 1.1f, p.d * 1.1f);
        }
    }
}

void drawLevel(const Level &L, float dim, const View *view = nullptr) {
    rlDisableBackfaceCulling();
    const unsigned char v = static_cast<unsigned char>(255.0f * dim);
    DrawModel(L.model, Vector3{0.0f, 0.0f, 0.0f}, 1.0f, Color{v, v, v, 255});
    if (gSettings.shadows && gSettings.potato) {
        drawPropShadows(L, view);
    }
    for (const Prop &p : L.props) {
        if (!visibleTo(view, propCenter(p), propRadius(p))) {
            continue;
        }
        Prop q = p;
        q.color = dimmed(p.color, dim);
        q.accent = dimmed(p.accent, dim);
        drawProp(q);
    }
    for (const Pitch &pt : L.pitches) {
        drawPitch(pt);
    }
}

void drawCorpse(const Corpse &c) {
    const Color body{200, 60, 60, 255};
    for (const BodyLink &l : BodyLinks) {
        const float r = (l.a == PChest && l.b == PHip) ? 0.3f : 0.14f;
        DrawCapsule(c.p[l.a], c.p[l.b], r, 6, 6, body);
    }
    DrawSphere(c.p[PHead], HeadRadius, Color{235, 200, 170, 255});
}

void drawMusicInfo(const Playlist &pl) {
    if (!pl.loaded || pl.current < 0) {
        return;
    }
    const int H = GetScreenHeight();
    DrawTextEx(gMusicFont,
               TextFormat("Now playing: %s   Volume %d%%%s", GetFileNameWithoutExt(pl.tracks[pl.current].c_str()),
                          static_cast<int>(pl.volume * 100.0f + 0.5f), pl.muted ? "   [MUTED]" : ""),
               Vector2{20.0f, static_cast<float>(H - 74)}, MusicFontSize, 0.0f, RAYWHITE);
    const float len = GetMusicTimeLength(pl.music);
    const float played = GetMusicTimePlayed(pl.music);
    const float frac = len > 0.0f ? std::clamp(played / len, 0.0f, 1.0f) : 0.0f;
    DrawRectangle(20, H - 88, 400, 8, Color{60, 60, 70, 255});
    DrawRectangle(20, H - 88, static_cast<int>(400.0f * frac), 8, Color{200, 70, 50, 255});
}

// Point on a fighter: forward along its facing, side and height from its feet.
Vector3 offsetOf(const Enemy &e, float fwd, float side, float up) {
    const Vector3 base = enemyPoint(e, side, up);
    return Vector3Add(base, Vector3Scale(e.dir, fwd));
}

// Hair, hat, jewelry and shirt logo for a fighter. The shirt keeps the team color.
void drawCosmetics(const Enemy &e) {
    const Look &l = e.look;
    const Color hair = HairColors[l.hairColor];
    const Color hat = HatColors[l.hatColor];
    if (l.hair == HairShort) {
        DrawSphere(offsetOf(e, -0.06f, 0.0f, HeadY + 0.08f), 0.25f, hair);
    } else if (l.hair == HairLong) {
        DrawSphere(offsetOf(e, -0.06f, 0.0f, HeadY + 0.08f), 0.25f, hair);
        DrawCapsule(offsetOf(e, -0.16f, 0.0f, HeadY - 0.02f), offsetOf(e, -0.16f, 0.0f, 1.2f), 0.16f, 6, 6, hair);
    } else if (l.hair == HairMohawk) {
        DrawCapsule(offsetOf(e, 0.0f, 0.0f, HeadY + 0.2f), offsetOf(e, -0.12f, 0.0f, HeadY + 0.42f), 0.07f, 4, 4, hair);
    }
    if (l.hat == HatCap) {
        DrawCylinder(offsetOf(e, 0.0f, 0.0f, HeadY + 0.25f), 0.27f, 0.27f, 0.1f, 10, hat);
        DrawCylinder(offsetOf(e, 0.2f, 0.0f, HeadY + 0.22f), 0.12f, 0.12f, 0.02f, 8, hat);
    } else if (l.hat == HatBeanie) {
        DrawSphere(offsetOf(e, -0.02f, 0.0f, HeadY + 0.12f), 0.27f, hat);
    } else if (l.hat == HatTop) {
        DrawCylinder(offsetOf(e, 0.0f, 0.0f, HeadY + 0.3f), 0.2f, 0.2f, 0.4f, 10, hat);
        DrawCylinder(offsetOf(e, 0.0f, 0.0f, HeadY + 0.24f), 0.36f, 0.36f, 0.04f, 12, hat);
    }
    if (l.jewel == JewelChain || l.jewel == JewelBoth) {
        DrawCapsule(offsetOf(e, 0.25f, -0.25f, 1.12f), offsetOf(e, 0.25f, 0.25f, 1.12f), 0.03f, 4, 4, GOLD);
    }
    if (l.jewel == JewelEarring || l.jewel == JewelBoth) {
        DrawSphere(offsetOf(e, 0.0f, 0.3f, HeadY - 0.02f), 0.04f, GOLD);
        DrawSphere(offsetOf(e, 0.0f, -0.3f, HeadY - 0.02f), 0.04f, GOLD);
    }
    const Vector3 chest = offsetOf(e, 0.38f, 0.0f, 1.0f);
    if (l.logo == LogoStripes) {
        for (int k = -1; k <= 1; ++k) {
            DrawCube(offsetOf(e, 0.38f, static_cast<float>(k) * 0.12f, 1.0f), 0.02f, 0.2f, 0.04f, WHITE);
        }
    } else if (l.logo == LogoRing) {
        for (int k = 0; k < 5; ++k) {
            const float a = static_cast<float>(k) * 2.0f * Pi / 5.0f;
            DrawSphere(offsetOf(e, 0.38f, std::cos(a) * 0.1f, 1.0f + std::sin(a) * 0.1f), 0.03f, WHITE);
        }
    } else if (l.logo == LogoCross) {
        DrawCube(chest, 0.02f, 0.22f, 0.04f, WHITE);
        DrawCube(chest, 0.02f, 0.04f, 0.22f, WHITE);
    } else if (l.logo == LogoBadge) {
        DrawSphere(chest, 0.09f, GOLD);
    }
}

void drawEnemyBars(const Game &g) {
    const float w = 56.0f, h = 7.0f;
    for (const Enemy &e : g.enemies) {
        const Vector3 top{e.pos.x, e.pos.y + HeadY + HeadRadius + 0.35f, e.pos.z};
        const Vector3 toE = Vector3Subtract(top, g.cam.position);
        if (Vector3Length(toE) > 60.0f || Vector3DotProduct(toE, forwardOf(g)) <= 0.0f) {
            continue;
        }
        const Vector2 s = GetWorldToScreen(top, g.cam);
        const float x = s.x - w / 2.0f;
        DrawRectangle(static_cast<int>(x), static_cast<int>(s.y), static_cast<int>(w), static_cast<int>(h),
                      Color{30, 30, 30, 200});
        const float frac = e.netId != 0 ? e.netHealth : static_cast<float>(e.hp) / EnemyHp;
        DrawRectangle(static_cast<int>(x), static_cast<int>(s.y), static_cast<int>(w * frac),
                      static_cast<int>(h), frac > 0.5f ? GREEN : RED);
    }
}

void drawCar(const Car &c, const Terrain &t, float dim) {
    const Vector3 p = carCenter(c);
    const float y = groundAt(t, p.x, p.z);
    const Vector3 body = c.alongX ? Vector3{CarLength, 0.9f, CarWidth} : Vector3{CarWidth, 0.9f, CarLength};
    const Vector3 cabin = c.alongX ? Vector3{2.4f, 0.7f, CarWidth - 0.3f} : Vector3{CarWidth - 0.3f, 0.7f, 2.4f};
    DrawCubeV(Vector3{p.x, y + 0.6f, p.z}, body, dimmed(c.color, dim));
    DrawCubeV(Vector3{p.x, y + 1.25f, p.z}, cabin, dimmed(Color{40, 50, 60, 255}, dim));
    if (c.police) {
        const bool red = std::fmod(GetTime(), 0.5) < 0.25;
        DrawCubeV(Vector3{p.x, y + 1.7f, p.z}, Vector3{0.8f, 0.2f, 0.4f},
                  red ? Color{255, 40, 40, 255} : Color{40, 80, 255, 255});
    }
}

void drawSignal(const Vector3 &p, bool greenX) {
    DrawCylinderEx(p, Vector3{p.x, p.y + 4.0f, p.z}, 0.1f, 0.1f, 6, Color{50, 50, 55, 255});
    DrawCubeV(Vector3{p.x, p.y + 4.4f, p.z}, Vector3{0.5f, 1.2f, 0.5f}, Color{30, 30, 30, 255});
    static const float lampY[3] = {4.8f, 4.4f, 4.0f};
    static const Color onCol[3] = {Color{255, 40, 40, 255}, Color{255, 200, 40, 255}, Color{60, 255, 90, 255}};
    static const Color offCol[3] = {Color{70, 25, 25, 255}, Color{70, 60, 20, 255}, Color{25, 70, 35, 255}};
    for (int face = 0; face < 4; ++face) {
        const bool alongX = face < 2;
        const float side = face % 2 == 0 ? 1.0f : -1.0f;
        const bool green = alongX ? greenX : !greenX;
        for (int i = 0; i < 3; ++i) {
            const bool lit = (i == 0 && !green) || (i == 2 && green);
            const Vector3 lamp = alongX ? Vector3{p.x + side * 0.26f, lampY[i], p.z}
                                        : Vector3{p.x, lampY[i], p.z + side * 0.26f};
            DrawSphereEx(lamp, 0.11f, 5, 5, lit ? onCol[i] : offCol[i]);
        }
    }
}

void drawDoors(const Level &L, const View *view, float dim) {
    const Color wood{125, 85, 55, 255};
    const Color knob{220, 190, 80, 255};
    for (const Door &d : L.doors) {
        if (!visibleTo(view, d.center, DoorWidth + DoorHeight)) {
            continue;
        }
        rlPushMatrix();
        rlTranslatef(d.hinge.x, d.hinge.y, d.hinge.z);
        rlRotatef(-d.angle, 0.0f, 1.0f, 0.0f);
        DrawCubeV(Vector3{DoorWidth / 2, DoorHeight / 2, 0.0f}, Vector3{DoorWidth, DoorHeight, DoorThick},
                  dimmed(wood, dim));
        DrawCubeV(Vector3{DoorWidth - 0.2f, 1.0f, DoorThick}, Vector3{0.1f, 0.1f, 0.05f}, dimmed(knob, dim));
        rlPopMatrix();
    }
}

void drawEntityShadows(const Game &g, const View &view) {
    for (const Car &c : g.cars) {
        if (!inView(view, carCenter(c), CarLength)) {
            continue;
        }
        const Vector3 p = carCenter(c);
        const float sx = c.alongX ? CarLength : CarWidth;
        const float sz = c.alongX ? CarWidth : CarLength;
        drawGroundShadow(p.x, p.z, groundAt(g.level.terrain, p.x, p.z), sx * 1.1f, sz * 1.1f);
    }
    for (const Ball &b : g.balls) {
        if (inView(view, b.pos, BallRadius)) {
            drawShadowDisc(Vector3{b.pos.x, groundAt(g.level.terrain, b.pos.x, b.pos.z), b.pos.z}, BallRadius * 0.9f);
        }
    }
    for (const Enemy &e : g.enemies) {
        if (inView(view, e.pos, 1.0f)) {
            drawShadowDisc(e.pos, 0.45f);
        }
    }
}

unsigned char alphaOf(float f) {
    return static_cast<unsigned char>(std::clamp(f, 0.0f, 1.0f) * 255.0f);
}

void drawPickups(const Game &g, const View &view, float dim) {
    for (const AmmoBox &b : g.ammoBoxes) {
        if (!inView(view, b.pos, 1.0f)) {
            continue;
        }
        DrawCubeV(Vector3{b.pos.x, b.pos.y + 0.25f, b.pos.z}, Vector3{0.6f, 0.5f, 0.6f},
                  dimmed(Color{90, 110, 60, 255}, dim));
        DrawCubeV(Vector3{b.pos.x, b.pos.y + 0.52f, b.pos.z}, Vector3{0.3f, 0.04f, 0.3f}, dimmed(YELLOW, dim));
    }
    for (const DroppedWeapon &d : g.drops) {
        if (!inView(view, d.pos, 1.0f)) {
            continue;
        }
        rlPushMatrix();
        rlTranslatef(d.pos.x, d.pos.y + 0.08f, d.pos.z);
        rlRotatef(90.0f, 0.0f, 0.0f, 1.0f);
        setGunGloss(true);
        drawWeaponParts(d.weapon, false);
        setGunGloss(false);
        rlPopMatrix();
    }
    for (const Chest &c : g.chests) {
        if (!inView(view, c.pos, 1.0f)) {
            continue;
        }
        const Color wood = c.opened ? Color{70, 50, 35, 255} : Color{120, 80, 45, 255};
        DrawCubeV(Vector3{c.pos.x, c.pos.y + 0.3f, c.pos.z}, Vector3{0.9f, 0.6f, 0.55f}, dimmed(wood, dim));
        DrawCubeV(Vector3{c.pos.x, c.pos.y + 0.66f, c.pos.z}, Vector3{0.9f, 0.1f, 0.55f},
                  dimmed(Color{200, 170, 70, 255}, dim));
    }
    for (const Decoy &d : g.decoys) {
        DrawSphere(Vector3{d.pos.x, d.pos.y + 0.5f, d.pos.z}, 0.3f, Color{255, 140, 0, 255});
    }
    for (const Blast &b : g.blasts) {
        const float t = b.age / BlastTime;
        if (b.bright) {
            DrawSphere(b.pos, FlashRadius * 0.5f * t, Color{255, 255, 250, alphaOf(1.0f - t)});
        } else {
            DrawSphere(b.pos, GrenadeRadius * t, Color{255, 150, 40, static_cast<unsigned char>(200.0f * (1.0f - t))});
        }
    }
    for (const FoeTracer &t : g.foeTracers) {
        DrawLine3D(t.from, t.to, Color{255, 120, 80, 255});
    }
}

// ---- Graphics: lit shader, sun/moon lighting, bloom post pass ----
const char *LitVs = R"(#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec4 vertexColor;
uniform mat4 mvp;
uniform mat4 matModel;
out vec2 fragTexCoord;
out vec4 fragColor;
out vec3 fragWorld;
out vec3 fragNormal;
void main() {
    vec4 wp = matModel * vec4(vertexPosition, 1.0);
    fragTexCoord = vertexTexCoord;
    fragColor = vertexColor;
    fragWorld = wp.xyz;
    fragNormal = mat3(matModel) * vertexNormal;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)";

const char *LitFs = R"(#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
in vec3 fragWorld;
in vec3 fragNormal;
uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform vec3 viewPos;
uniform vec3 uLightDir;
uniform vec3 uLightColor;
uniform vec3 uSkyColor;
uniform vec3 uGroundColor;
uniform float uExposure;
uniform float uGloss;
uniform vec3 uLampPos[16];
uniform int uLampCount;
uniform float uNight;
uniform float uFlash;
uniform vec3 uFlashPos;
uniform vec3 uFlashDir;
uniform vec3 uTvPos[4];
uniform int uTvCount;
uniform float uEmissive;
uniform vec3 uMuzzlePos[4];
uniform float uMuzzleAmt[4];
uniform vec3 uGrenPos[4];
uniform vec3 uGrenCol[4];
uniform int uGrenCount;
out vec4 finalColor;

void main() {
    vec4 tex = texture(texture0, fragTexCoord);
    vec3 base = tex.rgb * fragColor.rgb * colDiffuse.rgb;
    vec3 N = normalize(fragNormal);
    vec3 V = normalize(viewPos - fragWorld);
    float ndl = max(dot(N, uLightDir), 0.0);

    // Hemisphere ambient: sky above, ground bounce below.
    vec3 ambient = mix(uGroundColor, uSkyColor, N.y * 0.5 + 0.5);
    float ao = mix(0.75, 1.0, smoothstep(0.0, 1.2, fragWorld.y));

    float sat = max(base.r, max(base.g, base.b)) - min(base.r, min(base.g, base.b));
    float metal = 1.0 - smoothstep(0.02, 0.16, sat);
    float wood = clamp((base.r - base.b) * 4.0, 0.0, 1.0) * uGloss;
    metal = max(metal, uGloss * (1.0 - wood));

    vec3 H = normalize(uLightDir + V);
    float spec = pow(max(dot(N, H), 0.0), 48.0) * (0.15 + 0.85 * metal);
    spec += pow(max(dot(N, H), 0.0), 90.0) * wood * 0.6;

    vec3 R = reflect(-V, N);
    vec3 env = mix(uGroundColor, uSkyColor * 1.3, clamp(R.y * 0.5 + 0.5, 0.0, 1.0));
    float fres = pow(1.0 - max(dot(N, V), 0.0), 4.0);
    vec3 refl = env * mix(0.08, 0.7, fres) * metal * 0.5;

    vec3 lampLight = vec3(0.0);
    for (int i = 0; i < uLampCount; ++i) {
        vec3 toL = uLampPos[i] - fragWorld;
        float dist = length(toL);
        float att = clamp(1.0 - dist / 16.0, 0.0, 1.0);
        lampLight += vec3(1.0, 0.85, 0.55) * att * att * max(dot(N, toL / max(dist, 0.001)), 0.0);
    }
    lampLight *= uNight * 3.0;

    vec3 muzzleLight = vec3(0.0);
    for (int i = 0; i < 4; ++i) {
        vec3 toM = uMuzzlePos[i] - fragWorld;
        float md = length(toM);
        float matt = clamp(1.0 - md / 12.0, 0.0, 1.0);
        muzzleLight += vec3(1.0, 0.45, 0.12) * uMuzzleAmt[i] * matt * matt * 4.0 * max(dot(N, toM / max(md, 0.001)), 0.2);
    }

    vec3 torchLight = vec3(0.0);
    if (uFlash > 0.5) {
        vec3 toF = uFlashPos - fragWorld;
        float fd = length(toF);
        vec3 Lf = toF / max(fd, 0.001);
        float cone = smoothstep(0.85, 0.97, dot(-Lf, uFlashDir));
        float fatt = clamp(1.0 - fd / 30.0, 0.0, 1.0);
        torchLight += vec3(1.0, 0.92, 0.75) * cone * fatt * fatt * 3.0 * max(dot(N, Lf), 0.2);
    }
    for (int i = 0; i < uTvCount; ++i) {
        vec3 toT = uTvPos[i] - fragWorld;
        float td = length(toT);
        float tatt = clamp(1.0 - td / 7.0, 0.0, 1.0);
        torchLight += vec3(0.5, 0.7, 1.0) * tatt * tatt * (0.6 + 0.4 * max(dot(N, toT / max(td, 0.001)), 0.0));
    }

    vec3 grenLight = vec3(0.0);
    for (int i = 0; i < uGrenCount; ++i) {
        vec3 toG = uGrenPos[i] - fragWorld;
        float gd = length(toG);
        float gatt = clamp(1.0 - gd / 8.0, 0.0, 1.0);
        grenLight += uGrenCol[i] * gatt * gatt * 1.5 * max(dot(N, toG / max(gd, 0.001)), 0.2);
    }

    vec3 color = base * (ambient * ao + uLightColor * ndl + lampLight + torchLight + muzzleLight + grenLight) + uLightColor * spec + refl * mix(vec3(1.0), base, 0.5);
    color *= uExposure;
    if (uEmissive > 0.5) {
        color = base * 1.25 * uExposure;
    }
    finalColor = vec4(color, tex.a * fragColor.a * colDiffuse.a);
}
)";

const char *SmokeFs = R"(#version 330
in vec4 fragColor;
in vec3 fragWorld;
in vec3 fragNormal;
uniform vec3 viewPos;
uniform vec3 uLightDir;
uniform vec3 uLightColor;
uniform vec3 uSkyColor;
uniform float uExposure;
uniform float uNight;
uniform vec3 uLampPos[16];
uniform int uLampCount;
uniform float uFlash;
uniform vec3 uFlashPos;
uniform vec3 uFlashDir;
out vec4 finalColor;
void main() {
    vec3 N = normalize(fragNormal);
    vec3 V = normalize(viewPos - fragWorld);
    // Light passing through the cloud scatters most toward a viewer looking at its source.
    float wrap = clamp(dot(N, uLightDir) * 0.5 + 0.5, 0.0, 1.0);
    float sunPhase = pow(max(dot(V, -uLightDir), 0.0), 3.0);
    vec3 light = uSkyColor * 0.6 + uLightColor * (0.5 + 0.5 * wrap + 1.2 * sunPhase) * 0.7;
    for (int i = 0; i < uLampCount; ++i) {
        float d = length(fragWorld - uLampPos[i]);
        vec3 travel = (fragWorld - uLampPos[i]) / max(d, 0.001);
        float att = clamp(1.0 - d / 16.0, 0.0, 1.0);
        light += vec3(1.0, 0.85, 0.55) * att * att * uNight * (0.2 + 1.5 * pow(max(dot(V, travel), 0.0), 3.0));
    }
    if (uFlash > 0.5) {
        float d = length(fragWorld - uFlashPos);
        vec3 travel = (fragWorld - uFlashPos) / max(d, 0.001);
        float cone = smoothstep(0.85, 0.97, dot(travel, uFlashDir));
        float att = clamp(1.0 - d / 30.0, 0.0, 1.0);
        light += vec3(1.0, 0.92, 0.75) * cone * att * att * 2.0 * (0.2 + 1.5 * pow(max(dot(V, travel), 0.0), 3.0));
    }
    finalColor = vec4(fragColor.rgb * light * uExposure, fragColor.a);
}
)";

// Contrast and saturation grading adapted from raylib's color_correction.fs (zlib license).
const char *PostFs = R"(#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
uniform sampler2D texture0;
uniform vec2 uTexel;
uniform float uContrast;
uniform float uSaturation;
out vec4 finalColor;
void main() {
    vec3 c = texture(texture0, fragTexCoord).rgb;
    vec3 glow = vec3(0.0);
    for (int x = -3; x <= 3; ++x) {
        for (int y = -3; y <= 3; ++y) {
            vec3 s = texture(texture0, fragTexCoord + vec2(x, y) * uTexel * 3.0).rgb;
            glow += max(s - 0.75, 0.0);
        }
    }
    c += glow / 49.0 * 1.4;
    c = (c - 0.5) * uContrast + 0.5;
    float lum = dot(c, vec3(0.299, 0.587, 0.114));
    c = mix(vec3(lum), c, uSaturation);
    // Soft shoulder: values below 0.8 are untouched, brighter ones roll off instead of clipping.
    vec3 over = max(c - 0.8, 0.0);
    c = min(c, 0.8) + 0.2 * (1.0 - exp(-over / 0.2));
    finalColor = vec4(clamp(c, 0.0, 1.0), 1.0);
}
)";

const char *FireVs = R"(#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec4 vertexColor;
uniform mat4 mvp;
out vec3 vWorld;
void main() {
    vWorld = vertexPosition;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)";

const char *FireFs = R"(#version 330
in vec3 vWorld;
uniform vec3 uCenter;
uniform float uTime;
uniform vec3 uDir;
uniform float uLife;
out vec4 finalColor;
float hash(vec3 p) {
    return fract(sin(dot(p, vec3(127.1, 311.7, 74.7))) * 43758.5453);
}
float noise(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = mix(mix(hash(i), hash(i + vec3(1, 0, 0)), f.x), mix(hash(i + vec3(0, 1, 0)), hash(i + vec3(1, 1, 0)), f.x), f.y);
    float b = mix(mix(hash(i + vec3(0, 0, 1)), hash(i + vec3(1, 0, 1)), f.x), mix(hash(i + vec3(0, 1, 1)), hash(i + vec3(1, 1, 1)), f.x), f.y);
    return mix(a, b, f.z);
}
void main() {
    vec3 p = vWorld - uCenter;
    float along = dot(p, uDir);
    float across = length(p - uDir * along);
    float t = clamp(along / 0.55, 0.0, 1.0);
    float radius = mix(0.13, 0.015, t);
    float n = noise(vec3(across * 20.0, along * 12.0 - uTime * 25.0, uTime * 8.0));
    float flame = (1.0 - across / radius) + (n - 0.5) * 0.8;
    flame *= step(0.0, along) * (1.0 - t);
    flame = smoothstep(0.05, 0.5, flame);
    if (flame <= 0.0) {
        discard;
    }
    vec3 cool = vec3(0.8, 0.12, 0.02);
    vec3 mid = vec3(1.0, 0.4, 0.08);
    vec3 hot = vec3(1.0, 0.8, 0.4);
    vec3 col = mix(cool, mid, smoothstep(0.1, 0.5, flame));
    col = mix(col, hot, smoothstep(0.5, 0.95, flame));
    finalColor = vec4(col * flame * uLife * 2.0, 1.0);
}
)";

Shader gLit{};
Shader gShadow{};

// Flat translucent black for the projected shadows.
const char *ShadowFs = R"(#version 330
out vec4 finalColor;
void main() {
    finalColor = vec4(0.0, 0.0, 0.0, 0.45);
}
)";
Shader gPost{};
Shader gFire{};
Shader gSmoke{};

// Potato mode draws with raylib's default shader, so the lit shader is skipped.
void beginLit() {
    if (!gSettings.potato) {
        BeginShaderMode(gLit);
    }
}

Shader defaultShader() {
    Shader s{};
    s.id = rlGetShaderIdDefault();
    s.locs = rlGetShaderLocsDefault();
    return s;
}
RenderTexture2D gScene{};
Vector3 gSunDir{};
Vector3 gMoonDir{};

// Day sun and night moon directions; the moon sits lower and comes from the other side.
Vector3 activeLightDir(bool night) {
    return night ? gMoonDir : gSunDir;
}

void initGraphics() {
    gLit = LoadShaderFromMemory(LitVs, LitFs);
    gPost = LoadShaderFromMemory(nullptr, PostFs);
    gFire = LoadShaderFromMemory(FireVs, FireFs);
    gSmoke = LoadShaderFromMemory(LitVs, SmokeFs);
    gSaber = LoadShaderFromMemory(nullptr, SaberFs);
    gShadow = LoadShaderFromMemory(nullptr, ShadowFs);
    gSunDir = sunDirection();
    gMoonDir = Vector3Normalize(Vector3{-0.5f, 0.45f, -0.6f});
}

void shutdownGraphics() {
    UnloadShader(gLit);
    UnloadShader(gPost);
    UnloadShader(gFire);
    UnloadShader(gSmoke);
    UnloadShader(gSaber);
    UnloadShader(gShadow);
    if (gScene.texture.id != 0) {
        UnloadRenderTexture(gScene);
    }
}

void ensureScene() {
    const int w = GetScreenWidth();
    const int h = GetScreenHeight();
    if (gScene.texture.id == 0 || gScene.texture.width != w || gScene.texture.height != h) {
        if (gScene.texture.id != 0) {
            UnloadRenderTexture(gScene);
        }
        gScene = LoadRenderTexture(w, h);
    }
}

void setLevelShader(const Level &L, Shader s) {
    if (!L.modelLoaded) {
        return;
    }
    for (int i = 0; i < L.model.materialCount; ++i) {
        L.model.materials[i].shader = s;
    }
}

// Grenade body and glow colour, shared by the models and their night light.
Color grenadeColor(int kind) {
    if (kind == ThrowFlash) {
        return WHITE;
    }
    if (kind == ThrowSmoke) {
        return LIGHTGRAY;
    }
    if (kind == ThrowDecoy) {
        return ORANGE;
    }
    return Color{40, 60, 40, 255};
}

// Night point lights (lamps, bulbs), nearest first, capped at MaxLamps.
int collectLamps(const Game &g, Vector3 (&out)[MaxLamps]) {
    if (!g.night) {
        return 0;
    }
    std::vector<Vector3> lamps;
    for (const Vector3 &p : g.level.lamps) {
        lamps.push_back(Vector3{p.x, p.y + LampHeight, p.z});
    }
    lamps.insert(lamps.end(), g.level.bulbs.begin(), g.level.bulbs.end());
    std::sort(lamps.begin(), lamps.end(), [&](const Vector3 &a, const Vector3 &b) {
        return Vector3Distance(a, g.cam.position) < Vector3Distance(b, g.cam.position);
    });
    int count = 0;
    for (const Vector3 &p : lamps) {
        if (count >= MaxLamps) {
            break;
        }
        out[count++] = p;
    }
    return count;
}

// Smoke uses the lit shader's sky and lamp inputs with its own scattering shader.
void setSmokeUniforms(const Game &g) {
    const Vector3 lightDir = activeLightDir(g.night);
    const Vector3 lightCol = g.night ? Vector3{0.42f, 0.5f, 0.75f} : Vector3{1.25f, 1.15f, 0.95f};
    const Vector3 skyCol = g.night ? Vector3{0.14f, 0.16f, 0.24f} : Vector3{0.7f, 0.8f, 0.95f};
    const float exposure = g.night ? nightDim() * 2.0f : 1.0f;
    const float nightFlag = g.night ? 1.0f : 0.0f;
    const float torchFlag = (g.night && g.flashlight) ? 1.0f : 0.0f;
    const Vector3 camPos = g.cam.position;
    const Basis fb = gunBasis(g);
    Vector3 lampPos[MaxLamps]{};
    const int lampCount = collectLamps(g, lampPos);
    SetShaderValue(gSmoke, GetShaderLocation(gSmoke, "uLightDir"), &lightDir, SHADER_UNIFORM_VEC3);
    SetShaderValue(gSmoke, GetShaderLocation(gSmoke, "uLightColor"), &lightCol, SHADER_UNIFORM_VEC3);
    SetShaderValue(gSmoke, GetShaderLocation(gSmoke, "uSkyColor"), &skyCol, SHADER_UNIFORM_VEC3);
    SetShaderValue(gSmoke, GetShaderLocation(gSmoke, "uExposure"), &exposure, SHADER_UNIFORM_FLOAT);
    SetShaderValue(gSmoke, GetShaderLocation(gSmoke, "viewPos"), &camPos, SHADER_UNIFORM_VEC3);
    SetShaderValue(gSmoke, GetShaderLocation(gSmoke, "uNight"), &nightFlag, SHADER_UNIFORM_FLOAT);
    SetShaderValueV(gSmoke, GetShaderLocation(gSmoke, "uLampPos"), lampPos, SHADER_UNIFORM_VEC3, MaxLamps);
    SetShaderValue(gSmoke, GetShaderLocation(gSmoke, "uLampCount"), &lampCount, SHADER_UNIFORM_INT);
    SetShaderValue(gSmoke, GetShaderLocation(gSmoke, "uFlash"), &torchFlag, SHADER_UNIFORM_FLOAT);
    SetShaderValue(gSmoke, GetShaderLocation(gSmoke, "uFlashPos"), &fb.origin, SHADER_UNIFORM_VEC3);
    SetShaderValue(gSmoke, GetShaderLocation(gSmoke, "uFlashDir"), &fb.fwd, SHADER_UNIFORM_VEC3);
}

void setLitUniforms(const Game &g) {
    // Day: strong warm sun over a bright sky. Night: cool moon, dark sky, lamps. Brightness setting acts as exposure at night.
    const Vector3 lightDir = activeLightDir(g.night);
    const Vector3 lightCol = g.night ? Vector3{0.42f, 0.5f, 0.75f} : Vector3{1.25f, 1.15f, 0.95f};
    const Vector3 skyCol = g.night ? Vector3{0.14f, 0.16f, 0.24f} : Vector3{0.7f, 0.8f, 0.95f};
    const Vector3 groundCol = g.night ? Vector3{0.06f, 0.06f, 0.08f} : Vector3{0.3f, 0.27f, 0.22f};
    const float exposure = g.night ? nightDim() * 2.0f : 1.0f;
    const Vector3 camPos = g.cam.position;
    SetShaderValue(gLit, GetShaderLocation(gLit, "uLightDir"), &lightDir, SHADER_UNIFORM_VEC3);
    SetShaderValue(gLit, GetShaderLocation(gLit, "uLightColor"), &lightCol, SHADER_UNIFORM_VEC3);
    SetShaderValue(gLit, GetShaderLocation(gLit, "uSkyColor"), &skyCol, SHADER_UNIFORM_VEC3);
    SetShaderValue(gLit, GetShaderLocation(gLit, "uGroundColor"), &groundCol, SHADER_UNIFORM_VEC3);
    SetShaderValue(gLit, GetShaderLocation(gLit, "uExposure"), &exposure, SHADER_UNIFORM_FLOAT);
    SetShaderValue(gLit, GetShaderLocation(gLit, "viewPos"), &camPos, SHADER_UNIFORM_VEC3);

    Vector3 lampPos[MaxLamps]{};
    const int lampCount = collectLamps(g, lampPos);
    const float nightFlag = g.night ? 1.0f : 0.0f;
    SetShaderValueV(gLit, GetShaderLocation(gLit, "uLampPos"), lampPos, SHADER_UNIFORM_VEC3, MaxLamps);
    SetShaderValue(gLit, GetShaderLocation(gLit, "uLampCount"), &lampCount, SHADER_UNIFORM_INT);
    SetShaderValue(gLit, GetShaderLocation(gLit, "uNight"), &nightFlag, SHADER_UNIFORM_FLOAT);

    const float torchFlag = (g.night && g.flashlight) ? 1.0f : 0.0f;
    const Basis fb = gunBasis(g);
    SetShaderValue(gLit, GetShaderLocation(gLit, "uFlash"), &torchFlag, SHADER_UNIFORM_FLOAT);
    SetShaderValue(gLit, GetShaderLocation(gLit, "uFlashPos"), &fb.origin, SHADER_UNIFORM_VEC3);
    SetShaderValue(gLit, GetShaderLocation(gLit, "uFlashDir"), &fb.fwd, SHADER_UNIFORM_VEC3);
    constexpr int MaxMuzzles = 4;
    Vector3 muzzlePos[MaxMuzzles]{};
    float muzzleAmt[MaxMuzzles]{};
    int muzzleCount = 0;
    if (g.night) {
        if (g.fireGlow > 0.0f) {
            muzzlePos[muzzleCount] = muzzlePoint(fb);
            muzzleAmt[muzzleCount++] = g.fireGlow / FireTime;
        }
        for (const Enemy &e : g.enemies) {
            if (e.flash > 0.0f && e.hp > 0 && muzzleCount < MaxMuzzles) {
                muzzlePos[muzzleCount] = e.flashPos;
                muzzleAmt[muzzleCount++] = e.flash / FireTime;
            }
        }
    }
    SetShaderValueV(gLit, GetShaderLocation(gLit, "uMuzzlePos"), muzzlePos, SHADER_UNIFORM_VEC3, MaxMuzzles);
    SetShaderValueV(gLit, GetShaderLocation(gLit, "uMuzzleAmt"), muzzleAmt, SHADER_UNIFORM_FLOAT, MaxMuzzles);

    constexpr int MaxGrenLights = 4;
    Vector3 grenPos[MaxGrenLights]{};
    Vector3 grenCol[MaxGrenLights]{};
    int grenCount = 0;
    if (g.night) {
        for (const Grenade &gr : g.grenadeList) {
            if (grenCount < MaxGrenLights) {
                const Color c = grenadeColor(gr.kind);
                grenPos[grenCount] = gr.pos;
                grenCol[grenCount++] = Vector3{c.r / 255.0f, c.g / 255.0f, c.b / 255.0f};
            }
        }
    }
    SetShaderValueV(gLit, GetShaderLocation(gLit, "uGrenPos"), grenPos, SHADER_UNIFORM_VEC3, MaxGrenLights);
    SetShaderValueV(gLit, GetShaderLocation(gLit, "uGrenCol"), grenCol, SHADER_UNIFORM_VEC3, MaxGrenLights);
    SetShaderValue(gLit, GetShaderLocation(gLit, "uGrenCount"), &grenCount, SHADER_UNIFORM_INT);

    std::vector<Vector3> tvs;
    for (const Prop &p : g.level.props) {
        if (p.kind == PropKind::Tv && !p.broken) {
            tvs.push_back(propCenter(p));
        }
    }
    std::sort(tvs.begin(), tvs.end(), [&](const Vector3 &a, const Vector3 &b) {
        return Vector3Distance(a, g.cam.position) < Vector3Distance(b, g.cam.position);
    });
    Vector3 tvPos[MaxTvs]{};
    int tvCount = 0;
    for (const Vector3 &t : tvs) {
        if (tvCount >= MaxTvs) {
            break;
        }
        tvPos[tvCount++] = t;
    }
    SetShaderValueV(gLit, GetShaderLocation(gLit, "uTvPos"), tvPos, SHADER_UNIFORM_VEC3, MaxTvs);
    SetShaderValue(gLit, GetShaderLocation(gLit, "uTvCount"), &tvCount, SHADER_UNIFORM_INT);

}

// Flushes pending geometry first so only the following gun parts get the gloss.
void setGunGloss(bool on) {
    rlDrawRenderBatchActive();
    const float v = on ? 1.0f : 0.0f;
    SetShaderValue(gLit, GetShaderLocation(gLit, "uGloss"), &v, SHADER_UNIFORM_FLOAT);
}

// Flushes pending geometry first so only the following draws glow.
void setEmissive(bool on) {
    rlDrawRenderBatchActive();
    const float v = on ? 1.0f : 0.0f;
    SetShaderValue(gLit, GetShaderLocation(gLit, "uEmissive"), &v, SHADER_UNIFORM_FLOAT);
}

// Night flame cone out of a barrel, drawn with the fire shader.
void drawFlame(Vector3 at, Vector3 dir, float life) {
    const Vector3 tip = Vector3Add(at, Vector3Scale(dir, 0.55f));
    const float t = static_cast<float>(GetTime());
    EndShaderMode();
    BeginShaderMode(gFire);
    BeginBlendMode(BLEND_ADDITIVE);
    SetShaderValue(gFire, GetShaderLocation(gFire, "uCenter"), &at, SHADER_UNIFORM_VEC3);
    SetShaderValue(gFire, GetShaderLocation(gFire, "uDir"), &dir, SHADER_UNIFORM_VEC3);
    SetShaderValue(gFire, GetShaderLocation(gFire, "uTime"), &t, SHADER_UNIFORM_FLOAT);
    SetShaderValue(gFire, GetShaderLocation(gFire, "uLife"), &life, SHADER_UNIFORM_FLOAT);
    DrawCylinderEx(at, tip, 0.13f, 0.015f, 8, WHITE);
    rlDrawRenderBatchActive();
    EndBlendMode();
    EndShaderMode();
    beginLit();
}

void drawMuzzleFire(const Game &g) {
    if (!g.night || g.fireGlow <= 0.0f) {
        return;
    }
    const Basis b = gunBasis(g);
    drawFlame(muzzlePoint(b), b.fwd, g.fireGlow / FireTime);
}

// Visible sun by day and moon by night, drawn far away along the light direction, unlit.
void drawSkyBody(const Game &g) {
    const Vector3 at = Vector3Add(g.cam.position, Vector3Scale(activeLightDir(g.night), 380.0f));
    const float t = static_cast<float>(GetTime());
    EndShaderMode();
    if (g.night) {
        DrawSphere(at, 14.0f, Color{220, 230, 255, 255});
        DrawSphere(at, 30.0f, Fade(Color{150, 170, 230, 255}, 0.18f));
    } else {
        DrawSphere(at, 22.0f, Color{255, 255, 240, 255});
        DrawSphere(at, 48.0f + 3.0f * std::sin(t * 1.5f), Fade(Color{255, 235, 170, 255}, 0.18f));
        DrawSphere(at, 95.0f + 4.0f * std::sin(t * 0.9f), Fade(Color{255, 230, 160, 255}, 0.05f));
    }
    beginLit();
}

void drawGrenadesAndSmoke(const Game &g) {
    setEmissive(g.night);
    for (const Grenade &gr : g.grenadeList) {
        DrawSphere(gr.pos, 0.12f, grenadeColor(gr.kind));
    }
    setEmissive(false);
    const bool smokeLit = !gSettings.potato && !g.smokes.empty();
    if (smokeLit) {
        setSmokeUniforms(g);
        EndShaderMode();
        BeginShaderMode(gSmoke);
    }
    for (const SmokeCloud &s : g.smokes) {
        const float fade = std::min(1.0f, (SmokeLife - s.age) / 3.0f);
        DrawSphere(s.pos, SmokeRadius, Color{200, 200, 205, alphaOf(0.45f * fade)});
    }
    if (smokeLit) {
        EndShaderMode();
        beginLit();
    }
}

// Multiplies the current modelview by a projection onto y = groundY along the light direction.
void applyShadowProjection(Vector3 L, float groundY) {
    const float y = groundY + ShadowLift;
    // Column-major, as rlMultMatrixf expects.
    const float m[16] = {L.y, 0.0f, 0.0f, 0.0f,
                         -L.x, 0.0f, -L.z, 0.0f,
                         0.0f, 0.0f, L.y, 0.0f,
                         L.x * y, L.y * y, L.z * y, L.y};
    rlMultMatrixf(m);
}

// Flattens each moving object onto the ground beneath it as a dark silhouette.
void drawPlanarShadows(const Game &g, const View &view) {
    const Vector3 L = activeLightDir(g.night);
    if (L.y < 0.05f) {
        return;
    }
    EndShaderMode();
    BeginShaderMode(gShadow);
    BeginBlendMode(BLEND_ALPHA);
    for (const Car &c : g.cars) {
        const Vector3 p = carCenter(c);
        if (!inView(view, p, CarLength)) {
            continue;
        }
        rlPushMatrix();
        applyShadowProjection(L, groundAt(g.level.terrain, p.x, p.z));
        drawCar(c, g.level.terrain, 1.0f);
        rlDrawRenderBatchActive();
        rlPopMatrix();
    }
    for (const Ball &b : g.balls) {
        if (!inView(view, b.pos, BallRadius)) {
            continue;
        }
        rlPushMatrix();
        applyShadowProjection(L, groundAt(g.level.terrain, b.pos.x, b.pos.z));
        DrawSphere(b.pos, BallRadius, WHITE);
        rlDrawRenderBatchActive();
        rlPopMatrix();
    }
    for (const Enemy &e : g.enemies) {
        if (!inView(view, e.pos, 1.0f)) {
            continue;
        }
        rlPushMatrix();
        applyShadowProjection(L, groundAt(g.level.terrain, e.pos.x, e.pos.z));
        DrawCapsule(enemyPoint(e, 0.0f, 0.45f), enemyPoint(e, 0.0f, 1.3f), 0.35f, 8, 8, WHITE);
        DrawSphere(enemyPoint(e, 0.0f, HeadY), HeadRadius, WHITE);
        rlDrawRenderBatchActive();
        rlPopMatrix();
    }
    for (const Corpse &c : g.corpses) {
        rlPushMatrix();
        applyShadowProjection(L, groundAt(g.level.terrain, c.p[PHip].x, c.p[PHip].z));
        drawCorpse(c);
        rlDrawRenderBatchActive();
        rlPopMatrix();
    }
    EndBlendMode();
    EndShaderMode();
    beginLit();
}

void drawWorld(const Game &g) {
    // Potato mode has no lit shader, so night dimming is applied on the CPU instead.
    const float dim = (g.night && gSettings.potato) ? nightDim() : 1.0f;
    const View view{g.cam.position, forwardOf(g)};
    updateStatic();
    drawSkyBody(g);
    drawLevel(g.level, dim, &view);
    if (gSettings.shadows) {
        drawPlanarShadows(g, view);
    }
    drawDoors(g.level, &view, dim);
    drawLamps(g.level, g.night, &view);
    constexpr float BulbDrawDist = 60.0f;
    setEmissive(true);
    for (const Vector3 &b : g.level.bulbs) {
        if (Vector3Distance(b, g.cam.position) < BulbDrawDist) {
            DrawSphere(b, 0.15f, Color{255, 240, 190, 255});
        }
    }
    setEmissive(false);
    const bool greenX = signalGreenX();
    for (const Vector3 &s : g.level.signals) {
        if (inView(view, Vector3{s.x, s.y + 4.4f, s.z}, 1.2f)) {
            drawSignal(s, greenX);
        }
    }
    if (gSettings.shadows && gSettings.potato) {
        drawEntityShadows(g, view);
    }
    for (const Car &c : g.cars) {
        if (inView(view, carCenter(c), CarLength)) {
            drawCar(c, g.level.terrain, dim);
        }
    }
    for (const Ball &b : g.balls) {
        DrawSphere(b.pos, BallRadius, WHITE);
        DrawSphereWires(b.pos, BallRadius, 6, 6, Color{60, 60, 60, 255});
    }

    for (const Enemy &e : g.enemies) {
        if (!inView(view, enemyPoint(e, 0.0f, 0.9f), 1.5f)) {
            continue;
        }
        const bool blue = e.team == TeamBlue;
        const Color body = blue ? Color{60, 110, 220, 255} : Color{200, 60, 60, 255};
        DrawCapsule(enemyPoint(e, 0.0f, 0.45f), enemyPoint(e, 0.0f, 1.3f), 0.35f, 8, 8, body);
        DrawCapsuleWires(enemyPoint(e, 0.0f, 0.45f), enemyPoint(e, 0.0f, 1.3f), 0.35f, 8, 8, blue ? DARKBLUE : MAROON);
        DrawSphere(enemyPoint(e, 0.0f, HeadY), HeadRadius, Color{235, 200, 170, 255});
        DrawCapsule(enemyPoint(e, 0.3f, 1.25f), enemyPoint(e, 0.6f, 0.6f), 0.12f, 6, 6, body);
        DrawCapsule(enemyPoint(e, -0.3f, 1.25f), enemyPoint(e, -0.6f, 0.6f), 0.12f, 6, 6, body);
        // The enemy's gun sits in its right hand, barrel along the facing direction.
        const Vector3 hand{e.pos.x + e.dir.x * 0.35f, e.pos.y + 1.05f, e.pos.z + e.dir.z * 0.35f};
        rlPushMatrix();
        rlTranslatef(hand.x, hand.y, hand.z);
        rlRotatef(std::atan2(e.dir.x, e.dir.z) * RAD2DEG, 0.0f, 1.0f, 0.0f);
        {
            const RemoteSkinScope skins(e);
            drawWeaponParts(e.weapon, false);
        }
        rlPopMatrix();
        drawCosmetics(e);
    }
    if (g.night) {
        for (const Enemy &e : g.enemies) {
            if (e.flash > 0.0f && e.hp > 0) {
                drawFlame(e.flashPos, e.flashDir, e.flash / FireTime);
            }
        }
    }

    // Unlit warning glow for sniper aims: additive muzzle bloom plus a laser line to the player.
    bool anySniper = false;
    for (const Enemy &e : g.enemies) {
        anySniper = anySniper || (e.aimingSniper && e.hp > 0);
    }
    if (anySniper) {
        const float pulse = 0.65f + 0.35f * std::sin(static_cast<float>(GetTime()) * 18.0f);
        const Vector3 chest{g.pos.x, g.pos.y + g.height * 0.7f, g.pos.z};
        EndShaderMode();
        BeginBlendMode(BLEND_ADDITIVE);
        for (const Enemy &e : g.enemies) {
            if (!e.aimingSniper || e.hp <= 0) {
                continue;
            }
            const Vector3 muzzle{e.pos.x + e.dir.x * 1.05f, e.pos.y + 1.05f, e.pos.z + e.dir.z * 1.05f};
            DrawSphere(muzzle, 0.14f, Color{255, 70, 50, 255});
            DrawSphere(muzzle, 0.42f, Fade(Color{255, 40, 20, 255}, 0.35f * pulse));
            DrawLine3D(muzzle, chest, Fade(Color{255, 50, 40, 255}, 0.55f * pulse));
        }
        rlDrawRenderBatchActive();
        EndBlendMode();
        beginLit();
    }
    for (const Corpse &c : g.corpses) {
        drawCorpse(c);
    }

    if (g.tracerTimer > 0.0f) {
        DrawLine3D(g.tracerFrom, g.tracerTo, YELLOW);
    }

    drawPickups(g, view, dim);
    drawGrenadesAndSmoke(g);
    drawBirds(g);
    drawMuzzleFire(g);
    drawWeapon(g);
}

void drawDoorPrompt(const Game &g) {
    for (const Chest &c : g.chests) {
        if (!c.opened && std::hypot(c.pos.x - g.pos.x, c.pos.z - g.pos.z) <= ChestReach) {
            DrawText("E: open chest", 20, 218, 20, RAYWHITE);
            return;
        }
    }
    const int i = nearDoor(g);
    if (i >= 0) {
        DrawText(g.level.doors[i].open ? "E: close door" : "E: open door", 20, 218, 20, RAYWHITE);
    }
}

// Circular reload countdown: sweeping arc, pulsing ring, spinning ticks and seconds left.
void drawReloadRing(int cx, int cy, float remaining, float total) {
    const Vector2 center{static_cast<float>(cx), static_cast<float>(cy)};
    const float t = static_cast<float>(GetTime());
    const float progress = 1.0f - remaining / total;
    const float r = 46.0f + 3.0f * std::sin(t * 10.0f);
    DrawRing(center, r - 4.0f, r, 0.0f, 360.0f, 64, Fade(GRAY, 0.35f));
    DrawRing(center, r - 8.0f, r - 2.0f, -90.0f, -90.0f + 360.0f * progress, 64, Color{255, 200, 60, 255});
    for (int k = 0; k < 12; ++k) {
        const float a = (t * 120.0f + static_cast<float>(k) * 30.0f) * DEG2RAD;
        const Vector2 inner{center.x + std::cos(a) * (r + 6.0f), center.y + std::sin(a) * (r + 6.0f)};
        const Vector2 outer{center.x + std::cos(a) * (r + 13.0f), center.y + std::sin(a) * (r + 13.0f)};
        DrawLineEx(inner, outer, 2.0f, Fade(WHITE, 0.6f));
    }
    const char *text = TextFormat("%.1f", remaining);
    DrawText(text, cx - MeasureText(text, 24) / 2, cy - 12, 24, YELLOW);
}

// Draws the icon of one throwable centred on (cx, cy).
void drawThrowIcon(int kind, float cx, float cy, Color tint) {
    switch (kind) {
    case ThrowFrag:
        DrawCircle(static_cast<int>(cx), static_cast<int>(cy), 12.0f, tint);
        DrawRectangle(static_cast<int>(cx) - 4, static_cast<int>(cy) - 19, 8, 8, tint);
        break;
    case ThrowFlash:
        DrawCircle(static_cast<int>(cx), static_cast<int>(cy), 12.0f, tint);
        DrawCircleLines(static_cast<int>(cx), static_cast<int>(cy), 17.0f, tint);
        break;
    case ThrowSmoke:
        DrawCircle(static_cast<int>(cx) - 6, static_cast<int>(cy), 9.0f, tint);
        DrawCircle(static_cast<int>(cx) + 6, static_cast<int>(cy), 9.0f, tint);
        DrawCircle(static_cast<int>(cx), static_cast<int>(cy) - 6, 9.0f, tint);
        break;
    default:
        DrawRectangle(static_cast<int>(cx) - 9, static_cast<int>(cy) - 13, 18, 26, tint);
        break;
    }
}

// Bottom-right slot bar: the weapon slot, then throwables on keys 4-7 with stock counts.
void drawInventory(const Game &g) {
    const int slot = 60;
    const int gap = 8;
    const int right = GetScreenWidth() - 20;
    const int top = GetScreenHeight() - 52 - 12 - slot;
    const Color kindColor[ThrowCount] = {Color{90, 170, 90, 255}, Color{240, 240, 215, 255},
                                         Color{150, 150, 165, 255}, Color{235, 140, 60, 255}};

    const int throwX = right - (ThrowCount * slot + (ThrowCount - 1) * gap);
    for (int k = 0; k < ThrowCount; ++k) {
        const Rectangle box{static_cast<float>(throwX + k * (slot + gap)), static_cast<float>(top),
                            static_cast<float>(slot), static_cast<float>(slot)};
        const bool selected = g.holding && g.throwSel == k;
        const bool empty = g.stock[k] <= 0;
        DrawRectangleRec(box, Fade(BLACK, 0.55f));
        DrawRectangleLinesEx(box, selected ? 3.0f : 1.0f, selected ? YELLOW : GRAY);
        const Color tint = empty ? Fade(GRAY, 0.35f) : kindColor[k];
        drawThrowIcon(k, box.x + slot / 2.0f, box.y + slot / 2.0f - 3.0f, tint);
        DrawText(TextFormat("%d", k + 4), static_cast<int>(box.x) + 4, static_cast<int>(box.y) + 3, 16,
                 selected ? YELLOW : RAYWHITE);
        DrawText(TextFormat("x%d", g.stock[k]), static_cast<int>(box.x) + slot - 30, static_cast<int>(box.y) + slot - 20,
                 18, RAYWHITE);
    }

    const int gunX = throwX - gap - slot;
    const Rectangle gunBox{static_cast<float>(gunX), static_cast<float>(top), static_cast<float>(slot),
                           static_cast<float>(slot)};
    DrawRectangleRec(gunBox, Fade(BLACK, 0.55f));
    DrawRectangleLinesEx(gunBox, 1.0f, GRAY);
    DrawText(TextFormat("1-3"), gunX + 4, top + 3, 16, RAYWHITE);
    DrawText(Weapons[g.weapon].name, gunX + 4, top + slot / 2 + 6, 14, LIGHTGRAY);
}

// Scoped view: black outside a ring, thin black crosshair inside.
void drawScope(int cx, int cy) {
    const Vector2 c{static_cast<float>(cx), static_cast<float>(cy)};
    const float r = static_cast<float>(GetScreenHeight()) * 0.42f;
    const float outer = std::hypot(static_cast<float>(GetScreenWidth()), static_cast<float>(GetScreenHeight())) * 0.5f;
    DrawRing(c, r, outer, 0.0f, 360.0f, 128, BLACK);
    DrawRing(c, r - 2.0f, r, 0.0f, 360.0f, 128, BLACK);
    DrawLineEx(Vector2{c.x - r, c.y}, Vector2{c.x - 6.0f, c.y}, 2.0f, BLACK);
    DrawLineEx(Vector2{c.x + 6.0f, c.y}, Vector2{c.x + r, c.y}, 2.0f, BLACK);
    DrawLineEx(Vector2{c.x, c.y - r}, Vector2{c.x, c.y - 6.0f}, 2.0f, BLACK);
    DrawLineEx(Vector2{c.x, c.y + 6.0f}, Vector2{c.x, c.y + r}, 2.0f, BLACK);
}

// Top-right feed: killer, weapon and icons (headshot, flash, smoke), then victim.
void drawKillFeed(const Game &g) {
    constexpr int TextSize = 20;
    constexpr int IconSlot = 24;
    const int right = GetScreenWidth() - 20;
    int y = 20;
    for (const KillEntry &k : g.killFeed) {
        const float alpha = std::min(1.0f, KillFeedTime - k.age);
        const int icons = (k.headshot ? 1 : 0) + (k.blind ? 1 : 0) + (k.smoke ? 1 : 0);
        const int killerW = MeasureText(k.killer.c_str(), TextSize);
        const int weaponW = MeasureText(k.weapon.c_str(), TextSize);
        const int victimW = MeasureText(k.victim.c_str(), TextSize);
        int x = right - (killerW + 8 + weaponW + 8 + icons * IconSlot + victimW);
        DrawText(k.killer.c_str(), x, y, TextSize, Fade(k.killerColor, alpha));
        x += killerW + 8;
        DrawText(k.weapon.c_str(), x, y, TextSize, Fade(RAYWHITE, alpha));
        x += weaponW + 8;
        if (k.headshot) {
            DrawCircle(x + 9, y + 10, 9.0f, Fade(RED, alpha));
            DrawLine(x + 4, y + 10, x + 14, y + 10, Fade(WHITE, alpha));
            DrawLine(x + 9, y + 5, x + 9, y + 15, Fade(WHITE, alpha));
            x += IconSlot;
        }
        if (k.blind) {
            DrawCircle(x + 9, y + 10, 9.0f, Fade(WHITE, alpha));
            DrawCircleLines(x + 9, y + 10, 9.0f, Fade(YELLOW, alpha));
            x += IconSlot;
        }
        if (k.smoke) {
            DrawCircle(x + 9, y + 10, 9.0f, Fade(GRAY, alpha));
            DrawCircle(x + 5, y + 7, 5.0f, Fade(LIGHTGRAY, alpha));
            x += IconSlot;
        }
        DrawText(k.victim.c_str(), x, y, TextSize, Fade(k.victimColor, alpha));
        y += 28;
    }
}

void drawHud(const Game &g, const Playlist &pl) {
    const int acc = g.shots > 0 ? (100 * g.hits) / g.shots : 0;
    DrawText(TextFormat("Map: %s", Maps[g.mapIndex].name), 20, 20, 24, RAYWHITE);
    DrawText(TextFormat("Money: $%d", g.money), 20, 50, 24, RAYWHITE);
    DrawText(TextFormat("Wave: %d", g.wave), 20, 80, 24, RAYWHITE);
    DrawText(TextFormat("Enemies: %d", static_cast<int>(g.enemies.size())), 20, 110, 24, RAYWHITE);
    DrawText(TextFormat("Shots: %d  Hits: %d  Accuracy: %d%%", g.shots, g.hits, acc), 20, 140, 20, RAYWHITE);
    DrawText(TextFormat("Fire: %s (X to toggle)", g.rapid ? "rapid" : "single"), 20, 166, 20, RAYWHITE);
    DrawText(TextFormat("Time: %s (N)%s", g.night ? "night" : "day",
                        g.night ? (g.flashlight ? "   Flashlight on (F)" : "   Flashlight off (F)") : ""),
             20, 192, 20, RAYWHITE);
    DrawText(gSettings.teamMode ? "Mode: team (blue allies)" : "Mode: deathmatch", 20, 244, 20, RAYWHITE);
    DrawText("LMB: shoot/stab   R: reload   1/2/3: weapon   4-7: throwable   MMB: throw   B: buy   RMB: scope   WASD: move   Shift: sprint   Ctrl/C: crouch   Space: jump   G: kick   F5: restart   E: door/chest   ESC: menu",
             20, GetScreenHeight() - 30, 18, RAYWHITE);
    DrawText("Left/Right: track   Up/Down: volume   M: mute", 20, GetScreenHeight() - 52, 18, RAYWHITE);
    drawMusicInfo(pl);
    drawDoorPrompt(g);
    drawKillFeed(g);
    if (const int di = nearDroppedWeapon(g); di >= 0) {
        const char *txt = TextFormat("[E] Pick up %s", Weapons[g.drops[static_cast<size_t>(di)].weapon].name);
        DrawText(txt, GetScreenWidth() / 2 - MeasureText(txt, 22) / 2, GetScreenHeight() - 150, 22, YELLOW);
    }
    const int statTop = 20 + static_cast<int>(KillFeedMax) * 28 + 12;
    DrawText(TextFormat("Health: %d", static_cast<int>(std::max(0.0f, g.health))), GetScreenWidth() - 260, statTop, 24,
             RAYWHITE);
    DrawText(TextFormat("Armor: %d", static_cast<int>(g.armor)), GetScreenWidth() - 260, statTop + 26, 24, SKYBLUE);
    const bool gun = g.weapon != WeaponKnife;
    DrawText(TextFormat("Weapon: %s", Weapons[g.weapon].name), GetScreenWidth() - 260, statTop + 56, 24, RAYWHITE);
    int y = statTop + 82;
    const int shownMag = g.mags[g.weapon];
    if (gun) {
        DrawText(TextFormat("Ammo: %d / %d", shownMag, g.reserve), GetScreenWidth() - 260, y, 24, RAYWHITE);
        y += 26;
    }
    drawInventory(g);
    y += 30;
    if (gun && g.reloadTimer > 0.0f) {
        DrawText("Reloading...", GetScreenWidth() - 260, y, 20, YELLOW);
    } else if (gun && shownMag == 0) {
        DrawText(g.reserve > 0 ? "Press R to reload" : "No ammo", GetScreenWidth() - 260, y, 20, ORANGE);
    }

    const int cx = GetScreenWidth() / 2;
    const int cy = GetScreenHeight() / 2;
    if (g.scoped) {
        drawScope(cx, cy);
    } else {
        DrawCircleLines(cx, cy, 3.0f, WHITE);
        DrawLine(cx - 10, cy, cx - 4, cy, WHITE);
        DrawLine(cx + 4, cy, cx + 10, cy, WHITE);
        DrawLine(cx, cy - 10, cx, cy - 4, WHITE);
        DrawLine(cx, cy + 4, cx, cy + 10, WHITE);
    }
    if (g.reloadTimer > 0.0f) {
        drawReloadRing(cx, cy, g.reloadTimer, reloadTimeOf(g));
    }

    if (g.hitTextTimer > 0.0f) {
        DrawText(g.lastHit.c_str(), cx - 60, cy + 30, 28, YELLOW);
    }
    if (g.killFlash > 0.0f) {
        const char *kill = "KILL";
        DrawText(kill, cx - MeasureText(kill, 40) / 2, cy - 70, 40, Fade(RED, std::min(1.0f, g.killFlash / KillFlashTime)));
    }
}

// Red screen edges below LowHealthFrac, a red flash on hit, and blood splats.
void drawBlood(const Game &g) {
    const int w = GetScreenWidth();
    const int h = GetScreenHeight();
    const float lowLine = PlayerMaxHealth * LowHealthFrac;
    if (g.health < lowLine) {
        const float depth = 1.0f - std::max(0.0f, g.health) / lowLine;
        const float pulse = 0.85f + 0.15f * std::sin(static_cast<float>(GetTime()) * 5.0f);
        const unsigned char a = alphaOf((0.35f + 0.45f * depth) * pulse);
        const Color edge{130, 0, 0, a};
        const Color clear{130, 0, 0, 0};
        const int ew = w / 4;
        const int eh = h / 4;
        DrawRectangleGradientH(0, 0, ew, h, edge, clear);
        DrawRectangleGradientH(w - ew, 0, ew, h, clear, edge);
        DrawRectangleGradientV(0, 0, w, eh, edge, clear);
        DrawRectangleGradientV(0, h - eh, w, eh, clear, edge);
    }
    if (g.flashed > 0.0f) {
        DrawRectangle(0, 0, w, h, Color{255, 255, 255, alphaOf(g.flashed / FlashPlayerTime * lookAt(g, g.flashAt))});
    }
    // Smoke hides the view when you stand in it or look through it.
    const Vector3 ahead = Vector3Add(g.cam.position, Vector3Scale(forwardOf(g), SmokeSightRange));
    const bool inside = std::any_of(g.smokes.begin(), g.smokes.end(), [&](const SmokeCloud &s) {
        return Vector3Distance(s.pos, g.cam.position) < SmokeRadius;
    });
    if (inside) {
        DrawRectangle(0, 0, w, h, Color{190, 190, 195, 200});
    } else if (smoked(g, g.cam.position, ahead)) {
        DrawRectangle(0, 0, w, h, Color{190, 190, 195, 120});
    }
    if (g.hurtTimer > 0.0f) {
        DrawRectangle(0, 0, w, h, Color{180, 0, 0, alphaOf(0.35f * g.hurtTimer / HurtFlashTime)});
    }
    for (const BloodSplat &s : g.splats) {
        const unsigned char a = alphaOf(1.0f - s.age / SplatLife);
        const Vector2 c{s.x * static_cast<float>(w), s.y * static_cast<float>(h)};
        DrawCircleV(c, s.r, Color{110, 0, 0, a});
        DrawCircleV(Vector2{c.x + s.r * 0.7f, c.y - s.r * 0.4f}, s.r * 0.5f, Color{110, 0, 0, a});
        DrawCircleV(Vector2{c.x - s.r * 0.6f, c.y + s.r * 0.6f}, s.r * 0.35f, Color{110, 0, 0, a});
    }
}

constexpr int VisibleRows = 2;

struct MenuState {
    int sel = 0;
    int scroll = 0;
    float angle[MapCount]{};
    bool dirty[MapCount]{};
};

int maxScroll() {
    return std::max(0, (MapCount + GridCols - 1) / GridCols - VisibleRows);
}

bool cardVisible(int i, int scroll) {
    const int row = i / GridCols - scroll;
    return row >= 0 && row < VisibleRows;
}

void keepVisible(MenuState &menu) {
    const int row = menu.sel / GridCols;
    if (row < menu.scroll) {
        menu.scroll = row;
    } else if (row >= menu.scroll + VisibleRows) {
        menu.scroll = row - VisibleRows + 1;
    }
    menu.scroll = std::clamp(menu.scroll, 0, maxScroll());
}

Rectangle menuCardRect(int i, int scroll) {
    const int col = i % GridCols, row = i / GridCols - scroll;
    const float totalW = GridCols * CardW + (GridCols - 1) * CardGap;
    return Rectangle{GetScreenWidth() / 2.0f - totalW / 2.0f + col * (CardW + CardGap),
                     GridTop + row * (CardH + CardGap), CardW, CardH};
}

int hoveredCard(int scroll) {
    const Vector2 m = GetMousePosition();
    for (int i = 0; i < MapCount; ++i) {
        if (cardVisible(i, scroll) && CheckCollisionPointRec(m, menuCardRect(i, scroll))) {
            return i;
        }
    }
    return -1;
}

// Showcase cards rotate only while the mouse is over them.
int updateMenu(MenuState &menu, float dt) {
    if (IsKeyPressed(KEY_D)) {
        menu.sel = (menu.sel + 1) % MapCount;
    }
    if (IsKeyPressed(KEY_A)) {
        menu.sel = (menu.sel + MapCount - 1) % MapCount;
    }
    if (IsKeyPressed(KEY_S)) {
        menu.sel = (menu.sel + GridCols) % MapCount;
    }
    if (IsKeyPressed(KEY_W)) {
        menu.sel = (menu.sel + MapCount - GridCols) % MapCount;
    }
    if (IsKeyPressed(KEY_D) || IsKeyPressed(KEY_A) || IsKeyPressed(KEY_S) || IsKeyPressed(KEY_W)) {
        keepVisible(menu);
    }
    const float wheel = GetMouseWheelMove();
    if (wheel != 0.0f) {
        menu.scroll = std::clamp(menu.scroll + (wheel > 0.0f ? -1 : 1), 0, maxScroll());
    }
    const int hover = hoveredCard(menu.scroll);
    if (hover >= 0) {
        if (Vector2Length(GetMouseDelta()) > 0.0f) {
            menu.sel = hover;
        }
        menu.angle[hover] += dt * 0.8f;
        menu.dirty[hover] = true;
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            return hover;
        }
    }
    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE)) {
        return menu.sel;
    }
    return -1;
}

void renderPreview(RenderTexture2D &rt, const Level &L, float angle) {
    const float radius = L.terrain.half * 0.6f;
    Camera3D cam{};
    cam.position = Vector3{std::cos(angle) * radius, radius * 0.5f, std::sin(angle) * radius};
    cam.target = Vector3{0.0f, 0.0f, 0.0f};
    cam.up = Vector3{0.0f, 1.0f, 0.0f};
    cam.fovy = 55.0f;
    cam.projection = CAMERA_PERSPECTIVE;
    BeginTextureMode(rt);
    ClearBackground(L.sky);
    BeginMode3D(cam);
    drawLevel(L, 1.0f);
    EndMode3D();
    EndTextureMode();
}

constexpr int RowResolution = 0;
constexpr int RowFullscreen = 1;
constexpr int RowQuality = 2;
constexpr int RowShadows = 3;
constexpr int RowBrightness = 4;
constexpr int RowTeam = 5;
constexpr int RowAddMusic = 6;
constexpr int RowPotato = 7;
constexpr int RowBack = 8;
constexpr int RowCount = 9;
const char *const SettingLabels[RowCount] = {"Resolution (windowed)", "Fullscreen", "Graphics quality",
                                             "Shadows", "Night brightness", "Team mode (blue allies)",
                                             "Add music", "Potato graphics (no shaders)", "Back"};

struct SettingsMenu {
    int sel = 0;
};

void adjustSetting(int row, int dir) {
    Settings &s = gSettings;
    switch (row) {
    case RowResolution:
        s.resolution = (s.resolution + dir + ResolutionCount) % ResolutionCount;
        gDisplayDirty = true;
        break;
    case RowFullscreen:
        s.fullscreen = !s.fullscreen;
        gDisplayDirty = true;
        break;
    case RowQuality:
        s.quality = (s.quality + dir + QualityCount) % QualityCount;
        break;
    case RowShadows:
        s.shadows = !s.shadows;
        break;
    case RowBrightness:
        s.brightness = (s.brightness + dir + BrightnessCount) % BrightnessCount;
        break;
    case RowTeam:
        s.teamMode = !s.teamMode;
        break;
    case RowAddMusic:
        gRescanMusic = true;
        break;
    case RowPotato:
        s.potato = !s.potato;
        break;
    default:
        break;
    }
}

void applyDisplay() {
    if (!gDisplayDirty) {
        return;
    }
    gDisplayDirty = false;
    if (IsWindowFullscreen() != gSettings.fullscreen) {
        ToggleFullscreen();
    }
    if (!gSettings.fullscreen) {
        const Resolution r = Resolutions[gSettings.resolution];
        SetWindowSize(r.w, r.h);
    }
}

const char *settingValue(int row) {
    const Settings &s = gSettings;
    switch (row) {
    case RowResolution:
        return TextFormat("%dx%d", Resolutions[s.resolution].w, Resolutions[s.resolution].h);
    case RowFullscreen:
        return s.fullscreen ? "On" : "Off";
    case RowQuality:
        return QualityNames[s.quality];
    case RowShadows:
        return s.shadows ? "On" : "Off";
    case RowBrightness:
        return BrightnessNames[s.brightness];
    case RowTeam:
        return s.teamMode ? "On" : "Off";
    case RowAddMusic:
        return "Rescan";
    case RowPotato:
        return s.potato ? "On" : "Off";
    default:
        return "";
    }
}

Rectangle settingsRowRect(int row) {
    return Rectangle{GetScreenWidth() / 2.0f - 280.0f, 170.0f + row * 58.0f, 560.0f, 46.0f};
}

Rectangle settingsButtonRect() {
    return Rectangle{GetScreenWidth() - 190.0f, 20.0f, 170.0f, 40.0f};
}

bool settingsButtonPressed() {
    return IsKeyPressed(KEY_O) ||
           (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(GetMousePosition(), settingsButtonRect()));
}

Rectangle modsButtonRect() {
    return Rectangle{GetScreenWidth() - 190.0f, 68.0f, 170.0f, 40.0f};
}

bool modsButtonPressed() {
    if (gModsLocked) {
        return false;
    }
    return IsKeyPressed(KEY_TAB) ||
           (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(GetMousePosition(), modsButtonRect()));
}

void drawModsButton() {
    const Rectangle r = modsButtonRect();
    const bool hot = CheckCollisionPointRec(GetMousePosition(), r);
    DrawRectangleRec(r, hot ? Color{70, 74, 96, 255} : Color{40, 42, 56, 255});
    DrawRectangleLinesEx(r, 2.0f, RAYWHITE);
    const char *label = gModsLocked ? "Mods locked" : "Mods (TAB)";
    DrawText(label, static_cast<int>(r.x + r.width / 2) - MeasureText(label, 22) / 2, static_cast<int>(r.y) + 9, 22,
             RAYWHITE);
}

void drawSettingsButton() {
    const Rectangle r = settingsButtonRect();
    const bool hot = CheckCollisionPointRec(GetMousePosition(), r);
    DrawRectangleRec(r, hot ? Color{70, 74, 96, 255} : Color{40, 42, 56, 255});
    DrawRectangleLinesEx(r, 2.0f, RAYWHITE);
    const char *label = "Settings";
    DrawText(label, static_cast<int>(r.x + r.width / 2) - MeasureText(label, 22) / 2, static_cast<int>(r.y) + 9, 22,
             RAYWHITE);
}

// Returns true when leaving the settings screen.
bool updateSettings(SettingsMenu &sm) {
    if (IsKeyPressed(KEY_ESCAPE)) {
        return true;
    }
    if (IsKeyPressed(KEY_W) || IsKeyPressed(KEY_UP)) {
        sm.sel = (sm.sel + RowCount - 1) % RowCount;
    }
    if (IsKeyPressed(KEY_S) || IsKeyPressed(KEY_DOWN)) {
        sm.sel = (sm.sel + 1) % RowCount;
    }
    if (IsKeyPressed(KEY_A) || IsKeyPressed(KEY_LEFT)) {
        adjustSetting(sm.sel, -1);
    }
    if (IsKeyPressed(KEY_D) || IsKeyPressed(KEY_RIGHT)) {
        adjustSetting(sm.sel, 1);
    }
    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE)) {
        if (sm.sel == RowBack) {
            return true;
        }
        adjustSetting(sm.sel, 1);
    }
    for (int i = 0; i < RowCount; ++i) {
        if (!CheckCollisionPointRec(GetMousePosition(), settingsRowRect(i))) {
            continue;
        }
        if (Vector2Length(GetMouseDelta()) > 0.0f) {
            sm.sel = i;
        }
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            if (i == RowBack) {
                return true;
            }
            adjustSetting(i, 1);
        }
    }
    return false;
}

void drawSettings(const SettingsMenu &sm, size_t trackCount) {
    const char *title = "Settings";
    DrawText(title, GetScreenWidth() / 2 - MeasureText(title, 60) / 2, 60, 60, RAYWHITE);
    for (int i = 0; i < RowCount; ++i) {
        const Rectangle r = settingsRowRect(i);
        DrawRectangleRec(r, i == sm.sel ? Color{60, 64, 84, 255} : Color{40, 42, 56, 255});
        if (i == sm.sel) {
            DrawRectangleLinesEx(r, 3.0f, Color{200, 70, 50, 255});
        }
        if (i == RowBack) {
            DrawText(SettingLabels[i], GetScreenWidth() / 2 - MeasureText(SettingLabels[i], 24) / 2,
                     static_cast<int>(r.y) + 11, 24, RAYWHITE);
            continue;
        }
        DrawText(SettingLabels[i], static_cast<int>(r.x) + 16, static_cast<int>(r.y) + 11, 24, RAYWHITE);
        const char *val = settingValue(i);
        DrawText(val, static_cast<int>(r.x + r.width) - 16 - MeasureText(val, 24), static_cast<int>(r.y) + 11, 24,
                 YELLOW);
    }
    const std::string dir = std::string(GetApplicationDirectory()) + "music";
    const int infoY = 170 + RowCount * 58 + 14;
    const char *tip = "Put .mp3 files into this folder, then select Add music to rescan:";
    DrawText(tip, GetScreenWidth() / 2 - MeasureText(tip, 20) / 2, infoY, 20, RAYWHITE);
    DrawText(dir.c_str(), GetScreenWidth() / 2 - MeasureText(dir.c_str(), 20) / 2, infoY + 26, 20, YELLOW);
    DrawText(TextFormat("Tracks found: %d", static_cast<int>(trackCount)), GetScreenWidth() / 2 - 80, infoY + 52, 20,
             GRAY);
    const int H = GetScreenHeight();
    const char *hint = "W/S: select   A/D or click: change   Esc: back";
    DrawText(hint, GetScreenWidth() / 2 - MeasureText(hint, 18) / 2, H - 30, 18, GRAY);
}

std::string randomPlayerName() {
    char buf[16];
    std::snprintf(buf, sizeof buf, "Player%04d", GetRandomValue(0, 9999));
    return buf;
}

std::string settingsFilePath() {
    return std::string(GetApplicationDirectory()) + "settings.cfg";
}

void saveSettings() {
    std::ofstream out(settingsFilePath());
    out << "resolution=" << gSettings.resolution << '\n'
        << "fullscreen=" << gSettings.fullscreen << '\n'
        << "quality=" << gSettings.quality << '\n'
        << "shadows=" << gSettings.shadows << '\n'
        << "brightness=" << gSettings.brightness << '\n'
        << "potato=" << gSettings.potato << '\n'
        << "hair=" << gPlayerLook.hair << '\n'
        << "hairColor=" << gPlayerLook.hairColor << '\n'
        << "hat=" << gPlayerLook.hat << '\n'
        << "hatColor=" << gPlayerLook.hatColor << '\n'
        << "jewel=" << gPlayerLook.jewel << '\n'
        << "logo=" << gPlayerLook.logo << '\n'
        << "shirt=" << gPlayerLook.shirt << '\n'
        << "name=" << gPlayerName << '\n'
        << "server=" << gServerAddress << '\n'
        << "playerid=" << gPlayerCustomId << '\n';
}

void loadSettings() {
    std::ifstream in(settingsFilePath());
    std::string line;
    while (std::getline(in, line)) {
        const size_t eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        const std::string key = line.substr(0, eq);
        if (key == "name") {
            gPlayerName = line.substr(eq + 1, MaxNameLen);
            continue;
        }
        if (key == "server") {
            gServerAddress = line.substr(eq + 1);
            continue;
        }
        if (key == "playerid") {
            gPlayerCustomId = line.substr(eq + 1);
            continue;
        }
        const int value = std::atoi(line.c_str() + eq + 1);
        if (key == "resolution") {
            gSettings.resolution = std::clamp(value, 0, ResolutionCount - 1);
        } else if (key == "fullscreen") {
            gSettings.fullscreen = value != 0;
        } else if (key == "quality") {
            gSettings.quality = std::clamp(value, 0, QualityCount - 1);
        } else if (key == "shadows") {
            gSettings.shadows = value != 0;
        } else if (key == "brightness") {
            gSettings.brightness = std::clamp(value, 0, BrightnessCount - 1);
        } else if (key == "potato") {
            gSettings.potato = value != 0;
        } else if (key == "hair") {
            gPlayerLook.hair = std::clamp(value, 0, HairStyleCount - 1);
        } else if (key == "hairColor") {
            gPlayerLook.hairColor = std::clamp(value, 0, static_cast<int>(std::size(HairColors)) - 1);
        } else if (key == "hat") {
            gPlayerLook.hat = std::clamp(value, 0, HatStyleCount - 1);
        } else if (key == "hatColor") {
            gPlayerLook.hatColor = std::clamp(value, 0, static_cast<int>(std::size(HatColors)) - 1);
        } else if (key == "jewel") {
            gPlayerLook.jewel = std::clamp(value, 0, JewelStyleCount - 1);
        } else if (key == "logo") {
            gPlayerLook.logo = std::clamp(value, 0, LogoStyleCount - 1);
        } else if (key == "shirt") {
            gPlayerLook.shirt = std::clamp(value, 0, ShirtColorCount - 1);
        }
    }
    if (gPlayerName.empty()) {
        gPlayerName = randomPlayerName();
    }
    gDisplayDirty = true;
}

Texture2D gLogoTex{};

// Sets the window icon and keeps the embedded logo PNG as the menu texture.
void setGameIcon() {
    Image icon = LoadImageFromMemory(".png", kLogoPng, static_cast<int>(kLogoPngSize));
    SetWindowIcon(icon);
    gLogoTex = LoadTextureFromImage(icon);
    UnloadImage(icon);
}

// Title lockup: crosshair emblem, shadowed wordmark and subtitle.
void drawLogo() {
    const Color red{220, 60, 40, 255};
    const char *word = "OVERKILL";
    const int size = 72;
    const int x = GetScreenWidth() / 2 - MeasureText(word, size) / 2;
    const int y = 14;
    const float ex = static_cast<float>(x - 48);
    const float ey = static_cast<float>(y + 38);
    const Rectangle src{0.0f, 0.0f, static_cast<float>(gLogoTex.width), static_cast<float>(gLogoTex.height)};
    const Rectangle dst{ex - 48.0f, ey - 48.0f, 96.0f, 96.0f};
    DrawTexturePro(gLogoTex, src, dst, Vector2{0.0f, 0.0f}, 0.0f, WHITE);
    DrawText(word, x + 4, y + 4, size, Color{0, 0, 0, 160});
    DrawText(word, x, y, size, RAYWHITE);
    DrawRectangle(x, y + size + 2, MeasureText(word, size), 4, red);
    const char *sub = "common";
    DrawText(sub, GetScreenWidth() / 2 - MeasureText(sub, 26) / 2, y + size + 12, 26, LIGHTGRAY);
}

// Small preview tile in the top-left corner; click it (or press C) to edit the avatar.
Rectangle avatarTileRect() {
    return Rectangle{16.0f, 16.0f, 90.0f, 90.0f};
}

bool avatarButtonPressed() {
    return IsKeyPressed(KEY_C) ||
           (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(GetMousePosition(), avatarTileRect()));
}

void drawAvatarTile() {
    const Rectangle r = avatarTileRect();
    const bool hot = CheckCollisionPointRec(GetMousePosition(), r);
    drawLookPortrait(gPlayerLook, ShirtColors[gPlayerLook.shirt], r, hot);
    const char *label = "Avatar (C)";
    DrawText(label, static_cast<int>(r.x + r.width / 2) - MeasureText(label, 18) / 2, static_cast<int>(r.y + r.height) + 4,
             18, RAYWHITE);
}

// Avatar editor: rows change the player's look; the preview on the left shows the result.
constexpr int AvatarHair = 0;
constexpr int AvatarHairColor = 1;
constexpr int AvatarHat = 2;
constexpr int AvatarHatColor = 3;
constexpr int AvatarJewel = 4;
constexpr int AvatarLogo = 5;
constexpr int AvatarShirt = 6;
constexpr int AvatarName = 7;
constexpr int AvatarBack = 8;
constexpr int AvatarRowCount = 9;
const char *const AvatarLabels[AvatarRowCount] = {"Hair style", "Hair color", "Hat",        "Hat color", "Jewelry",
                                                  "Shirt logo", "Shirt color", "Name", "Back"};
const char *const HairNames[HairStyleCount] = {"Bald", "Short", "Long", "Mohawk"};
const char *const HatNames[HatStyleCount] = {"None", "Cap", "Beanie", "Top hat"};
const char *const JewelNames[JewelStyleCount] = {"None", "Chain", "Earring", "Both"};
const char *const LogoNames[LogoStyleCount] = {"Plain", "Stripes", "Ring", "Cross", "Badge"};

struct AvatarMenu {
    int sel = 0;
    bool editingName = false;
};

Rectangle avatarRowRect(int i) {
    return Rectangle{GetScreenWidth() / 2.0f + 40.0f, 170.0f + static_cast<float>(i) * 58.0f, 380.0f, 46.0f};
}

int cycleIndex(int value, int dir, int count) {
    return (value + dir + count) % count;
}

void adjustAvatar(int row, int dir) {
    Look &l = gPlayerLook;
    switch (row) {
    case AvatarHair:
        l.hair = cycleIndex(l.hair, dir, HairStyleCount);
        break;
    case AvatarHairColor:
        l.hairColor = cycleIndex(l.hairColor, dir, static_cast<int>(std::size(HairColors)));
        break;
    case AvatarHat:
        l.hat = cycleIndex(l.hat, dir, HatStyleCount);
        break;
    case AvatarHatColor:
        l.hatColor = cycleIndex(l.hatColor, dir, static_cast<int>(std::size(HatColors)));
        break;
    case AvatarJewel:
        l.jewel = cycleIndex(l.jewel, dir, JewelStyleCount);
        break;
    case AvatarLogo:
        l.logo = cycleIndex(l.logo, dir, LogoStyleCount);
        break;
    case AvatarShirt:
        l.shirt = cycleIndex(l.shirt, dir, ShirtColorCount);
        break;
    default:
        break;
    }
}

// Returns true when leaving the editor.
bool updateAvatar(AvatarMenu &am) {
    if (am.editingName) {
        for (int c = GetCharPressed(); c != 0; c = GetCharPressed()) {
            if (c < 128 && gPlayerName.size() < MaxNameLen && (std::isalnum(c) || c == '_' || c == ' ')) {
                gPlayerName.push_back(static_cast<char>(c));
            }
        }
        if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) {
            if (!gPlayerName.empty()) {
                gPlayerName.pop_back();
            }
        }
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_ESCAPE)) {
            am.editingName = false;
            if (gPlayerName.empty()) {
                gPlayerName = randomPlayerName();
            }
        }
        return false;
    }
    if (IsKeyPressed(KEY_ESCAPE)) {
        return true;
    }
    if (IsKeyPressed(KEY_W) || IsKeyPressed(KEY_UP)) {
        am.sel = (am.sel + AvatarRowCount - 1) % AvatarRowCount;
    }
    if (IsKeyPressed(KEY_S) || IsKeyPressed(KEY_DOWN)) {
        am.sel = (am.sel + 1) % AvatarRowCount;
    }
    if (IsKeyPressed(KEY_A) || IsKeyPressed(KEY_LEFT)) {
        adjustAvatar(am.sel, -1);
    }
    if (IsKeyPressed(KEY_D) || IsKeyPressed(KEY_RIGHT)) {
        adjustAvatar(am.sel, 1);
    }
    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE)) {
        if (am.sel == AvatarBack) {
            return true;
        }
        if (am.sel == AvatarName) {
            am.editingName = true;
        } else {
            adjustAvatar(am.sel, 1);
        }
    }
    for (int i = 0; i < AvatarRowCount; ++i) {
        if (!CheckCollisionPointRec(GetMousePosition(), avatarRowRect(i))) {
            continue;
        }
        if (Vector2Length(GetMouseDelta()) > 0.0f) {
            am.sel = i;
        }
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            if (i == AvatarBack) {
                return true;
            }
            if (i == AvatarName) {
                am.editingName = true;
            } else {
                adjustAvatar(i, 1);
            }
        }
    }
    return false;
}

void drawAvatar(const AvatarMenu &am) {
    const int W = GetScreenWidth();
    const char *title = "Avatar";
    DrawText(title, W / 2 - MeasureText(title, 60) / 2, 60, 60, RAYWHITE);
    drawLookPortrait(gPlayerLook, ShirtColors[gPlayerLook.shirt], Rectangle{W / 2.0f - 360.0f, 170.0f, 280.0f, 280.0f},
                     false);
    for (int i = 0; i < AvatarRowCount; ++i) {
        const Rectangle r = avatarRowRect(i);
        DrawRectangleRec(r, i == am.sel ? Color{60, 64, 84, 255} : Color{40, 42, 56, 255});
        if (i == am.sel) {
            DrawRectangleLinesEx(r, 3.0f, Color{200, 70, 50, 255});
        }
        if (i == AvatarBack) {
            DrawText(AvatarLabels[i], W / 2 + 40 + 190 - MeasureText(AvatarLabels[i], 24) / 2,
                     static_cast<int>(r.y) + 11, 24, RAYWHITE);
            continue;
        }
        DrawText(AvatarLabels[i], static_cast<int>(r.x) + 16, static_cast<int>(r.y) + 11, 24, RAYWHITE);
        const Look &l = gPlayerLook;
        const Color *swatch = nullptr;
        const char *val = nullptr;
        std::string nameText;
        switch (i) {
        case AvatarHairColor:
            swatch = &HairColors[l.hairColor];
            break;
        case AvatarHatColor:
            swatch = &HatColors[l.hatColor];
            break;
        case AvatarShirt:
            swatch = &ShirtColors[l.shirt];
            break;
        case AvatarHair:
            val = HairNames[l.hair];
            break;
        case AvatarHat:
            val = HatNames[l.hat];
            break;
        case AvatarJewel:
            val = JewelNames[l.jewel];
            break;
        case AvatarName:
            nameText = gPlayerName + (am.editingName ? "_" : "");
            val = nameText.c_str();
            break;
        case AvatarLogo:
            val = LogoNames[l.logo];
            break;
        default:
            break;
        }
        if (swatch) {
            DrawRectangleRec(Rectangle{r.x + r.width - 76.0f, r.y + 8.0f, 60.0f, r.height - 16.0f}, *swatch);
        } else if (val) {
            DrawText(val, static_cast<int>(r.x + r.width) - 16 - MeasureText(val, 24), static_cast<int>(r.y) + 11, 24,
                     YELLOW);
        }
    }
    const int H = GetScreenHeight();
    const char *hint = "W/S: select   A/D or click: change   Esc: back (saved)";
    DrawText(hint, W / 2 - MeasureText(hint, 18) / 2, H - 30, 18, GRAY);
}

// Cases: buy boxes and keys with Overcoins, open a box with a key, and equip owned skins.
Rectangle casesButtonRect() {
    return Rectangle{GetScreenWidth() - 190.0f, 116.0f, 170.0f, 40.0f};
}

bool casesButtonPressed() {
    return IsKeyPressed(KEY_B) ||
           (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(GetMousePosition(), casesButtonRect()));
}

void drawCasesButton() {
    const Rectangle r = casesButtonRect();
    const bool hot = CheckCollisionPointRec(GetMousePosition(), r);
    DrawRectangleRec(r, hot ? Color{70, 74, 96, 255} : Color{40, 42, 56, 255});
    DrawRectangleLinesEx(r, 2.0f, RAYWHITE);
    const char *label = "Cases (B)";
    DrawText(label, static_cast<int>(r.x + r.width / 2) - MeasureText(label, 22) / 2, static_cast<int>(r.y) + 9, 22,
             RAYWHITE);
}

constexpr float CasesRowY = 120.0f;
constexpr float CasesRowH = 64.0f;
constexpr float EquipListY = 490.0f;
constexpr float ListRowH = 20.0f;
constexpr int ListVisible = 8;
const Color WarnColor{255, 170, 60, 255};

Rectangle caseRowRect(int c) {
    return Rectangle{GetScreenWidth() / 2.0f - 500.0f, CasesRowY + c * CasesRowH, 1000.0f, 56.0f};
}

// k: 0 = buy box, 1 = buy key, 2 = open
Rectangle caseButtonRect(int c, int k) {
    return Rectangle{GetScreenWidth() / 2.0f + 100.0f + k * 170.0f, CasesRowY + c * CasesRowH + 10.0f, 160.0f, 36.0f};
}

Rectangle equipLeftRect() {
    return Rectangle{GetScreenWidth() / 2.0f - 500.0f, 446.0f, 36.0f, 30.0f};
}

Rectangle equipRightRect() {
    return Rectangle{GetScreenWidth() / 2.0f - 150.0f, 446.0f, 36.0f, 30.0f};
}

Rectangle gunListRect() {
    return Rectangle{GetScreenWidth() / 2.0f - 500.0f, EquipListY, 480.0f, ListRowH * ListVisible};
}

Rectangle knifeListRect() {
    return Rectangle{GetScreenWidth() / 2.0f + 20.0f, EquipListY, 480.0f, ListRowH * ListVisible};
}

Rectangle casesBackRect() {
    return Rectangle{GetScreenWidth() / 2.0f - 100.0f, GetScreenHeight() - 50.0f, 200.0f, 38.0f};
}

// Case reel: a strip of skins that slides to the winner, one click per card.
constexpr int SpinStripLen = 60;
constexpr int SpinWinPos = 50;
constexpr float SpinCardW = 150.0f;
constexpr float SpinCardGap = 8.0f;

struct Spin {
    bool active = false;
    float t = 0.0f;       // seconds since the reel started
    float duration = 5.0f;
    float target = 0.0f;  // final strip position, in cards
    float pos = 0.0f;     // current strip position, in cards
    int lastCell = 0;
    std::vector<const Skin *> strip;
};

struct CasesMenu {
    int equipSel = 0;  // index into equipWeapons()
    int gunTop = 0;
    int knifeTop = 0;
    std::string result;
    Color resultColor = RAYWHITE;
    Spin spin;
};

// Weapons that take a gun skin; the knife and taser have their own looks.
std::vector<int> equipWeapons() {
    std::vector<int> out;
    for (int w = 0; w < WeaponCount; ++w) {
        if (w != WeaponKnife && w != WeaponTaser) {
            out.push_back(w);
        }
    }
    return out;
}

// First row is the default look (-1), then every owned skin index.
std::vector<int> ownedRows(const bool *owned, int count) {
    std::vector<int> rows{-1};
    for (int i = 0; i < count; ++i) {
        if (owned[i]) {
            rows.push_back(i);
        }
    }
    return rows;
}

int maxTop(size_t rows) {
    return std::max(0, static_cast<int>(rows) - ListVisible);
}

// Row index under the mouse in a list, or -1.
int listRowUnder(Rectangle area, int top, size_t rows) {
    const Vector2 m = GetMousePosition();
    if (!CheckCollisionPointRec(m, area)) {
        return -1;
    }
    const int i = top + static_cast<int>((m.y - area.y) / ListRowH);
    return (i >= 0 && i < static_cast<int>(rows)) ? i : -1;
}

void buyItem(CasesMenu &cm, int c, bool key) {
    const CaseDef &def = caseDefs()[c];
    const int price = key ? def.keyPrice : def.price;
    if (gInv.coins < price) {
        cm.result = "Not enough Overcoins";
        cm.resultColor = WarnColor;
        return;
    }
    gInv.coins -= price;
    if (key) {
        gInv.keys[c]++;
    } else {
        gInv.boxes[c]++;
    }
    cm.result = std::string("Bought ") + def.name + (key ? " key" : " box");
    cm.resultColor = RAYWHITE;
    persistInventory();
}

// Uses one box and one key; the drop goes into the inventory.
void openCase(CasesMenu &cm, int c) {
    const CaseDef &def = caseDefs()[c];
    if (gInv.boxes[c] <= 0 || gInv.keys[c] <= 0) {
        cm.result = "You need a box and a key";
        cm.resultColor = WarnColor;
        return;
    }
    gInv.boxes[c]--;
    gInv.keys[c]--;
    const Rarity rar = rarityFromRoll(def, GetRandomValue(0, 99));
    const bool knife = rar == Rarity::Knife;
    const std::vector<Skin> &pool = knife ? knifeSkins() : gunSkins();
    std::vector<int> matches;
    for (int i = 0; i < static_cast<int>(pool.size()); ++i) {
        if (pool[i].rarity == rar && !(knife && i == KnifeLightsaber)) {
            matches.push_back(i);
        }
    }
    const int idx = matches[GetRandomValue(0, static_cast<int>(matches.size()) - 1)];
    if (knife) {
        gInv.knife[idx] = true;
    } else {
        gInv.gun[idx] = true;
    }
    cm.result = std::string("Opened ") + def.name + ": " + pool[idx].name + " (" + rarityName(rar) + ")";
    cm.resultColor = rarityColor(rar);
    cm.spin = Spin{};
    cm.spin.active = true;
    cm.spin.target = SpinWinPos + GetRandomValue(-30, 30) / 100.0f;
    cm.spin.strip.resize(SpinStripLen);
    for (const Skin *&slot : cm.spin.strip) {
        slot = &pool[GetRandomValue(0, static_cast<int>(pool.size()) - 1)];
    }
    cm.spin.strip[SpinWinPos] = &pool[idx];
    persistInventory();
}

// Advances the case reel, clicking each time a card passes the marker.
void updateSpin(Spin &s, const Audio &a, float dt) {
    const bool skip = IsKeyPressed(KEY_ESCAPE) || IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    if (s.t < s.duration) {
        s.t = skip ? s.duration : s.t + dt;
    } else if (skip) {
        s.active = false;
        return;
    }
    const float p = std::min(1.0f, s.t / s.duration);
    const float ease = 1.0f - (1.0f - p) * (1.0f - p) * (1.0f - p);
    s.pos = s.target * ease;
    const int cell = static_cast<int>(s.pos);
    if (cell != s.lastCell) {
        s.lastCell = cell;
        if (a.ready) {
            PlaySound(a.tick);
        }
    }
}

// Returns true when leaving the cases screen.
bool updateCases(CasesMenu &cm, const Audio &a, float dt) {
    if (cm.spin.active) {
        updateSpin(cm.spin, a, dt);
        return false;
    }
    if (IsKeyPressed(KEY_ESCAPE)) {
        return true;
    }
    const std::vector<int> weapons = equipWeapons();
    const std::vector<int> gunRows = ownedRows(gInv.gun, GunSkinCount);
    const std::vector<int> knifeRows = ownedRows(gInv.knife, KnifeSkinCount);
    const float wheel = GetMouseWheelMove();
    if (wheel != 0.0f) {
        const int step = wheel > 0.0f ? -1 : 1;
        if (CheckCollisionPointRec(GetMousePosition(), gunListRect())) {
            cm.gunTop = std::clamp(cm.gunTop + step, 0, maxTop(gunRows.size()));
        }
        if (CheckCollisionPointRec(GetMousePosition(), knifeListRect())) {
            cm.knifeTop = std::clamp(cm.knifeTop + step, 0, maxTop(knifeRows.size()));
        }
    }
    if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        return false;
    }
    const Vector2 m = GetMousePosition();
    for (int c = 0; c < CaseCount; ++c) {
        if (CheckCollisionPointRec(m, caseButtonRect(c, 0))) {
            buyItem(cm, c, false);
            return false;
        }
        if (CheckCollisionPointRec(m, caseButtonRect(c, 1))) {
            buyItem(cm, c, true);
            return false;
        }
        if (CheckCollisionPointRec(m, caseButtonRect(c, 2))) {
            openCase(cm, c);
            return false;
        }
    }
    const int n = static_cast<int>(weapons.size());
    if (CheckCollisionPointRec(m, equipLeftRect())) {
        cm.equipSel = (cm.equipSel + n - 1) % n;
    }
    if (CheckCollisionPointRec(m, equipRightRect())) {
        cm.equipSel = (cm.equipSel + 1) % n;
    }
    const int gi = listRowUnder(gunListRect(), cm.gunTop, gunRows.size());
    if (gi >= 0) {
        gInv.equipGun[weapons[cm.equipSel]] = gunRows[gi];
        persistInventory();
    }
    const int ki = listRowUnder(knifeListRect(), cm.knifeTop, knifeRows.size());
    if (ki >= 0) {
        gInv.equipKnife = knifeRows[ki];
        persistInventory();
    }
    return CheckCollisionPointRec(m, casesBackRect());
}

void drawButton(Rectangle r, const char *label, bool enabled) {
    const bool hot = enabled && CheckCollisionPointRec(GetMousePosition(), r);
    DrawRectangleRec(r, hot ? Color{70, 74, 96, 255} : Color{40, 42, 56, 255});
    DrawRectangleLinesEx(r, 2.0f, enabled ? RAYWHITE : GRAY);
    DrawText(label, static_cast<int>(r.x + r.width / 2) - MeasureText(label, 20) / 2, static_cast<int>(r.y) + 8, 20,
             enabled ? RAYWHITE : GRAY);
}

// Lobby: connect to the lobby server, list public lobbies, create or join lobbies.
enum LobbyField { LobbyAddress, LobbyCreateName, LobbyCreatePass, LobbyJoinId, LobbyJoinPass, LobbyFieldCount };
enum LobbyButton { LBConnect, LBRefresh, LBPaste, LBVisibility, LBMode, LBCreate, LBJoin, LBLeave, LBStart, LBMap, LBPlayBots, LBDiscord, LBBack, LBFindIp };
constexpr int LobbyListVisible = 12;
constexpr int LobbyMaxPlayers = 12;
constexpr int LobbyMinStart = 2;
constexpr const char *DiscordInvite = "https://discord.gg/4ce3aJME3y";

constexpr float VoteSeconds = 20.0f;

struct LobbyMenu {
    LobbyConnection conn;
    std::string fields[LobbyFieldCount] = {"127.0.0.1:7777", "MyLobby", "", "", ""};
    bool createPrivate = false;
    bool createTeams = false;
    std::vector<LobbyInfo> lobbies;
    bool listing = false;
    int joinedId = -1;
    std::string joinedMode;
    int map = 0;
    std::string pendingMode;
    bool pendingCreate = false;
    bool pendingPrivate = false;
    bool joinedPrivate = false;
    int pendingPlayers = 1;
    bool isHost = false;
    bool started = false;
    bool matchReady = false;
    bool voting = false;
    MenuState voteMenu;
    std::map<int, int> votes; // fd -> map index
    float voteTimeLeft = 0.0f;
    bool voteFinishSent = false;
    int players = 0;
    int pingMs = -1;
    std::string status = "Enter the server address and press Connect";
    int editing = -1;
    int myFd = 0;                // our lobby id, from YOU
    int myTeam = -1;             // our team index from TEAM (-1 = deathmatch)
    bool outdated = false;       // server rejected our game version
};

Rectangle lobbyFieldRect(int f) {
    switch (f) {
    case LobbyAddress: return Rectangle{40.0f, 120.0f, 360.0f, 40.0f};
    case LobbyCreateName: return Rectangle{680.0f, 220.0f, 360.0f, 40.0f};
    case LobbyCreatePass: return Rectangle{680.0f, 320.0f, 360.0f, 40.0f};
    case LobbyJoinId: return Rectangle{680.0f, 460.0f, 170.0f, 40.0f};
    case LobbyJoinPass: return Rectangle{870.0f, 460.0f, 170.0f, 40.0f};
    default: return Rectangle{0.0f, 0.0f, 0.0f, 0.0f};
    }
}

Rectangle lobbyButtonBounds(int b) {
    switch (b) {
    case LBConnect: return Rectangle{410.0f, 120.0f, 120.0f, 40.0f};
    case LBRefresh: return Rectangle{540.0f, 120.0f, 120.0f, 40.0f};
    case LBPaste: return Rectangle{670.0f, 120.0f, 170.0f, 40.0f};
    case LBPlayBots: return Rectangle{850.0f, 120.0f, 170.0f, 40.0f};
    case LBDiscord: return Rectangle{1040.0f, 120.0f, 170.0f, 40.0f};
    case LBMap: return Rectangle{1040.0f, 570.0f, 170.0f, 40.0f};
    case LBVisibility: return Rectangle{680.0f, 270.0f, 170.0f, 40.0f};
    case LBMode: return Rectangle{870.0f, 270.0f, 170.0f, 40.0f};
    case LBCreate: return Rectangle{680.0f, 370.0f, 360.0f, 40.0f};
    case LBJoin: return Rectangle{680.0f, 510.0f, 360.0f, 40.0f};
    case LBLeave: return Rectangle{680.0f, 570.0f, 170.0f, 40.0f};
    case LBStart: return Rectangle{870.0f, 570.0f, 170.0f, 40.0f};
    case LBFindIp: return Rectangle{220.0f, 30.0f, 170.0f, 44.0f};
    default: return Rectangle{40.0f, 650.0f, 160.0f, 44.0f};
    }
}

Rectangle lobbyRowRect(int i) {
    return Rectangle{40.0f, 220.0f + i * 36.0f, 600.0f, 32.0f};
}

Rectangle lobbyMenuButtonRect() {
    return Rectangle{GetScreenWidth() - 190.0f, 164.0f, 170.0f, 40.0f};
}

bool lobbyMenuButtonPressed() {
    return IsKeyPressed(KEY_L) ||
           (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(GetMousePosition(), lobbyMenuButtonRect()));
}

void drawLobbyMenuButton() {
    const Rectangle r = lobbyMenuButtonRect();
    const bool hot = CheckCollisionPointRec(GetMousePosition(), r);
    DrawRectangleRec(r, hot ? Color{70, 74, 96, 255} : Color{40, 42, 56, 255});
    DrawRectangleLinesEx(r, 2.0f, RAYWHITE);
    const char *label = "Lobby (L)";
    DrawText(label, static_cast<int>(r.x + r.width / 2) - MeasureText(label, 22) / 2, static_cast<int>(r.y) + 9, 22,
             RAYWHITE);
}

std::string lobbyErrorText(const std::string &code) {
    if (code == "no_lobby") return "No lobby with that id";
    if (code == "already_in_lobby") return "You are already in a lobby";
    if (code == "wrong_password") return "Wrong password";
    if (code == "bad_create") return "Could not create the lobby (check the name and password)";
    if (code == "not_in_lobby") return "You are not in a lobby";
    if (code == "lobby_full") return "That lobby is full (12 players)";
    if (code == "already_started") return "That lobby has already started";
    if (code == "not_host") return "Only the host can start the lobby";
    if (code == "not_enough_players") return "Need at least 2 players to start";
    if (code == "teams_need_even") return "Team mode needs an even number of players";
    if (code == "version_mismatch" || code == "version_required") return "Upgrade your game please";
    return "Server error: " + code;
}

void lobbyCloseConnection(LobbyMenu &lm) {
    lm.conn.close();
    lm.joinedId = -1;
    lm.joinedMode.clear();
    lm.listing = false;
    lm.isHost = false;
    lm.started = false;
    lm.voting = false;
    lm.players = 0;
    lm.pingMs = -1;
}

void lobbyRefresh(LobbyMenu &lm) {
    if (!lm.conn.isOpen()) {
        lm.status = "Connect to a server first";
        return;
    }
    lm.lobbies.clear();
    lm.listing = true;
    lm.conn.sendLine("LIST");
}

void lobbyConnect(LobbyMenu &lm) {
    std::string err;
    const double start = GetTime();
    if (!lm.conn.open(lm.fields[LobbyAddress], err)) {
        lm.status = err + " - check the address and press Connect";
        return;
    }
    lm.outdated = false;
    lm.conn.sendLine(std::string("HELLO ") + GameVersion);
    // Connect time is used as the ping estimate.
    lm.pingMs = static_cast<int>((GetTime() - start) * 1000.0);
    gServerAddress = lm.fields[LobbyAddress];
    saveSettings();
    lm.joinedId = -1;
    lm.joinedMode.clear();
    lm.status = "Connected (" + std::to_string(lm.pingMs) + " ms)";
    lobbyRefresh(lm);
}

void lobbyCreate(LobbyMenu &lm) {
    if (!lm.conn.isOpen()) {
        lm.status = "Connect to a server first";
        return;
    }
    const std::string &name = lm.fields[LobbyCreateName];
    const std::string &pass = lm.fields[LobbyCreatePass];
    if (name.empty()) {
        lm.status = "Enter a lobby name";
        return;
    }
    if (lm.createPrivate && pass.empty()) {
        lm.status = "Private lobbies need a password";
        return;
    }
    lm.pendingMode = lm.createTeams ? "teams" : "deathmatch";
    lm.pendingCreate = true;
    lm.pendingPrivate = lm.createPrivate;
    lm.pendingPlayers = 1;
    lm.conn.sendLine("CREATE " + name + (lm.createPrivate ? " private " : " public ") +
                     (lm.createPrivate ? pass : std::string("-")) + " " + lm.pendingMode);
}

void lobbyJoin(LobbyMenu &lm, const std::string &id, const std::string &pass) {
    if (!lm.conn.isOpen()) {
        lm.status = "Connect to a server first";
        return;
    }
    if (id.empty()) {
        lm.status = "Enter a lobby id";
        return;
    }
    lm.pendingMode.clear();
    lm.pendingCreate = false;
    lm.pendingPlayers = 1;
    bool listed = false;
    for (const LobbyInfo &info : lm.lobbies) {
        if (std::to_string(info.id) == id) {
            lm.pendingMode = info.mode;
            lm.pendingPlayers = info.players + 1;
            listed = true;
        }
    }
    // Unlisted lobbies that take a password are private.
    lm.pendingPrivate = !listed && !pass.empty();
    lm.conn.sendLine("JOIN " + id + " " + (pass.empty() ? std::string("-") : pass));
}

void lobbyHandleLine(LobbyMenu &lm, const std::string &line) {
    std::istringstream ss(line);
    std::string cmd;
    ss >> cmd;
    if (cmd == "LOBBY") {
        LobbyInfo info;
        ss >> info.id >> info.name >> info.mode >> info.players;
        if (!ss.fail()) {
            lm.lobbies.push_back(info);
        }
    } else if (cmd == "END") {
        lm.listing = false;
        lm.status = std::to_string(lm.lobbies.size()) + " public lobbies";
    } else if (cmd == "OK") {
        int id = -1;
        if (ss >> id) {
            lm.joinedId = id;
            lm.joinedPrivate = lm.pendingPrivate;
            lm.joinedMode = lm.pendingMode;
            lm.isHost = lm.pendingCreate;
            lm.players = lm.pendingPlayers;
            lm.started = false;
            lm.map = 0;
            lm.status = "In lobby #" + std::to_string(id) +
                        (lm.joinedMode.empty() ? std::string() : " (" + lm.joinedMode + ")");
        } else {
            lm.joinedId = -1;
            lm.joinedPrivate = false;
            lm.joinedMode.clear();
            lm.isHost = false;
            lm.started = false;
            lm.players = 0;
            lm.status = "Left the lobby";
        }
        lm.pendingMode.clear();
        lm.pendingCreate = false;
    } else if (cmd == "ERR") {
        std::string code;
        ss >> code;
        lm.status = lobbyErrorText(code);
        lm.outdated = code == "version_mismatch" || code == "version_required";
        lm.pendingMode.clear();
        lm.pendingCreate = false;
    } else if (cmd == "PLAYER_JOINED") {
        ++lm.players;
        lm.status = "A player joined your lobby";
    } else if (cmd == "PLAYER_LEFT") {
        if (lm.players > 1) {
            --lm.players;
        }
        lm.status = "A player left your lobby";
    } else if (cmd == "MAP") {
        int map = -1;
        ss >> map;
        if (map >= 0 && map < MapCount) {
            lm.map = map;
        }
    } else if (cmd == "YOU") {
        ss >> lm.myFd;
    } else if (cmd == "TEAM") {
        ss >> lm.myTeam;
    } else if (cmd == "VOTE_START") {
        lm.started = true;
        lm.voting = true;
        lm.votes.clear();
        lm.voteMenu = MenuState{};
        for (bool &d : lm.voteMenu.dirty) {
            d = true;
        }
        lm.voteTimeLeft = VoteSeconds;
        lm.voteFinishSent = false;
        lm.status = "Vote for a map";
    } else if (cmd == "VOTED") {
        int fd = 0;
        int map = -1;
        ss >> fd >> map;
        if (map >= 0 && map < MapCount) {
            lm.votes[fd] = map;
        }
    } else if (cmd == "MAP_RESULT") {
        int map = -1;
        ss >> map;
        if (map >= 0 && map < MapCount) {
            lm.map = map;
        }
        lm.voting = false;
        lm.matchReady = true;
    }
}

void updateVote(LobbyMenu &lm, float dt) {
    const int pick = updateMenu(lm.voteMenu, dt);
    if (pick >= 0 && lm.conn.isOpen()) {
        lm.conn.sendLine("VOTE " + std::to_string(pick));
    }
    lm.voteTimeLeft -= dt;
    if (lm.isHost && lm.voteTimeLeft <= 0.0f && !lm.voteFinishSent && lm.conn.isOpen()) {
        lm.conn.sendLine("FINISH_VOTE");
        lm.voteFinishSent = true;
    }
}

// Applies the PlayFab server address once the background lookup has finished.
void applyServerLookup(LobbyMenu &lm) {
    if (!gServerLookup.valid() ||
        gServerLookup.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        return;
    }
    const ServerLookup found = gServerLookup.get();
    if (!found.ok) {
        lm.status = "PlayFab lookup failed: " + found.error;
        return;
    }
    gServerAddress = found.address;
    lm.fields[LobbyAddress] = found.address;
    saveSettings();
    lm.status = "Server address loaded from PlayFab";
}

void startServerLookup(LobbyMenu &lm) {
    if (gServerLookup.valid() && gServerLookup.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        lm.status = "Already searching PlayFab...";
        return;
    }
    gServerLookup = std::async(std::launch::async, lookupServerAddress, gPlayerCustomId);
    lm.status = "Searching PlayFab for the server address...";
}

// Returns true when the player leaves the lobby screen.
bool updateLobby(LobbyMenu &lm) {
    applyServerLookup(lm);
    const bool wasOpen = lm.conn.isOpen();
    for (const std::string &line : lm.conn.poll()) {
        lobbyHandleLine(lm, line);
    }
    if (wasOpen && !lm.conn.isOpen()) {
        lm.joinedId = -1;
        lm.joinedMode.clear();
        lm.listing = false;
        lm.status = lm.outdated ? "Upgrade your game please" : "Disconnected from the server";
    }

    bool escUsed = false;
    if (lm.editing >= 0) {
        std::string &text = lm.fields[lm.editing];
        const size_t maxLen = lm.editing == LobbyAddress ? 64 : 16;
        for (int ch = GetCharPressed(); ch != 0; ch = GetCharPressed()) {
            const bool ok = ch < 128 && (std::isalnum(ch) || ch == '_' || ch == '-' ||
                                         (lm.editing == LobbyAddress && (ch == '.' || ch == ':')));
            if (ok && text.size() < maxLen) {
                text.push_back(static_cast<char>(ch));
            }
        }
        if ((IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) && !text.empty()) {
            text.pop_back();
        }
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_TAB)) {
            lm.editing = -1;
        }
        if (IsKeyPressed(KEY_ESCAPE)) {
            lm.editing = -1;
            escUsed = true;
        }
    }

    const bool clicked = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    const Vector2 mouse = GetMousePosition();
    if (lm.voting) {
        return false;
    }
    auto hit = [&](Rectangle r) { return clicked && CheckCollisionPointRec(mouse, r); };
    if (clicked) {
        lm.editing = -1;
        for (int f = 0; f < LobbyFieldCount; ++f) {
            if (hit(lobbyFieldRect(f))) {
                lm.editing = f;
            }
        }
    }

    if (hit(lobbyButtonBounds(LBBack)) || (!escUsed && IsKeyPressed(KEY_ESCAPE))) {
        lobbyCloseConnection(lm);
        return true;
    }
    if (hit(lobbyButtonBounds(LBConnect))) {
        lobbyConnect(lm);
    }
    if (hit(lobbyButtonBounds(LBRefresh))) {
        lobbyRefresh(lm);
    }
    if (hit(lobbyButtonBounds(LBPaste))) {
        if (const char *clip = GetClipboardText()) {
            std::string text = clip;
            while (!text.empty() && (text.back() == ' ' || text.back() == '\n' || text.back() == '\r' || text.back() == '\t')) {
                text.pop_back();
            }
            const size_t start = text.find_first_not_of(" \t\r\n");
            lm.fields[LobbyAddress] = start == std::string::npos ? "" : text.substr(start);
        }
    }
    if (hit(lobbyButtonBounds(LBFindIp))) {
        startServerLookup(lm);
    }
    if (hit(lobbyButtonBounds(LBDiscord))) {
        SetClipboardText(DiscordInvite);
        lm.status = "Discord link pasted to your clipboard";
    }
    if (hit(lobbyButtonBounds(LBVisibility))) {
        lm.createPrivate = !lm.createPrivate;
    }
    if (hit(lobbyButtonBounds(LBMode))) {
        lm.createTeams = !lm.createTeams;
    }
    if (hit(lobbyButtonBounds(LBCreate))) {
        lobbyCreate(lm);
    }
    if (hit(lobbyButtonBounds(LBJoin))) {
        lobbyJoin(lm, lm.fields[LobbyJoinId], lm.fields[LobbyJoinPass]);
    }
    if (hit(lobbyButtonBounds(LBLeave)) && lm.joinedId >= 0 && lm.conn.isOpen()) {
        lm.conn.sendLine("LEAVE");
    }
    if (hit(lobbyButtonBounds(LBStart)) && lm.isHost && lm.joinedId >= 0 && !lm.started) {
        lm.conn.sendLine("START");
    }
    if (hit(lobbyButtonBounds(LBMap)) && !lm.started && (lm.joinedId < 0 || lm.isHost)) {
        const int next = (lm.map + 1) % MapCount;
        if (lm.joinedId >= 0) {
            lm.conn.sendLine("MAP " + std::to_string(next));
        } else {
            lm.map = next;
        }
    }
    if (hit(lobbyButtonBounds(LBPlayBots))) {
        lm.matchReady = true;
    }
    for (int i = 0; i < static_cast<int>(lm.lobbies.size()) && i < LobbyListVisible; ++i) {
        if (hit(lobbyRowRect(i))) {
            lobbyJoin(lm, std::to_string(lm.lobbies[i].id), "");
        }
    }
    return false;
}

void drawLobbyField(const LobbyMenu &lm, int f, const char *label, bool secret) {
    const Rectangle r = lobbyFieldRect(f);
    const bool active = lm.editing == f;
    DrawText(label, static_cast<int>(r.x), static_cast<int>(r.y) - 22, 18, GRAY);
    DrawRectangleRec(r, Color{30, 32, 46, 255});
    DrawRectangleLinesEx(r, 2.0f, active ? YELLOW : RAYWHITE);
    std::string text = secret ? std::string(lm.fields[f].size(), '*') : lm.fields[f];
    if (active) {
        text += "_";
    }
    DrawText(text.c_str(), static_cast<int>(r.x) + 10, static_cast<int>(r.y) + 9, 20, RAYWHITE);
}

void drawLobby(const LobbyMenu &lm) {
    DrawText("Lobby", 40, 30, 40, RAYWHITE);

    drawLobbyField(lm, LobbyAddress, "Server address (host:port)", false);
    drawButton(lobbyButtonBounds(LBConnect), "Connect", true);
    drawButton(lobbyButtonBounds(LBRefresh), "Refresh", lm.conn.isOpen());
    drawButton(lobbyButtonBounds(LBPaste), "Paste", true);
    drawButton(lobbyButtonBounds(LBFindIp), "Find IP", true);

    DrawText("Public lobbies", 40, 190, 22, RAYWHITE);
    const int count = static_cast<int>(lm.lobbies.size());
    if (!lm.conn.isOpen()) {
        DrawText("Connect to see public lobbies", 40, 224, 20, GRAY);
    } else if (count == 0) {
        DrawText(lm.listing ? "Loading..." : "No public lobbies right now", 40, 224, 20, GRAY);
    }
    for (int i = 0; i < count && i < LobbyListVisible; ++i) {
        const LobbyInfo &info = lm.lobbies[static_cast<size_t>(i)];
        const Rectangle r = lobbyRowRect(i);
        const bool hot = CheckCollisionPointRec(GetMousePosition(), r);
        DrawRectangleRec(r, hot ? Color{70, 74, 96, 255} : Color{40, 42, 56, 255});
        char text[160];
        std::snprintf(text, sizeof(text), "#%d  %s  %s  %d player(s)", info.id, info.name.c_str(),
                      info.mode.c_str(), info.players);
        DrawText(text, static_cast<int>(r.x) + 10, static_cast<int>(r.y) + 6, 20, RAYWHITE);
    }

    DrawText("Create lobby", 680, 160, 22, RAYWHITE);
    drawLobbyField(lm, LobbyCreateName, "Lobby name", false);
    drawButton(lobbyButtonBounds(LBVisibility), lm.createPrivate ? "Private" : "Public", true);
    drawButton(lobbyButtonBounds(LBMode), lm.createTeams ? "Teams" : "Deathmatch", true);
    drawLobbyField(lm, LobbyCreatePass, "Password (private only)", true);
    drawButton(lobbyButtonBounds(LBCreate), "Create lobby", lm.conn.isOpen());

    drawLobbyField(lm, LobbyJoinId, "Join by lobby id", false);
    drawLobbyField(lm, LobbyJoinPass, "Password", true);
    drawButton(lobbyButtonBounds(LBJoin), "Join", lm.conn.isOpen());
    drawButton(lobbyButtonBounds(LBLeave), "Leave lobby", lm.joinedId >= 0);
    drawButton(lobbyButtonBounds(LBStart), lm.started ? "Started" : "Start",
               lm.isHost && lm.joinedId >= 0 && !lm.started);
    const std::string mapLabel = std::string("Map: ") + Maps[lm.map].name;
    drawButton(lobbyButtonBounds(LBMap), mapLabel.c_str(), !lm.started && (lm.joinedId < 0 || lm.isHost));
    drawButton(lobbyButtonBounds(LBPlayBots), "Play with bots", true);
    drawButton(lobbyButtonBounds(LBDiscord), "Discord", true);
    if (lm.joinedId >= 0) {
        const std::string info = "Players: " + std::to_string(lm.players) + "/" + std::to_string(LobbyMaxPlayers) +
                                 "   Min to start: " + std::to_string(LobbyMinStart);
        DrawText(info.c_str(), 680, 620, 20, RAYWHITE);
    }

    drawButton(lobbyButtonBounds(LBBack), "Back", true);
    DrawText(lm.status.c_str(), 230, 662, 20, YELLOW);
}

void drawSkinList(Rectangle area, int top, const std::vector<int> &rows, const std::vector<Skin> &pool,
                  int equipped) {
    const int count = static_cast<int>(rows.size());
    for (int i = 0; i < ListVisible && top + i < count; ++i) {
        const int skin = rows[top + i];
        const Rectangle r{area.x, area.y + i * ListRowH, area.width, ListRowH - 2.0f};
        DrawRectangleRec(r, Color{40, 42, 56, 255});
        if (skin < 0) {
            DrawText("Default look", static_cast<int>(r.x) + 26, static_cast<int>(r.y) + 1, 16, LIGHTGRAY);
        } else {
            const Skin &s = pool[skin];
            DrawRectangle(static_cast<int>(r.x) + 5, static_cast<int>(r.y) + 3, 12, 12, s.color);
            DrawText(s.name.c_str(), static_cast<int>(r.x) + 26, static_cast<int>(r.y) + 1, 16, RAYWHITE);
            const char *rn = rarityName(s.rarity);
            DrawText(rn, static_cast<int>(r.x + r.width) - 8 - MeasureText(rn, 16), static_cast<int>(r.y) + 1, 16,
                     rarityColor(s.rarity));
        }
        if (skin == equipped) {
            DrawRectangleLinesEx(r, 2.0f, YELLOW);
        }
    }
}

void drawSpin(const Spin &s) {
    const int W = GetScreenWidth();
    const int H = GetScreenHeight();
    DrawRectangle(0, 0, W, H, Color{0, 0, 0, 180});
    const float cx = W / 2.0f;
    const float cy = H / 2.0f - 30.0f;
    const float step = SpinCardW + SpinCardGap;
    DrawRectangleRec(Rectangle{0.0f, cy - 80.0f, static_cast<float>(W), 160.0f}, Color{30, 32, 44, 255});
    BeginScissorMode(0, static_cast<int>(cy - 80.0f), W, 160);
    for (int i = 0; i < static_cast<int>(s.strip.size()); ++i) {
        const float x = cx + (i - s.pos) * step - SpinCardW / 2.0f;
        if (x + SpinCardW < 0.0f || x > W) {
            continue;
        }
        const Skin &k = *s.strip[i];
        DrawRectangle(static_cast<int>(x), static_cast<int>(cy - 60.0f), static_cast<int>(SpinCardW), 120,
                      Color{40, 42, 56, 255});
        DrawRectangle(static_cast<int>(x) + 16, static_cast<int>(cy - 46.0f), static_cast<int>(SpinCardW) - 32, 56,
                      k.color);
        DrawText(k.name.c_str(), static_cast<int>(x) + 8, static_cast<int>(cy + 20.0f), 14, RAYWHITE);
        DrawRectangle(static_cast<int>(x), static_cast<int>(cy + 54.0f), static_cast<int>(SpinCardW), 6,
                      rarityColor(k.rarity));
    }
    EndScissorMode();
    DrawRectangle(static_cast<int>(cx) - 2, static_cast<int>(cy - 80.0f), 4, 160, YELLOW);

    const Skin &won = *s.strip[SpinWinPos];
    const bool done = s.t >= s.duration;
    if (done) {
        const std::string line = won.name + "  (" + rarityName(won.rarity) + ")";
        DrawText(line.c_str(), W / 2 - MeasureText(line.c_str(), 26) / 2, static_cast<int>(cy) + 110, 26,
                 rarityColor(won.rarity));
    }
    const char *hint = done ? "Click or Esc to continue" : "Click or Esc to skip";
    DrawText(hint, W / 2 - MeasureText(hint, 18) / 2, static_cast<int>(cy) + 150, 18, GRAY);
}

void drawCases(const CasesMenu &cm) {
    const int W = GetScreenWidth();
    const char *title = "Cases";
    DrawText(title, W / 2 - MeasureText(title, 48) / 2, 28, 48, RAYWHITE);
    DrawText(TextFormat("Overcoins: %d", gInv.coins), 24, 24, 26, YELLOW);
    const char *tip = "Click to buy, open and equip   Esc: back (saved)";
    DrawText(tip, W - 24 - MeasureText(tip, 18), 34, 18, GRAY);

    const auto &defs = caseDefs();
    for (int c = 0; c < CaseCount; ++c) {
        const Rectangle r = caseRowRect(c);
        DrawRectangleRec(r, Color{40, 42, 56, 255});
        DrawText(defs[c].name, static_cast<int>(r.x) + 16, static_cast<int>(r.y) + 6, 24, RAYWHITE);
        DrawText(TextFormat("Box: %d   Key: %d", gInv.boxes[c], gInv.keys[c]), static_cast<int>(r.x) + 16,
                 static_cast<int>(r.y) + 34, 18, LIGHTGRAY);
        const std::string boxLabel = "Box " + std::to_string(defs[c].price);
        const std::string keyLabel = "Key " + std::to_string(defs[c].keyPrice);
        drawButton(caseButtonRect(c, 0), boxLabel.c_str(), true);
        drawButton(caseButtonRect(c, 1), keyLabel.c_str(), true);
        drawButton(caseButtonRect(c, 2), "Open", gInv.boxes[c] > 0 && gInv.keys[c] > 0);
    }
    DrawText(cm.result.c_str(), W / 2 - MeasureText(cm.result.c_str(), 22) / 2, 384, 22, cm.resultColor);

    const std::vector<int> weapons = equipWeapons();
    const int w = weapons[cm.equipSel];
    DrawText("Gun skins", static_cast<int>(gunListRect().x), 418, 20, LIGHTGRAY);
    DrawText("Knives", static_cast<int>(knifeListRect().x), 418, 20, LIGHTGRAY);
    drawButton(equipLeftRect(), "<", true);
    drawButton(equipRightRect(), ">", true);
    DrawText(Weapons[w].name, static_cast<int>(equipLeftRect().x) + 46, 452, 22, YELLOW);

    drawSkinList(gunListRect(), cm.gunTop, ownedRows(gInv.gun, GunSkinCount), gunSkins(), gInv.equipGun[w]);
    drawSkinList(knifeListRect(), cm.knifeTop, ownedRows(gInv.knife, KnifeSkinCount), knifeSkins(), gInv.equipKnife);
    drawButton(casesBackRect(), "Back", true);
    if (cm.spin.active) {
        drawSpin(cm.spin);
    }
}

void drawMapCards(const MenuState &menu, const RenderTexture2D *previews) {
    for (int i = 0; i < MapCount; ++i) {
        if (!cardVisible(i, menu.scroll)) {
            continue;
        }
        const Rectangle r = menuCardRect(i, menu.scroll);
        DrawRectangleRec(r, Color{40, 42, 56, 255});
        const Texture2D &tex = previews[i].texture;
        DrawTexturePro(tex, Rectangle{0.0f, 0.0f, static_cast<float>(tex.width), -static_cast<float>(tex.height)},
                       Rectangle{r.x, r.y, CardW, PreviewH}, Vector2{0.0f, 0.0f}, 0.0f, WHITE);
        DrawText(Maps[i].name, static_cast<int>(r.x) + 8, static_cast<int>(r.y + PreviewH) + 6, 22, RAYWHITE);
        if (i == menu.sel) {
            DrawRectangleLinesEx(r, 3.0f, Color{200, 70, 50, 255});
        }
    }
}

void drawMenu(const MenuState &menu, const RenderTexture2D *previews, const Playlist &pl) {
    drawLogo();
    drawAvatarTile();
    drawMapCards(menu, previews);

    const int descY = static_cast<int>(GridTop + 2 * (CardH + CardGap)) + 8;
    DrawText(Maps[menu.sel].description, GetScreenWidth() / 2 - MeasureText(Maps[menu.sel].description, 22) / 2,
             descY, 22, LIGHTGRAY);

    const int H = GetScreenHeight();
    const char *hint = "WASD or mouse to choose   Wheel to scroll   Enter or click to play   Q to quit";
    DrawText(hint, GetScreenWidth() / 2 - MeasureText(hint, 18) / 2, H - 30, 18, GRAY);
    const char *music = "Left/Right: track   Up/Down: volume   M: mute";
    DrawText(music, GetScreenWidth() / 2 - MeasureText(music, 18) / 2, H - 52, 18, GRAY);
    drawMusicInfo(pl);
    drawSettingsButton();
    drawModsButton();
    drawCasesButton();
    drawLobbyMenuButton();
}

void drawVote(const LobbyMenu &lm, const RenderTexture2D *previews) {
    const int W = GetScreenWidth();
    const char *title = "Vote for a map";
    DrawText(title, W / 2 - MeasureText(title, 40) / 2, 28, 40, RAYWHITE);
    const int secondsLeft = static_cast<int>(std::ceil(std::max(0.0f, lm.voteTimeLeft)));
    const std::string timer = "Voting ends in " + std::to_string(secondsLeft) + " s";
    DrawText(timer.c_str(), W / 2 - MeasureText(timer.c_str(), 22) / 2, 80, 22, YELLOW);
    drawMapCards(lm.voteMenu, previews);
    for (int i = 0; i < MapCount; ++i) {
        if (!cardVisible(i, lm.voteMenu.scroll)) {
            continue;
        }
        int count = 0;
        for (const auto &v : lm.votes) {
            if (v.second == i) ++count;
        }
        const Rectangle r = menuCardRect(i, lm.voteMenu.scroll);
        const std::string label = std::to_string(count) + (count == 1 ? " vote" : " votes");
        DrawText(label.c_str(), static_cast<int>(r.x) + 8, static_cast<int>(r.y + PreviewH) + 30, 20, LIGHTGRAY);
    }
    const char *hint = "WASD, mouse or Enter to vote";
    DrawText(hint, W / 2 - MeasureText(hint, 18) / 2, GetScreenHeight() - 30, 18, GRAY);
}

} // namespace

Game *gModGame = nullptr;

void modExplodeAt(Vector3 pos) {
    if (gModGame) {
        explode(*gModGame, pos, nullptr);
    }
}

void modKillAllEnemies() {
    if (!gModGame) {
        return;
    }
    for (Enemy &e : gModGame->enemies) {
        if (e.hp > 0 && e.netId == 0 && hostile(gModGame->localTeam, e.team)) {
            e.hp = 0;
            gModGame->money += 100;
        }
    }
    reapEnemies(*gModGame);
}

void modAddScore(int points) {
    if (gModGame) {
        gModGame->money += points;
    }
}

int modGetCoins() {
    return gInv.coins;
}

void modSetCoins(int coins) {
    if (gInv.coins != coins) {
        gInv.coins = std::max(0, coins);
        persistInventory();
    }
}

// Unlocks every knife except the lightsaber, which only the lightsaber mod can grant.
void modUnlockKnives() {
    for (int i = 0; i < KnifeSkinCount; ++i) {
        if (i != KnifeLightsaber) {
            gInv.knife[i] = true;
        }
    }
    persistInventory();
}

int modKnifeOwned(int index) {
    return index >= 0 && index < KnifeSkinCount && gInv.knife[index] ? 1 : 0;
}

void modUnlockKnife(int index) {
    if (index >= 0 && index < KnifeSkinCount && !gInv.knife[index]) {
        gInv.knife[index] = true;
        persistInventory();
    }
}

void modHeal(float amount) {
    if (gModGame) {
        gModGame->health = std::min(PlayerMaxHealth, gModGame->health + amount);
    }
}

void modRefillAmmo() {
    if (gModGame) {
        for (int w = 0; w < WeaponCount; ++w) {
            if (gModGame->owned[w]) {
                gModGame->mags[w] = Weapons[w].mag;
            }
        }
        gModGame->reserve = std::max(gModGame->reserve, 999);
    }
}

struct ModsState {
    int sel = 0;
    int open = -1;
};

// Returns false when the player leaves the mods screen.
bool runMods(ModsState &ms, float dt) {
    const std::vector<ModPage> &pages = modPages();
    const int count = static_cast<int>(pages.size());
    if (ms.open >= 0) {
        if (IsKeyPressed(KEY_ESCAPE)) {
            ms.open = -1;
        }
    } else {
        if (IsKeyPressed(KEY_ESCAPE)) {
            return false;
        }
        if (count > 0 && (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S))) {
            ms.sel = (ms.sel + 1) % count;
        }
        if (count > 0 && (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W))) {
            ms.sel = (ms.sel + count - 1) % count;
        }
        if (count > 0 && IsKeyPressed(KEY_ENTER)) {
            ms.open = ms.sel;
        }
    }

    BeginDrawing();
    ClearBackground(Color{20, 22, 35, 255});
    if (ms.open >= 0) {
        if (pages[static_cast<size_t>(ms.open)].draw) {
            pages[static_cast<size_t>(ms.open)].draw(dt);
        }
        DrawText("ESC: back to mod list", 20, GetScreenHeight() - 40, 20, GRAY);
    } else {
        DrawText("Mods", 40, 40, 40, RAYWHITE);
        if (count == 0) {
            DrawText("No mods loaded. Put .cpp files in build/mods and restart.", 40, 110, 22, GRAY);
        }
        for (int i = 0; i < count; ++i) {
            DrawText(pages[static_cast<size_t>(i)].title.c_str(), 60, 110 + i * 40, 28, i == ms.sel ? YELLOW : RAYWHITE);
        }
        DrawText("Enter: open   ESC: back", 20, GetScreenHeight() - 40, 20, GRAY);
    }
    EndDrawing();
    return true;
}

// Online: applies a server line that changes other players, cars, or our own death.
void netHandleLine(Game &g, const std::string &line) {
    std::istringstream ss(line);
    std::string cmd;
    ss >> cmd;
    if (cmd == "PSTATE") {
        int fd = 0;
        int team = -1;
        int weapon = 0;
        int alive = 0;
        int hair = 0;
        int hairColor = 0;
        int hat = 0;
        int hatColor = 0;
        int jewel = 0;
        int logo = 0;
        int gunSkin = -1;
        int knifeSkin = -1;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float yaw = 0.0f;
        float pitch = 0.0f;
        float hp = 0.0f;
        ss >> fd >> team >> x >> y >> z >> yaw >> pitch >> weapon >> hp >> alive >> hair >> hairColor >> hat >> hatColor >> jewel >> logo >> gunSkin >> knifeSkin;
        if (ss.fail() || fd == g.localFd) {
            return;
        }
        if (alive == 0) {
            removeRemote(g, fd);
            return;
        }
        Enemy *e = remoteOf(g, fd);
        if (e == nullptr) {
            Enemy fresh{};
            fresh.netId = fd;
            fresh.pos = Vector3{x, y, z};
            g.enemies.push_back(fresh);
            e = &g.enemies.back();
        }
        e->netPos = Vector3{x, y, z};
        e->team = team == 0 ? TeamBlue : TeamRed;
        e->dir = Vector3{std::sin(yaw), 0.0f, std::cos(yaw)};
        e->weapon = (weapon >= 0 && weapon < WeaponCount) ? weapon : WeaponPistol;
        e->hp = std::max(1, static_cast<int>(std::ceil(hp / PlayerMaxHealth * EnemyHp)));
        e->netHealth = hp / PlayerMaxHealth;
        e->look.hair = std::clamp(hair, 0, HairStyleCount - 1);
        e->look.hairColor = std::clamp(hairColor, 0, static_cast<int>(std::size(HairColors)) - 1);
        e->look.hat = std::clamp(hat, 0, HatStyleCount - 1);
        e->look.hatColor = std::clamp(hatColor, 0, static_cast<int>(std::size(HatColors)) - 1);
        e->look.jewel = std::clamp(jewel, 0, JewelStyleCount - 1);
        e->look.logo = std::clamp(logo, 0, LogoStyleCount - 1);
        e->gunSkin = std::clamp(gunSkin, -1, GunSkinCount - 1);
        e->knifeSkin = std::clamp(knifeSkin, -1, KnifeSkinCount - 1);
    } else if (cmd == "HURT") {
        int attacker = 0;
        int weapon = -1;
        int head = 0;
        float dmg = 0.0f;
        ss >> attacker >> dmg >> head >> weapon;
        if (ss.fail() || g.dead) {
            return;
        }
        g.lastAttacker = attacker;
        g.lastWeapon = weapon;
        g.lastHead = head != 0;
        hurtPlayer(g, dmg);
    } else if (cmd == "KILL") {
        int victim = 0;
        int attacker = 0;
        int weapon = -1;
        int head = 0;
        ss >> victim >> attacker >> weapon >> head;
        if (ss.fail()) {
            return;
        }
        const bool iKilled = attacker == g.localFd;
        Team victimTeam = victim == g.localFd ? g.localTeam : TeamRed;
        Team killerTeam = attacker == g.localFd ? g.localTeam : TeamRed;
        if (const Enemy *v = remoteOf(g, victim)) {
            victimTeam = v->team;
        }
        if (const Enemy *k = remoteOf(g, attacker)) {
            killerTeam = k->team;
        }
        const std::string weaponName = weapon < 0 ? "HE grenade" : (weapon < WeaponCount ? Weapons[weapon].name : "Unknown");
        addKill(g, KillEntry{playerName(g, attacker), iKilled ? GOLD : teamColor(killerTeam), weaponName,
                             playerName(g, victim), teamColor(victimTeam), head != 0, false, false});
        if (iKilled) {
            g.freshKill = true;
        }
        if (victim == g.localFd) {
            g.lastHit = "Killed by " + playerName(g, attacker);
            g.hitTextTimer = 2.0f;
        } else {
            removeRemote(g, victim);
        }
    } else if (cmd == "CARS") {
        if (g.netHost) {
            return;
        }
        float p = 0.0f;
        float s = 0.0f;
        size_t i = 0;
        while (ss >> p >> s) {
            if (i < g.cars.size()) {
                g.cars[i].pos = p;
                g.cars[i].speed = s;
            }
            ++i;
        }
    } else if (cmd == "PLAYER_LEFT") {
        int fd = 0;
        ss >> fd;
        removeRemote(g, fd);
    }
}

// Online: sends our state and deaths, relays host car snapshots, and applies incoming lines.
void netFrame(Game &g, LobbyMenu &lm, float dt) {
    if (!g.online) {
        return;
    }
    for (const std::string &line : lm.conn.poll()) {
        netHandleLine(g, line);
        lobbyHandleLine(lm, line);
    }
    // Remote players glide toward their last reported position; far jumps snap.
    for (Enemy &e : g.enemies) {
        if (e.netId == 0) {
            continue;
        }
        const Vector3 d = Vector3Subtract(e.netPos, e.pos);
        if (Vector3Length(d) > 6.0f) {
            e.pos = e.netPos;
        } else {
            e.pos = Vector3Add(e.pos, Vector3Scale(d, std::min(1.0f, dt * 12.0f)));
        }
    }
    if (g.dead && !g.deathSent) {
        g.deathSent = true;
        std::ostringstream out;
        out << "DEATH " << g.lastAttacker << ' ' << g.lastWeapon << ' ' << (g.lastHead ? 1 : 0);
        g.netOut.push_back(out.str());
    }
    g.netSendTimer -= dt;
    if (g.netSendTimer <= 0.0f) {
        g.netSendTimer = NetSendInterval;
        std::ostringstream out;
        out << "STATE " << g.pos.x << ' ' << g.pos.y << ' ' << g.pos.z << ' ' << g.yaw << ' ' << g.pitch << ' '
            << g.weapon << ' ' << (g.dead ? 0.0f : g.health) << ' ' << (g.dead ? 0 : 1) << ' '
            << gPlayerLook.hair << ' ' << gPlayerLook.hairColor << ' ' << gPlayerLook.hat << ' '
            << gPlayerLook.hatColor << ' ' << gPlayerLook.jewel << ' ' << gPlayerLook.logo << ' '
            << gInv.equipGun[g.weapon] << ' ' << gInv.equipKnife;
        g.netOut.push_back(out.str());
    }
    if (g.netHost) {
        g.carSendTimer -= dt;
        if (g.carSendTimer <= 0.0f) {
            g.carSendTimer = CarSendInterval;
            std::ostringstream out;
            out << "CARS";
            for (const Car &c : g.cars) {
                out << ' ' << c.pos << ' ' << c.speed;
            }
            g.netOut.push_back(out.str());
        }
    }
    if (lm.conn.isOpen()) {
        for (const std::string &line : g.netOut) {
            lm.conn.sendLine(line);
        }
    }
    g.netOut.clear();
}

// Switches a freshly reset game into a shared match with the other lobby players.
void startOnlineMatch(Game &g, const LobbyMenu &lm) {
    g.online = true;
    g.netHost = lm.isHost;
    g.localFd = lm.myFd;
    g.localTeam = lm.myTeam == 1 ? TeamRed : TeamBlue;
    g.dead = false;
    g.deathSent = false;
    g.intermission = RoundBuyTime;
    g.roundLive = false;
    g.roundPlayers = 0;
    g.roundMsg.clear();
    g.netOut.clear();
    g.netSendTimer = 0.0f;
    g.carSendTimer = 0.0f;
    g.enemies.clear();
    placeOnlineSpawn(g);
}

// Leaves the shared match and drops the other players from the local world.
void endOnlineMatch(Game &g, LobbyMenu &lm) {
    if (lm.conn.isOpen()) {
        lm.conn.sendLine("LEAVE");
    }
    g.online = false;
    g.netHost = false;
    g.dead = false;
    g.netOut.clear();
    g.enemies.erase(std::remove_if(g.enemies.begin(), g.enemies.end(),
                                   [](const Enemy &e) { return e.netId != 0; }),
                    g.enemies.end());
}

int main() {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(1280, 720, "OverKill opensource");
    setGameIcon();
    SetWindowMinSize(640, 360);
    SetExitKey(KEY_NULL);
    loadSettings();
    if (gPlayerCustomId.empty()) {
        gPlayerCustomId = makeCustomId();
        saveSettings();
    }
    gServerLookup = std::async(std::launch::async, lookupServerAddress, gPlayerCustomId);
    loadInventory(gInv, inventoryFilePath());
    applyDisplay();
    initGraphics();
    SetTargetFPS(60);
    InitAudioDevice();
    initMods(std::string(GetApplicationDirectory()) + "mods", modAddScore, modHeal, modRefillAmmo, modExplodeAt,
             modKillAllEnemies, modGetCoins, modSetCoins, modUnlockKnives, modKnifeOwned, modUnlockKnife);

    Audio audio;
    audio.ready = IsAudioDeviceReady();
    if (audio.ready) {
        for (int w = 0; w < WeaponCount; ++w) {
            for (int k = 0; k < ShotSoundCount; ++k) {
                const unsigned int seed = 12345u + static_cast<unsigned int>(k * 97);
                audio.shots[w][k] = makeShotSound(ShotProfiles[w], seed);
                SetSoundVolume(audio.shots[w][k], 0.6f);
                audio.foeShots[w][k] = makeShotSound(ShotProfiles[w], seed);
                SetSoundVolume(audio.foeShots[w][k], 0.35f);
            }
        }
        for (int i = 0; i < StepSoundCount; ++i) {
            audio.steps[i] = makeStepSound(static_cast<unsigned int>(1000 + i * 77));
        }
        audio.chirps[0] = makeChirp(2600.0f, 3400.0f);
        audio.chirps[1] = makeChirp(3200.0f, 4300.0f);
        audio.chirps[2] = makeChirp(2200.0f, 2900.0f);
        audio.bug = makeBug();
        audio.flap = makeFlap();
        audio.wind = makeWind();
        audio.hurt = makeHurt();
        audio.hitMarker = makeHitMarker();
        audio.crack = makeCrack();
        audio.kill = makeKill();
        audio.boom = makeBoom();
        audio.bang = makeBang();
        audio.ring = makeRing();
        audio.hiss = makeHiss();
        audio.cash = makeCash();
        audio.clink = makeClink();
        audio.tick = makeTick();
        audio.saberHum = makeSaberHum();
        SetSoundVolume(audio.saberHum, 0.5f);
        audio.saberIgnite = makeSaberIgnite();
        audio.saberSwing = makeSaberSwing();
        audio.waveStart = makeSting(440.0f, 3);
        audio.roundWon = makeSting(523.0f, 4);
    }

    scanMusic(audio.playlist, std::string(GetApplicationDirectory()) + "music");
    rebuildMusicFont(audio.playlist);
    if (!audio.playlist.tracks.empty()) {
        loadTrack(audio.playlist, pickNext(audio.playlist));
    }

    std::vector<Level> previews(MapCount);
    RenderTexture2D previewTex[MapCount]{};
    MenuState menu;
    SettingsMenu settingsMenu;
    ModsState modState;
    AvatarMenu avatarMenu;
    CasesMenu casesMenu;
    LobbyMenu lobbyMenu;
    lobbyMenu.fields[LobbyAddress] = gServerAddress;
    for (int i = 0; i < MapCount; ++i) {
        Maps[i].build(previews[i]);
        finishLevel(previews[i]);
        previewTex[i] = LoadRenderTexture(static_cast<int>(CardW), static_cast<int>(PreviewH));
        menu.dirty[i] = true;
    }

    Screen screen = Screen::Menu;
    Game game;
    gModGame = &game;
    EnableCursor();

    while (!WindowShouldClose()) {
        const float dt = GetFrameTime();
        gModsLocked = lobbyMenu.joinedId >= 0 && !lobbyMenu.joinedPrivate;
        handleMusicKeys(audio.playlist);
        updatePlaylist(audio.playlist);

        if (screen == Screen::Menu) {
            if (IsKeyPressed(KEY_Q)) {
                saveInventory(gInv, inventoryFilePath());
                break;
            }
            if (settingsButtonPressed()) {
                settingsMenu = SettingsMenu{};
                screen = Screen::Settings;
                continue;
            }
            if (modsButtonPressed()) {
                modState = ModsState{};
                screen = Screen::Mods;
                continue;
            }
            if (avatarButtonPressed()) {
                avatarMenu = AvatarMenu{};
                screen = Screen::Avatar;
                continue;
            }
            if (casesButtonPressed()) {
                casesMenu = CasesMenu{};
                screen = Screen::Cases;
                continue;
            }
            if (lobbyMenuButtonPressed()) {
                screen = Screen::Lobby;
                continue;
            }
            const int pick = updateMenu(menu, dt);
            if (pick >= 0) {
                resetGame(game, pick);
                DisableCursor();
                screen = Screen::Playing;
                continue;
            }

            for (int i = 0; i < MapCount; ++i) {
                if (menu.dirty[i]) {
                    renderPreview(previewTex[i], previews[i], menu.angle[i]);
                    menu.dirty[i] = false;
                }
            }

            BeginDrawing();
            ClearBackground(Color{20, 22, 35, 255});
            drawMenu(menu, previewTex, audio.playlist);
            EndDrawing();
            continue;
        }

        if (screen == Screen::Mods) {
            if (!runMods(modState, dt)) {
                screen = Screen::Menu;
            }
            continue;
        }

        if (screen == Screen::Avatar) {
            if (updateAvatar(avatarMenu)) {
                saveSettings();
                screen = Screen::Menu;
                continue;
            }
            BeginDrawing();
            ClearBackground(Color{20, 22, 35, 255});
            drawAvatar(avatarMenu);
            EndDrawing();
            continue;
        }

        if (screen == Screen::Cases) {
            if (updateCases(casesMenu, audio, dt)) {
                saveInventory(gInv, inventoryFilePath());
                screen = Screen::Menu;
                continue;
            }
            BeginDrawing();
            ClearBackground(Color{20, 22, 35, 255});
            drawCases(casesMenu);
            EndDrawing();
            continue;
        }

        if (screen == Screen::Lobby) {
            if (updateLobby(lobbyMenu)) {
                screen = Screen::Menu;
                continue;
            }
            if (lobbyMenu.voting) {
                updateVote(lobbyMenu, dt);
                for (int i = 0; i < MapCount; ++i) {
                    if (lobbyMenu.voteMenu.dirty[i]) {
                        renderPreview(previewTex[i], previews[i], lobbyMenu.voteMenu.angle[i]);
                        lobbyMenu.voteMenu.dirty[i] = false;
                        break;
                    }
                }
                BeginDrawing();
                ClearBackground(Color{20, 22, 35, 255});
                drawVote(lobbyMenu, previewTex);
                EndDrawing();
                continue;
            }
            if (lobbyMenu.matchReady) {
                lobbyMenu.matchReady = false;
                gSettings.teamMode = lobbyMenu.joinedId >= 0 ? lobbyMenu.joinedMode == "teams" : lobbyMenu.createTeams;
                resetGame(game, lobbyMenu.map);
                if (lobbyMenu.joinedId >= 0) {
                    startOnlineMatch(game, lobbyMenu);
                }
                DisableCursor();
                screen = Screen::Playing;
                continue;
            }
            BeginDrawing();
            ClearBackground(Color{20, 22, 35, 255});
            drawLobby(lobbyMenu);
            EndDrawing();
            continue;
        }

        if (screen == Screen::Settings) {
            applyDisplay();
            if (gRescanMusic) {
                gRescanMusic = false;
                rescanMusic(audio.playlist);
            }
            if (updateSettings(settingsMenu)) {
                for (int i = 0; i < MapCount; ++i) {
                    menu.dirty[i] = true;
                }
                screen = Screen::Menu;
                continue;
            }
            BeginDrawing();
            ClearBackground(Color{20, 22, 35, 255});
            drawSettings(settingsMenu, audio.playlist.tracks.size());
            EndDrawing();
            continue;
        }

        if (IsKeyPressed(KEY_ESCAPE) && game.shopOpen) {
            game.shopOpen = false;
            DisableCursor();
        } else if (IsKeyPressed(KEY_ESCAPE)) {
            saveInventory(gInv, inventoryFilePath());
            EnableCursor();
            if (audio.ready) {
                StopSound(audio.wind);
            }
            if (game.online) {
                endOnlineMatch(game, lobbyMenu);
            }
            screen = Screen::Menu;
            continue;
        }
        if (IsKeyPressed(KEY_F5) && !game.online) {
            if (game.shopOpen) {
                DisableCursor();
            }
            resetGame(game, game.mapIndex);
        }
        netFrame(game, lobbyMenu, dt);
        if (game.online && !lobbyMenu.conn.isOpen()) {
            endOnlineMatch(game, lobbyMenu);
            screen = Screen::Menu;
            continue;
        }
        update(game, audio, dt);
        if (!gModsLocked) {
            for (const ModPlayFn hook : modPlayHooks()) {
                hook(dt);
            }
        }

        if (!gSettings.potato) {
            ensureScene();
            setLevelShader(game.level, gLit);
            setLitUniforms(game);

            BeginTextureMode(gScene);
            ClearBackground(skyOf(game));
            BeginMode3D(game.cam);
            BeginShaderMode(gLit);
            drawWorld(game);
            EndShaderMode();
            EndMode3D();
            EndTextureMode();
        }

        BeginDrawing();
        ClearBackground(BLACK);
        if (gSettings.potato) {
            setLevelShader(game.level, defaultShader());
            ClearBackground(skyOf(game));
            BeginMode3D(game.cam);
            drawWorld(game);
            EndMode3D();
        } else {
            const Vector2 postTexel{1.0f / static_cast<float>(gScene.texture.width),
                                    1.0f / static_cast<float>(gScene.texture.height)};
            SetShaderValue(gPost, GetShaderLocation(gPost, "uTexel"), &postTexel, SHADER_UNIFORM_VEC2);
            const float postContrast = 1.08f;
            const float postSaturation = 1.12f;
            SetShaderValue(gPost, GetShaderLocation(gPost, "uContrast"), &postContrast, SHADER_UNIFORM_FLOAT);
            SetShaderValue(gPost, GetShaderLocation(gPost, "uSaturation"), &postSaturation, SHADER_UNIFORM_FLOAT);
            BeginShaderMode(gPost);
            DrawTextureRec(gScene.texture,
                           Rectangle{0.0f, 0.0f, static_cast<float>(gScene.texture.width),
                                     -static_cast<float>(gScene.texture.height)},
                           Vector2{0.0f, 0.0f}, WHITE);
            EndShaderMode();
        }
        drawEnemyBars(game);
        drawBlood(game);
        drawHud(game, audio.playlist);
        drawRoundOverlay(game);
        if (game.online && game.dead) {
            const char *text = "You were killed - wait for the next round";
            DrawText(text, GetScreenWidth() / 2 - MeasureText(text, 30) / 2, GetScreenHeight() / 2 - 15, 30, RED);
        }
        EndDrawing();
    }

    unloadLevel(game.level);
    for (int i = 0; i < MapCount; ++i) {
        UnloadRenderTexture(previewTex[i]);
        if (previews[i].modelLoaded) {
            UnloadModel(previews[i].model);
        }
    }
    if (audio.ready) {
        for (auto &row : audio.shots) {
            for (Sound &s : row) {
                UnloadSound(s);
            }
        }
        for (auto &row : audio.foeShots) {
            for (Sound &s : row) {
                UnloadSound(s);
            }
        }
        for (Sound &s : audio.steps) {
            UnloadSound(s);
        }
        for (Sound &s : audio.chirps) {
            UnloadSound(s);
        }
        UnloadSound(audio.bug);
        UnloadSound(audio.flap);
        UnloadSound(audio.wind);
        UnloadSound(audio.hurt);
        UnloadSound(audio.hitMarker);
        UnloadSound(audio.crack);
        UnloadSound(audio.kill);
        UnloadSound(audio.boom);
        UnloadSound(audio.bang);
        UnloadSound(audio.ring);
        UnloadSound(audio.hiss);
        UnloadSound(audio.cash);
        UnloadSound(audio.clink);
        UnloadSound(audio.tick);
        UnloadSound(audio.saberHum);
        UnloadSound(audio.saberIgnite);
        UnloadSound(audio.saberSwing);
        UnloadSound(audio.waveStart);
        UnloadSound(audio.roundWon);
    }
    saveSettings();
    if (gMusicFontOwned) {
        UnloadFont(gMusicFont);
    }
    if (audio.playlist.loaded) {
        UnloadMusicStream(audio.playlist.music);
    }
    CloseAudioDevice();
    if (gStatic.id != 0) {
        UnloadTexture(gStatic);
    }
    shutdownGraphics();
    CloseWindow();
    return 0;
}
