#pragma once

// Overcoins, cases, keys and skins. Header-only so the save format and tables live in one place.

#include <raylib.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

enum class Rarity { Common, Uncommon, Rare, Epic, Legendary, Knife, Count };

constexpr int RarityCount = static_cast<int>(Rarity::Count);
constexpr int GunSkinCount = 100;
constexpr int KnifeSkinCount = 7;
// Index of the lightsaber in knifeSkins(). Only the lightsaber mod can unlock it, never a case.
constexpr int KnifeLightsaber = 6;
constexpr int CaseCount = 4;
constexpr int SkinSlots = 32;  // equip slots, indexed by the Weapon enum (21 weapons today)

inline const char *rarityName(Rarity r) {
    static const char *const names[RarityCount] = {"Common", "Uncommon", "Rare", "Epic", "Legendary", "Knife"};
    return names[static_cast<int>(r)];
}

inline Color rarityColor(Rarity r) {
    static const Color cols[RarityCount] = {Color{176, 196, 222, 255}, Color{94, 152, 217, 255},
                                            Color{75, 105, 255, 255},  Color{136, 71, 255, 255},
                                            Color{235, 75, 75, 255},   Color{255, 215, 0, 255}};
    return cols[static_cast<int>(r)];
}

struct Skin {
    std::string name;
    Rarity rarity;
    Color color;
};

inline unsigned char shadeByte(int v, float k) {
    return static_cast<unsigned char>(std::min(255.0f, static_cast<float>(v) * k));
}

inline Color shadeColor(Color c, float k) {
    return Color{shadeByte(c.r, k), shadeByte(c.g, k), shadeByte(c.b, k), 255};
}

// 100 gun skins: 10 finishes x 10 colours. Rarer finishes and colours come later in the list.
inline const std::vector<Skin> &gunSkins() {
    static const std::vector<Skin> skins = [] {
        const char *finish[10] = {"Chrome", "Matte", "Gold", "Neon", "Camo", "Frost", "Ember", "Shadow", "Venom", "Ocean"};
        const float shade[10] = {1.15f, 0.7f, 1.3f, 1.45f, 0.55f, 1.35f, 0.9f, 0.4f, 1.5f, 0.85f};
        const char *colour[10] = {"Crimson", "Amber", "Sun", "Jade", "Teal", "Cobalt", "Violet", "Rose", "Ivory", "Slate"};
        const Color base[10] = {Color{200, 40, 40, 255}, Color{230, 130, 30, 255}, Color{235, 200, 50, 255},
                                Color{60, 170, 80, 255}, Color{40, 160, 160, 255}, Color{50, 90, 210, 255},
                                Color{120, 60, 200, 255}, Color{210, 70, 160, 255}, Color{230, 230, 230, 255},
                                Color{80, 85, 95, 255}};
        std::vector<Skin> out;
        for (int f = 0; f < 10; ++f) {
            for (int c = 0; c < 10; ++c) {
                const int i = f * 10 + c;
                Rarity r = Rarity::Common;
                if (i >= 97) {
                    r = Rarity::Legendary;
                } else if (i >= 88) {
                    r = Rarity::Epic;
                } else if (i >= 75) {
                    r = Rarity::Rare;
                } else if (i >= 50) {
                    r = Rarity::Uncommon;
                }
                out.push_back(Skin{std::string(finish[f]) + " " + colour[c], r, shadeColor(base[c], shade[f])});
            }
        }
        return out;
    }();
    return skins;
}

// Knife types, in the order of their index. Each has its own model in app.cpp; the colour tints the grip.
inline const std::vector<Skin> &knifeSkins() {
    static const std::vector<Skin> skins = {
        Skin{"Karambit", Rarity::Knife, Color{30, 30, 36, 255}},
        Skin{"Butterfly", Rarity::Knife, Color{190, 30, 50, 255}},
        Skin{"Bayonet", Rarity::Knife, Color{30, 150, 80, 255}},
        Skin{"Huntsman", Rarity::Knife, Color{40, 80, 200, 255}},
        Skin{"Falchion", Rarity::Knife, Color{235, 235, 240, 255}},
        Skin{"Flip", Rarity::Knife, Color{220, 170, 40, 255}},
        Skin{"Lightsaber", Rarity::Knife, Color{60, 160, 255, 255}},
    };
    return skins;
}

// Weights are percentages out of 100, in Rarity order (Common .. Knife).
struct CaseDef {
    const char *name;
    int price;     // Overcoins for one box
    int keyPrice;  // Overcoins for one key
    int weights[RarityCount];
};

inline const std::array<CaseDef, CaseCount> &caseDefs() {
    static const std::array<CaseDef, CaseCount> defs = {{
        {"Starter Case", 60, 30, {70, 25, 5, 0, 0, 0}},
        {"Arms Case", 150, 75, {40, 40, 15, 5, 0, 0}},
        {"Elite Case", 400, 200, {0, 20, 45, 25, 10, 0}},
        {"Knife Case", 1200, 600, {0, 0, 0, 30, 50, 20}},
    }};
    return defs;
}

// Maps a roll in [0, 100) onto a rarity using the case weights.
inline Rarity rarityFromRoll(const CaseDef &c, int roll) {
    int acc = 0;
    for (int r = 0; r < RarityCount; ++r) {
        acc += c.weights[r];
        if (roll < acc) {
            return static_cast<Rarity>(r);
        }
    }
    return Rarity::Common;
}

// The player's economy. Owned by the game server; the client only holds a copy received from it.
struct Inventory {
    int coins = 0;
    int boxes[CaseCount]{};
    int keys[CaseCount]{};
    bool gun[GunSkinCount]{};
    bool knife[KnifeSkinCount]{};
    int equipGun[SkinSlots];  // skin index per weapon, -1 for the default look
    int equipKnife = -1;
    bool adminSign = false;   // granted by the server to customIds listed in admins.txt

    Inventory() {
        std::fill(std::begin(equipGun), std::end(equipGun), -1);
    }
};

// Parses the server's INV payload, where '|' stands in for newlines.
inline void inventoryFromText(Inventory &inv, const std::string &text) {
    std::string normalized = text;
    std::replace(normalized.begin(), normalized.end(), '|', '\n');
    std::istringstream in(normalized);
    std::string line;
    while (std::getline(in, line)) {
        const size_t eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        const std::string key = line.substr(0, eq);
        const std::string val = line.substr(eq + 1);
        const int num = std::atoi(val.c_str());
        if (key == "coins") {
            inv.coins = std::max(0, num);
        } else if (key == "gun") {
            for (size_t i = 0; i < GunSkinCount && i < val.size(); ++i) {
                inv.gun[i] = val[i] == '1';
            }
        } else if (key == "knife") {
            for (size_t i = 0; i < KnifeSkinCount && i < val.size(); ++i) {
                inv.knife[i] = val[i] == '1';
            }
        } else if (key == "equipKnife") {
            inv.equipKnife = std::clamp(num, -1, KnifeSkinCount - 1);
        } else if (key == "adminSign") {
            inv.adminSign = num != 0;
        } else if (key.compare(0, 3, "box") == 0) {
            const int c = std::atoi(key.c_str() + 3);
            if (c >= 0 && c < CaseCount) {
                inv.boxes[c] = std::max(0, num);
            }
        } else if (key.compare(0, 3, "key") == 0) {
            const int c = std::atoi(key.c_str() + 3);
            if (c >= 0 && c < CaseCount) {
                inv.keys[c] = std::max(0, num);
            }
        } else if (key.compare(0, 5, "equip") == 0) {
            const int w = std::atoi(key.c_str() + 5);
            if (w >= 0 && w < SkinSlots) {
                inv.equipGun[w] = std::clamp(num, -1, GunSkinCount - 1);
            }
        }
    }
}
