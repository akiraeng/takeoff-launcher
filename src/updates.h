#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>

namespace takeoff {

inline constexpr wchar_t kAppVersion[] = L"1.0.0";
inline constexpr wchar_t kDefaultReleasesUrl[] = L"https://github.com/akiraredddd/Takeoff/releases";
inline constexpr wchar_t kDefaultApiHost[] = L"api.github.com";
inline constexpr wchar_t kDefaultApiPath[] = L"/repos/akiraredddd/Takeoff/releases/latest";

inline std::wstring ExtractTagName(std::string_view json) {
    const std::string_view key = "\"tag_name\"";
    size_t pos = json.find(key);
    if (pos == std::string_view::npos) return {};
    pos += key.size();
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == ':' || json[pos] == '\t' || json[pos] == '\r' || json[pos] == '\n')) {
        pos++;
    }
    if (pos >= json.size() || json[pos] != '"') return {};
    pos++; // skip opening quote
    size_t end = json.find('"', pos);
    if (end == std::string_view::npos) return {};
    std::string tag(json.substr(pos, end - pos));
    return std::wstring(tag.begin(), tag.end());
}

inline std::vector<int> ParseVersion(std::wstring_view v) {
    while (!v.empty() && (v.front() == L'v' || v.front() == L'V' || v.front() == L' ')) {
        v.remove_prefix(1);
    }
    std::vector<int> parts;
    int current = 0;
    bool hasNum = false;
    for (wchar_t ch : v) {
        if (ch >= L'0' && ch <= L'9') {
            current = current * 10 + (ch - L'0');
            hasNum = true;
        } else if (ch == L'.' || ch == L'-') {
            if (hasNum) {
                parts.push_back(current);
                current = 0;
                hasNum = false;
            }
            if (ch == L'-') break; // Ignore pre-release suffixes (e.g. -beta, -rc1)
        }
    }
    if (hasNum) {
        parts.push_back(current);
    }
    return parts;
}

inline bool IsNewerVersion(std::wstring_view remote, std::wstring_view current) {
    auto r = ParseVersion(remote);
    auto c = ParseVersion(current);
    if (r.empty()) return false;
    const size_t n = (std::max)(r.size(), c.size());
    for (size_t i = 0; i < n; ++i) {
        const int rv = i < r.size() ? r[i] : 0;
        const int cv = i < c.size() ? c[i] : 0;
        if (rv > cv) return true;
        if (rv < cv) return false;
    }
    return false;
}

inline bool ShouldCheckForUpdates(uint64_t lastCheckSeconds, uint64_t currentSeconds, bool enabled) {
    if (!enabled) return false;
    constexpr uint64_t k24HoursInSeconds = 24 * 60 * 60; // 86400
    if (currentSeconds < lastCheckSeconds) return true; // Clock shifted backwards
    return (currentSeconds - lastCheckSeconds) >= k24HoursInSeconds;
}

inline bool QueryLatestReleaseTag(std::wstring_view host, std::wstring_view path,
                                  std::wstring& outTag, std::wstring& outHtmlUrl) {
    HINTERNET session = WinHttpOpen(L"Takeoff-Launcher/1.0",
                                    WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                    WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return false;

    // 5-second timeouts for connect, send, and receive so we never stall
    WinHttpSetTimeouts(session, 5000, 5000, 5000, 5000);

    std::wstring hostStr(host);
    HINTERNET connect = WinHttpConnect(session, hostStr.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!connect) {
        WinHttpCloseHandle(session);
        return false;
    }

    std::wstring pathStr(path);
    HINTERNET request = WinHttpOpenRequest(connect, L"GET", pathStr.c_str(),
                                          nullptr, WINHTTP_NO_REFERER,
                                          WINHTTP_DEFAULT_ACCEPT_TYPES,
                                          WINHTTP_FLAG_SECURE);
    if (!request) {
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return false;
    }

    // GitHub API requires Accept header and recommends User-Agent (already set in WinHttpOpen)
    const wchar_t headers[] = L"Accept: application/vnd.github.v3+json\r\n";
    BOOL sent = WinHttpSendRequest(request, headers, static_cast<DWORD>(-1),
                                  WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    bool success = false;
    if (sent && WinHttpReceiveResponse(request, nullptr)) {
        DWORD statusCode = 0;
        DWORD size = sizeof(statusCode);
        if (WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &size, WINHTTP_NO_HEADER_INDEX) &&
            statusCode == 200) {
            std::string response;
            DWORD bytesAvailable = 0;
            while (WinHttpQueryDataAvailable(request, &bytesAvailable) && bytesAvailable > 0) {
                std::vector<char> buffer(bytesAvailable);
                DWORD bytesRead = 0;
                if (WinHttpReadData(request, buffer.data(), bytesAvailable, &bytesRead) && bytesRead > 0) {
                    response.append(buffer.data(), bytesRead);
                }
            }
            std::wstring tag = ExtractTagName(response);
            if (!tag.empty()) {
                outTag = std::move(tag);
                // Also optionally extract "html_url" if present
                const std::string_view htmlUrlKey = "\"html_url\"";
                size_t hpos = response.find(htmlUrlKey);
                if (hpos != std::string_view::npos) {
                    hpos += htmlUrlKey.size();
                    while (hpos < response.size() && (response[hpos] == ' ' || response[hpos] == ':' || response[hpos] == '\t')) hpos++;
                    if (hpos < response.size() && response[hpos] == '"') {
                        hpos++;
                        size_t hend = response.find('"', hpos);
                        if (hend != std::string_view::npos) {
                            std::string u = response.substr(hpos, hend - hpos);
                            outHtmlUrl = std::wstring(u.begin(), u.end());
                        }
                    }
                }
                success = true;
            }
        }
    }

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return success;
}

} // namespace takeoff

