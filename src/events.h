#pragma once

#include "raylib.h"

#include <algorithm>
#include <string>

// Cosmetic seasonal events. The server stores the active one and pushes it to every client
// ("EVENT <id>" line). Admin GUI uses the same table. Keep the order identical to the enum.
enum class SeasonEvent : int {
    Default,
    NewYear,
    Valentine,
    Spring,
    Easter,
    Summer,
    Fall,
    Halloween,
    Christmas,
    Count
};

// Falling particles drawn over menus and the game.
enum class Weather : int { None, Snow, Leaves, Petals, Embers, Sparkles };

// Props added to every map for an event.
enum class SeasonDecor : int { None, Pumpkins, Hearts, Blossom, Easter, Leaves, Sprinklers, Christmas };

struct SeasonInfo {
    const char *id;          // name used on the wire and in events.txt
    const char *label;       // name shown to players and in the admin GUI
    const char *description; // short text for the admin GUI
    Color menuBg;            // menu background
    Color tint;              // blended into terrain, walls, roofs and sky of every map
    float tintAmount;        // 0 keeps the normal map colours
    Weather weather;
    Color weatherColor;
    Color sky;               // replaces the map sky when alpha > 0
    SeasonDecor decor;
};

inline const SeasonInfo SeasonTable[] = {
    {"default", "Default", "Maps and colours as usual", Color{20, 22, 35, 255}, WHITE, 0.0f, Weather::None, WHITE,
     Color{0, 0, 0, 0}, SeasonDecor::None},
    {"newyear", "New Year", "Gold tint and sparkles", Color{14, 14, 36, 255}, Color{255, 215, 90, 255}, 0.12f,
     Weather::Sparkles, Color{255, 220, 120, 255}, Color{0, 0, 0, 0}, SeasonDecor::None},
    {"valentine", "Valentine's Day", "Winter snow with hearts everywhere", Color{60, 16, 36, 255},
     Color{255, 170, 200, 255}, 0.3f, Weather::Snow, Color{255, 220, 235, 255}, Color{0, 0, 0, 0}, SeasonDecor::Hearts},
    {"spring", "Spring", "Blossom trees and petals", Color{40, 70, 40, 255}, Color{170, 235, 140, 255}, 0.25f,
     Weather::Petals, Color{255, 200, 230, 255}, Color{0, 0, 0, 0}, SeasonDecor::Blossom},
    {"easter", "Easter", "Eggs, bunnies and pink nature", Color{60, 80, 110, 255}, Color{255, 170, 220, 255}, 0.35f,
     Weather::Petals, Color{255, 240, 150, 255}, Color{0, 0, 0, 0}, SeasonDecor::Easter},
    {"summer", "Summer", "Hot sun and sprinklers", Color{20, 60, 90, 255}, Color{255, 190, 80, 255}, 0.25f,
     Weather::None, WHITE, Color{255, 214, 130, 255}, SeasonDecor::Sprinklers},
    {"fall", "Fall", "Brown ground and falling leaves", Color{50, 30, 15, 255}, Color{140, 85, 40, 255}, 0.45f,
     Weather::Leaves, Color{220, 120, 40, 255}, Color{0, 0, 0, 0}, SeasonDecor::Leaves},
    {"halloween", "Halloween", "Dark nights with glowing pumpkins", Color{18, 8, 24, 255}, Color{90, 30, 10, 255}, 0.3f,
     Weather::Embers, Color{255, 130, 30, 255}, Color{14, 6, 28, 255}, SeasonDecor::Pumpkins},
    {"christmas", "Christmas", "Snow, trees and presents", Color{12, 30, 28, 255}, Color{235, 245, 255, 255}, 0.35f,
     Weather::Snow, WHITE, Color{0, 0, 0, 0}, SeasonDecor::Christmas},
};

static_assert(sizeof(SeasonTable) / sizeof(SeasonTable[0]) == static_cast<size_t>(SeasonEvent::Count),
              "SeasonTable must match SeasonEvent");

// Event shown in this session. Defaults to the normal look until the server sends one.
inline SeasonEvent gSeason = SeasonEvent::Default;

inline const SeasonInfo &seasonInfo(SeasonEvent e) {
    return SeasonTable[static_cast<int>(e)];
}

inline const SeasonInfo &seasonInfo() {
    return seasonInfo(gSeason);
}

// Unknown ids fall back to the default event.
inline SeasonEvent seasonFromId(const std::string &id) {
    for (int i = 0; i < static_cast<int>(SeasonEvent::Count); ++i) {
        if (id == SeasonTable[i].id) {
            return static_cast<SeasonEvent>(i);
        }
    }
    return SeasonEvent::Default;
}

inline unsigned char mixChannel(unsigned char a, unsigned char b, float t) {
    const float v = static_cast<float>(a) + (static_cast<float>(b) - static_cast<float>(a)) * t;
    return static_cast<unsigned char>(std::clamp(v, 0.0f, 255.0f) + 0.5f);
}

// Moves c toward target by t (0..1), keeping c's alpha.
inline Color blendTowards(Color c, Color target, float t) {
    return Color{mixChannel(c.r, target.r, t), mixChannel(c.g, target.g, t), mixChannel(c.b, target.b, t), c.a};
}
