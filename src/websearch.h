#pragma once

// Web search engines: data model, registry persistence, keyword-prefix query
// parsing, and search-URL construction. The settings UI that lists and edits
// engines lives in launcher.h with the rest of the window drawing.

#include "search.h"

#include <algorithm>
#include <cwctype>
#include <string>
#include <string_view>
#include <vector>

namespace takeoff {

struct SearchEngine {
    std::wstring name;      // Display name, e.g. "DuckDuckGo".
    std::wstring keyword;   // Typed prefix in the launcher, e.g. "d" for "d cats".
    std::wstring url;       // Template containing {query}.
};

// The single-URL default shipped before search engines became a list. A stored
// value equal to this is dropped during migration instead of becoming an engine.
inline constexpr wchar_t kLegacySearchEngineUrl[] = L"https://www.google.com/search?q={query}";

inline constexpr size_t kMaxSearchEngines = 32;

// Placeholder replaced by the URL-encoded query in every engine template.
inline constexpr wchar_t kQueryPlaceholder[] = L"{query}";

// Mirrors kSettingsRegistryPath + "\SearchEngines" from launcher.h; kept local
// so this header does not depend on the window class.
inline constexpr wchar_t kEnginesRegistryKey[] = L"Software\\Takeoff\\SearchEngines";

inline std::vector<SearchEngine> DefaultSearchEngines() {
    return {
        {L"DuckDuckGo", L"d", L"https://duckduckgo.com/?q={query}"},
        {L"YouTube", L"yt", L"https://www.youtube.com/results?search_query={query}"},
        {L"Bing", L"b", L"https://www.bing.com/search?q={query}"},
    };
}

inline SearchEngine CreateDefaultEngine() {
    return {L"New engine", L"", L"https://"};
}

// Host of the URL's template portion ("https://duckduckgo.com/?q=…" →
// "duckduckgo.com"); "web" when no scheme is present.
inline std::wstring ExtractHostname(const std::wstring& url) {
    const auto qPos = url.find(L"{query}");
    const std::wstring base = (qPos != std::wstring::npos) ? url.substr(0, qPos) : url;
    const auto schemeEnd = base.find(L"://");
    if (schemeEnd != std::wstring::npos) {
        const auto hostStart = schemeEnd + 3;
        const auto slashPos = base.find(L'/', hostStart);
        std::wstring host = base.substr(hostStart,
            slashPos != std::wstring::npos ? slashPos - hostStart : std::wstring::npos);
        if (!host.empty()) return host;
    }
    return L"web";
}

// True when the template can actually be handed to ShellExecuteW: it needs a
// scheme plus a non-empty host ("https://" alone is a half-typed edit, not a
// usable engine).
inline bool IsUsableEngineUrl(const std::wstring& url) {
    const auto schemeEnd = url.find(L"://");
    if (schemeEnd == std::wstring::npos || schemeEnd == 0) return false;
    const auto hostStart = schemeEnd + 3;
    const auto slashPos = url.find(L'/', hostStart);
    const std::wstring host = url.substr(hostStart,
        slashPos != std::wstring::npos ? slashPos - hostStart : std::wstring::npos);
    return !host.empty();
}

inline std::wstring EngineDisplayName(const SearchEngine& engine) {
    return engine.name.empty() ? ExtractHostname(engine.url) : engine.name;
}

// Substitutes the URL-encoded query for {query}; engines without a placeholder
// get the query appended.
inline std::wstring BuildSearchUrl(const SearchEngine& engine, std::wstring_view query) {
    const std::wstring placeholder = kQueryPlaceholder;
    std::wstring url = engine.url;
    const std::wstring encoded = UrlEncode(query);
    bool replaced = false;
    for (size_t pos = url.find(placeholder); pos != std::wstring::npos;
            pos = url.find(placeholder, pos + encoded.size())) {
        url.replace(pos, placeholder.size(), encoded);
        replaced = true;
    }
    if (!replaced) url += encoded;
    return url;
}

struct ParsedEngineQuery {
    int engineIndex = -1;  // Engine whose keyword the query starts with; -1 if none.
    std::wstring query;    // Text after the keyword, or the full text.
};

// Splits "d cats" into the engine matching the "d" keyword plus the trimmed
// rest. The keyword alone ("d" or "d ") is not web intent, so it parses to -1.
inline ParsedEngineQuery ParseKeywordQuery(const std::vector<SearchEngine>& engines,
                                           const std::wstring& text) {
    ParsedEngineQuery parsed;
    parsed.query = text;
    if (engines.empty()) return parsed;
    const size_t space = text.find(L' ');
    if (space == std::wstring::npos || space == 0) return parsed;
    const std::wstring token = Normalize(std::wstring_view(text).substr(0, space));
    if (token.empty()) return parsed;
    std::wstring rest = text.substr(space + 1);
    const size_t first = rest.find_first_not_of(L" \t");
    if (first == std::wstring::npos) return parsed;
    rest.erase(0, first);
    for (size_t i = 0; i < engines.size(); ++i) {
        if (!engines[i].keyword.empty() && Normalize(engines[i].keyword) == token) {
            parsed.engineIndex = static_cast<int>(i);
            parsed.query = std::move(rest);
            break;
        }
    }
    return parsed;
}

// Normalizes edited fields before saving: trims surrounding whitespace,
// removes spaces from the keyword (a keyword containing a space could never
// match the word before the first space in a query), and falls back to the
// URL host when no name was entered.
inline void NormalizeEditedEngine(SearchEngine& engine,
                                  const std::wstring& name,
                                  const std::wstring& keyword,
                                  const std::wstring& url) {
    auto trim = [](const std::wstring& text) {
        const size_t first = text.find_first_not_of(L" \t");
        if (first == std::wstring::npos) return std::wstring();
        const size_t last = text.find_last_not_of(L" \t");
        return text.substr(first, last - first + 1);
    };
    engine.name = trim(name);
    engine.keyword = trim(keyword);
    engine.keyword.erase(std::remove(engine.keyword.begin(), engine.keyword.end(), L' '),
        engine.keyword.end());
    engine.url = trim(url);
    if (engine.name.empty()) engine.name = ExtractHostname(engine.url);
}

// ---- Registry persistence -------------------------------------------------

struct EngineRegistryData {
    bool stored = false;   // Subkey existed: the list (even empty) is authoritative.
    std::vector<SearchEngine> engines;  // Read list when stored; migrated engine when not.
    int defaultEngine = 0;
};

inline DWORD ReadEngineDword(HKEY key, const wchar_t* name, DWORD fallback) {
    DWORD value = fallback;
    DWORD size = sizeof(value);
    if (RegGetValueW(key, nullptr, name, RRF_RT_REG_DWORD, nullptr, &value, &size) != ERROR_SUCCESS) {
        return fallback;
    }
    return value;
}

inline EngineRegistryData LoadEnginesFromRegistry(const std::wstring& legacyUrl) {
    EngineRegistryData data;
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kEnginesRegistryKey, 0, KEY_READ, &key) == ERROR_SUCCESS) {
        // The subkey existing means the list is authoritative — even when empty
        // (web search disabled), or presets would resurrect on restart.
        data.stored = true;
        const DWORD count = (std::min<DWORD>)(ReadEngineDword(key, L"Count", 0),
            static_cast<DWORD>(kMaxSearchEngines));
        for (DWORD i = 0; i < count; ++i) {
            const std::wstring suffix = std::to_wstring(i);
            SearchEngine engine;
            wchar_t buf[512]{};
            DWORD size = sizeof(buf);
            if (RegGetValueW(key, nullptr, (L"Name" + suffix).c_str(), RRF_RT_REG_SZ,
                    nullptr, buf, &size) == ERROR_SUCCESS) {
                engine.name = buf;
            }
            size = sizeof(buf);
            if (RegGetValueW(key, nullptr, (L"Keyword" + suffix).c_str(), RRF_RT_REG_SZ,
                    nullptr, buf, &size) == ERROR_SUCCESS) {
                engine.keyword = buf;
            }
            size = sizeof(buf);
            if (RegGetValueW(key, nullptr, (L"Url" + suffix).c_str(), RRF_RT_REG_SZ,
                    nullptr, buf, &size) == ERROR_SUCCESS) {
                engine.url = buf;
            }
            if (!engine.name.empty() || !engine.url.empty()) {
                data.engines.push_back(std::move(engine));
            }
        }
        data.defaultEngine = static_cast<int>(ReadEngineDword(key, L"DefaultEngine", 0));
        RegCloseKey(key);
        return data;
    }
    // One-time migration from the pre-list single-URL setting. The old default
    // Google URL is dropped; a customized URL becomes one engine whose keyword
    // is the first letter of its host.
    if (!legacyUrl.empty() && legacyUrl != kLegacySearchEngineUrl) {
        const std::wstring host = ExtractHostname(legacyUrl);
        std::wstring keyword;
        for (wchar_t ch : host) {
            if (iswalnum(ch)) {
                keyword = static_cast<wchar_t>(towlower(ch));
                break;
            }
        }
        if (keyword.empty()) keyword = L"w";
        data.engines = {{host, keyword, legacyUrl}};
    }
    return data;
}

// Rewrites the whole list; indices beyond Count are cleared first so removed
// engines do not linger.
inline void SaveEnginesToRegistry(const std::vector<SearchEngine>& engines, int defaultEngine) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kEnginesRegistryKey, 0, nullptr, 0,
            KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
        return;
    }
    for (size_t i = 0; i < kMaxSearchEngines; ++i) {
        const std::wstring suffix = std::to_wstring(i);
        RegDeleteValueW(key, (L"Name" + suffix).c_str());
        RegDeleteValueW(key, (L"Keyword" + suffix).c_str());
        RegDeleteValueW(key, (L"Url" + suffix).c_str());
    }
    const DWORD count =
        static_cast<DWORD>((std::min<size_t>)(engines.size(), kMaxSearchEngines));
    RegSetValueExW(key, L"Count", 0, REG_DWORD,
        reinterpret_cast<const BYTE*>(&count), sizeof(count));
    const DWORD defaultIndex = static_cast<DWORD>(
        (std::max)(0, (std::min)(defaultEngine, static_cast<int>(count) - 1)));
    RegSetValueExW(key, L"DefaultEngine", 0, REG_DWORD,
        reinterpret_cast<const BYTE*>(&defaultIndex), sizeof(defaultIndex));
    for (size_t i = 0; i < count; ++i) {
        const std::wstring suffix = std::to_wstring(i);
        const auto write = [&](const wchar_t* prefix, const std::wstring& value) {
            RegSetValueExW(key, (std::wstring(prefix) + suffix).c_str(), 0, REG_SZ,
                reinterpret_cast<const BYTE*>(value.c_str()),
                static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
        };
        write(L"Name", engines[i].name);
        write(L"Keyword", engines[i].keyword);
        write(L"Url", engines[i].url);
    }
    RegCloseKey(key);
}

} // namespace takeoff
