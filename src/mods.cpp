#include "mods.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <system_error>

#ifndef _WIN32
#include <dlfcn.h>
#endif

namespace fs = std::filesystem;

namespace {

std::vector<ModPage> gPages;
std::vector<ModPlayFn> gPlayHooks;
void (*gAddScore)(int) = nullptr;
void (*gHeal)(float) = nullptr;
void (*gRefillAmmo)() = nullptr;
void (*gExplodeAt)(Vector3) = nullptr;
void (*gKillAll)() = nullptr;
int (*gGetCoins)() = nullptr;
void (*gSetCoins)(int) = nullptr;
void (*gUnlockKnives)() = nullptr;
int (*gKnifeOwned)(int) = nullptr;
void (*gUnlockKnife)(int) = nullptr;
std::vector<ModHitFn> gHitHooks;
ModApi gApi{};

void apiAddPage(const char *title, ModPageFn draw) {
    gPages.push_back(ModPage{title, draw});
}

void apiSetPlayHook(ModPlayFn fn) {
    if (fn) {
        gPlayHooks.push_back(fn);
    }
}

void apiAddScore(int points) {
    if (gAddScore) {
        gAddScore(points);
    }
}

void apiHeal(float amount) {
    if (gHeal) {
        gHeal(amount);
    }
}

void apiRefillAmmo() {
    if (gRefillAmmo) {
        gRefillAmmo();
    }
}

void apiExplodeAt(Vector3 pos) {
    if (gExplodeAt) {
        gExplodeAt(pos);
    }
}

void apiSetBulletHitHook(ModHitFn fn) {
    if (fn) {
        gHitHooks.push_back(fn);
    }
}

void apiKillAllEnemies() {
    if (gKillAll) {
        gKillAll();
    }
}

int apiGetCoins() {
    return gGetCoins ? gGetCoins() : 0;
}

void apiSetCoins(int coins) {
    if (gSetCoins) {
        gSetCoins(coins);
    }
}

void apiUnlockAllKnives() {
    if (gUnlockKnives) {
        gUnlockKnives();
    }
}

int apiKnifeOwned(int index) {
    return gKnifeOwned ? gKnifeOwned(index) : 0;
}

void apiUnlockKnife(int index) {
    if (gUnlockKnife) {
        gUnlockKnife(index);
    }
}

#ifndef _WIN32
bool compileMod(const fs::path &src, const fs::path &lib, const fs::path &dir) {
    fs::path log = src;
    log.replace_extension(".log");
    const std::string cmd = "g++ -std=c++17 -O2 -shared -fPIC -I'" + dir.string() + "' '" + src.string() +
                            "' -o '" + lib.string() + "' > '" + log.string() + "' 2>&1";
    return std::system(cmd.c_str()) == 0;
}

void loadOne(const fs::path &src, const fs::path &dir) {
    fs::path lib = src;
    lib.replace_extension(".so");
    std::error_code ec;
    const bool stale = !fs::exists(lib, ec) || fs::last_write_time(src, ec) > fs::last_write_time(lib, ec);
    if (stale && !compileMod(src, lib, dir)) {
        fs::remove(lib, ec);
        std::fprintf(stderr, "Mod %s failed to compile (see %s.log)\n", src.filename().string().c_str(),
                     src.stem().string().c_str());
        return;
    }
    void *handle = dlopen(lib.c_str(), RTLD_NOW | RTLD_GLOBAL);
    if (!handle) {
        std::fprintf(stderr, "Mod %s failed to load: %s\n", src.filename().string().c_str(), dlerror());
        return;
    }
    auto init = reinterpret_cast<void (*)(ModApi *)>(dlsym(handle, "modInit"));
    if (!init) {
        std::fprintf(stderr, "Mod %s has no modInit\n", src.filename().string().c_str());
        return;
    }
    init(&gApi);
}
#endif // _WIN32

} // namespace

void initMods(const std::string &dir, void (*addScore)(int), void (*heal)(float), void (*refillAmmo)(),
              void (*explodeAt)(Vector3), void (*killAll)(), int (*getCoins)(), void (*setCoins)(int),
              void (*unlockKnives)(), int (*knifeOwned)(int), void (*unlockKnife)(int)) {
    gKnifeOwned = knifeOwned;
    gUnlockKnife = unlockKnife;
    gAddScore = addScore;
    gHeal = heal;
    gRefillAmmo = refillAmmo;
    gExplodeAt = explodeAt;
    gKillAll = killAll;
    gGetCoins = getCoins;
    gSetCoins = setCoins;
    gUnlockKnives = unlockKnives;
    gApi = ModApi{MOD_API_VERSION, apiAddPage, apiSetPlayHook, apiAddScore,
                  apiHeal, apiRefillAmmo, apiExplodeAt, apiSetBulletHitHook, apiKillAllEnemies,
                  apiGetCoins, apiSetCoins, apiUnlockAllKnives, apiKnifeOwned, apiUnlockKnife};

#ifdef _WIN32
    (void)dir;
    std::fprintf(stderr, "Mods are not supported in the Windows build\n");
#else
    const fs::path root = dir;
    std::error_code ec;
    fs::create_directories(root, ec);

    std::vector<fs::path> sources;
    for (const fs::directory_entry &e : fs::directory_iterator(root, ec)) {
        if (e.is_regular_file(ec) && e.path().extension() == ".cpp") {
            sources.push_back(e.path());
        }
    }
    std::sort(sources.begin(), sources.end());
    for (const fs::path &src : sources) {
        loadOne(src, root);
    }
#endif
}

const std::vector<ModPage> &modPages() {
    return gPages;
}

const std::vector<ModPlayFn> &modPlayHooks() {
    return gPlayHooks;
}

const std::vector<ModHitFn> &modHitHooks() {
    return gHitHooks;
}
