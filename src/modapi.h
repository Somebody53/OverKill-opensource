#pragma once
#include "raylib.h"

// Mods export: extern "C" void modInit(ModApi *api);
// Mods are compiled at startup from build/mods/*.cpp and may use raylib.
#define MOD_API_VERSION 5

// Draws a mod page each frame while it is open in the Mods screen.
typedef void (*ModPageFn)(float dt);
// Called each frame during play. Every registered hook runs, in load order.
typedef void (*ModPlayFn)(float dt);
// Called with the impact point when a bullet hits an enemy. Every registered hook runs, in load order.
typedef void (*ModHitFn)(Vector3 pos);

struct ModApi {
    int version;
    void (*addPage)(const char *title, ModPageFn draw);
    void (*setPlayHook)(ModPlayFn fn);
    void (*addScore)(int points);
    void (*heal)(float amount);
    void (*refillAmmo)();
    void (*explodeAt)(Vector3 pos);
    void (*setBulletHitHook)(ModHitFn fn);
    void (*killAllEnemies)();
    int (*getCoins)();
    void (*setCoins)(int coins);
    void (*unlockAllKnives)();
    int (*knifeOwned)(int index);
    void (*unlockKnife)(int index);
};
