#pragma once
#include "modapi.h"

#include <string>
#include <vector>

struct ModPage {
    std::string title;
    ModPageFn draw;
};

// Compiles and loads every build/mods/*.cpp; addScore and heal act on the live game.
void initMods(const std::string &dir, void (*addScore)(int), void (*heal)(float), void (*refillAmmo)(),
              void (*explodeAt)(Vector3), void (*killAll)(), int (*getCoins)(), void (*setCoins)(int),
              void (*unlockKnives)(), int (*knifeOwned)(int), void (*unlockKnife)(int));
const std::vector<ModPage> &modPages();
const std::vector<ModPlayFn> &modPlayHooks();
const std::vector<ModHitFn> &modHitHooks();
