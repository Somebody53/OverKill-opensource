#pragma once

#include <string>

// Result of asking PlayFab Title Data for the current lobby server address.
struct ServerLookup {
    bool ok = false;
    std::string address;
    std::string latestVersion;
    std::string error;
};

// Random identifier used as the PlayFab CustomId. Created once and saved in settings.
std::string makeCustomId();

// Blocking HTTPS request to PlayFab (login + GetTitleData "ServerAddress" and "GameVersion").
// Run it on a background thread.
ServerLookup lookupServerAddress(const std::string &customId);
