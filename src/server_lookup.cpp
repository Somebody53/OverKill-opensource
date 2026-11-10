#include "server_lookup.h"

#include <cstdio>
#include <random>

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#else
#include <curl/curl.h>
#endif

namespace {

// Public PlayFab title ID. Safe to ship in the client. Never put the secret key here.
constexpr const char *PlayFabTitleId = "16FD4";
constexpr const char *PlayFabTitleDataKey = "ServerAddress";
constexpr const char *PlayFabVersionKey = "GameVersion";

// Extracts a simple "key":"value" string field (values here contain no escapes).
std::string jsonStringField(const std::string &text, const std::string &key) {
    const std::string needle = "\"" + key + "\":\"";
    const size_t start = text.find(needle);
    if (start == std::string::npos) {
        return {};
    }
    const size_t valueStart = start + needle.size();
    const size_t valueEnd = text.find('"', valueStart);
    if (valueEnd == std::string::npos) {
        return {};
    }
    return text.substr(valueStart, valueEnd - valueStart);
}

#ifdef _WIN32

std::wstring toWide(const std::string &text) {
    return std::wstring(text.begin(), text.end());
}

bool postJson(const std::string &host, const std::string &path, const std::string &body,
              const std::string &auth, std::string &response, std::string &error) {
    HINTERNET session = WinHttpOpen(L"OverKill", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        error = "WinHTTP init failed";
        return false;
    }
    WinHttpSetTimeouts(session, 8000, 8000, 8000, 8000);
    HINTERNET connection = WinHttpConnect(session, toWide(host).c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET request = connection ? WinHttpOpenRequest(connection, L"POST", toWide(path).c_str(), nullptr,
                                                        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                        WINHTTP_FLAG_SECURE)
                                   : nullptr;
    std::string headers = "Content-Type: application/json\r\n";
    if (!auth.empty()) {
        headers += "X-Authorization: " + auth + "\r\n";
    }
    bool ok = request && WinHttpSendRequest(request, toWide(headers).c_str(), static_cast<DWORD>(-1L),
                                            const_cast<char *>(body.data()), static_cast<DWORD>(body.size()),
                                            static_cast<DWORD>(body.size()), 0) &&
              WinHttpReceiveResponse(request, nullptr);
    if (ok) {
        DWORD available = 0;
        while (WinHttpQueryDataAvailable(request, &available) && available > 0) {
            std::string chunk(available, '\0');
            DWORD read = 0;
            if (!WinHttpReadData(request, &chunk[0], available, &read)) {
                ok = false;
                break;
            }
            response.append(chunk.data(), read);
        }
    }
    if (!ok) {
        error = "network request failed";
    }
    if (request) {
        WinHttpCloseHandle(request);
    }
    if (connection) {
        WinHttpCloseHandle(connection);
    }
    WinHttpCloseHandle(session);
    return ok;
}

#else

size_t appendResponse(char *data, size_t size, size_t count, void *out) {
    static_cast<std::string *>(out)->append(data, size * count);
    return size * count;
}

bool postJson(const std::string &host, const std::string &path, const std::string &body,
              const std::string &auth, std::string &response, std::string &error) {
    CURL *curl = curl_easy_init();
    if (!curl) {
        error = "curl init failed";
        return false;
    }
    curl_slist *headers = curl_slist_append(nullptr, "Content-Type: application/json");
    if (!auth.empty()) {
        headers = curl_slist_append(headers, ("X-Authorization: " + auth).c_str());
    }
    const std::string url = "https://" + host + path;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, appendResponse);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 8L);
    const CURLcode rc = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    if (rc != CURLE_OK) {
        error = curl_easy_strerror(rc);
        return false;
    }
    return true;
}

#endif

std::string playFabHost() {
    return std::string(PlayFabTitleId) + ".playfabapi.com";
}

// Logs in with the custom ID and returns the session ticket.
bool playFabLogin(const std::string &customId, std::string &ticket, std::string &error) {
    const std::string loginBody = std::string("{\"TitleId\":\"") + PlayFabTitleId + "\",\"CustomId\":\"" +
                                  customId + "\",\"CreateAccount\":true}";
    std::string response;
    if (!postJson(playFabHost(), "/Client/LoginWithCustomID", loginBody, "", response, error)) {
        return false;
    }
    ticket = jsonStringField(response, "SessionTicket");
    if (ticket.empty()) {
        error = "PlayFab login failed";
        return false;
    }
    return true;
}

} // namespace

std::string makeCustomId() {
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<unsigned long long> dist;
    const unsigned long long a = dist(gen);
    const unsigned long long b = dist(gen);
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%016llx%016llx", a, b);
    return buf;
}

ServerLookup lookupServerAddress(const std::string &customId) {
    ServerLookup result;
    std::string ticket;
    if (!playFabLogin(customId, ticket, result.error)) {
        return result;
    }

    std::string response;
    const std::string dataBody = std::string("{\"Keys\":[\"") + PlayFabTitleDataKey + "\",\"" +
                                 PlayFabVersionKey + "\"]}";
    if (!postJson(playFabHost(), "/Client/GetTitleData", dataBody, ticket, response, result.error)) {
        return result;
    }
    const std::string address = jsonStringField(response, PlayFabTitleDataKey);
    if (address.empty()) {
        result.error = "ServerAddress not set in PlayFab";
        return result;
    }

    result.ok = true;
    result.address = address;
    result.latestVersion = jsonStringField(response, PlayFabVersionKey);
    return result;
}

