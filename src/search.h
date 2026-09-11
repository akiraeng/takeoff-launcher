#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cwctype>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace takeoff {

enum class AppCategory : uint8_t {
    Application,
    System,
    File,
    Folder
};

inline std::wstring Normalize(std::wstring_view value) {
    std::wstring normalized;
    bool lastWasSpace = true;
    for (wchar_t ch : value) {
        if (iswalnum(ch)) {
            normalized.push_back(static_cast<wchar_t>(towlower(ch)));
            lastWasSpace = false;
        } else if (!lastWasSpace) {
            normalized.push_back(L' ');
            lastWasSpace = true;
        }
    }
    if (!normalized.empty() && normalized.back() == L' ') {
        normalized.pop_back();
    }
    return normalized;
}

inline std::wstring Condense(std::wstring_view value) {
    std::wstring condensed;
    condensed.reserve(value.size());
    for (wchar_t ch : value) {
        if (ch != L' ') condensed.push_back(ch);
    }
    return condensed;
}

inline bool MatchAcronym(std::wstring_view target, std::wstring_view query, bool& exact) {
    exact = false;
    if (query.empty() || target.empty()) return false;
    std::wstring initials;
    initials.push_back(target[0]);
    for (size_t i = 1; i < target.size(); ++i) {
        if (target[i - 1] == L' ' && target[i] != L' ') {
            initials.push_back(target[i]);
        }
    }
    if (query == initials) {
        exact = true;
        return true;
    }
    if (initials.rfind(query, 0) == 0) {
        exact = false;
        return true;
    }
    return false;
}

inline bool MatchTokens(std::wstring_view target, std::wstring_view query) {
    if (query.find(L' ') == std::wstring_view::npos) return false;
    size_t qStart = 0;
    size_t tPos = 0;
    while (qStart < query.size()) {
        size_t qEnd = query.find(L' ', qStart);
        if (qEnd == std::wstring_view::npos) qEnd = query.size();
        std::wstring_view token = query.substr(qStart, qEnd - qStart);
        if (token.empty()) {
            qStart = qEnd + 1;
            continue;
        }
        bool found = false;
        while (tPos < target.size()) {
            if (tPos == 0 || target[tPos - 1] == L' ') {
                if (target.substr(tPos).rfind(token, 0) == 0) {
                    found = true;
                    while (tPos < target.size() && target[tPos] != L' ') ++tPos;
                    if (tPos < target.size() && target[tPos] == L' ') ++tPos;
                    break;
                }
            }
            ++tPos;
        }
        if (!found) return false;
        qStart = qEnd + 1;
    }
    return true;
}

inline int MatchScore(std::wstring_view name, std::wstring_view query) {
    if (query.empty()) return -1;
    if (name == query) return 10000;

    const std::wstring queryCondensed = Condense(query);
    const std::wstring nameCondensed = Condense(name);
    if (!queryCondensed.empty() && queryCondensed == nameCondensed) {
        return 9500;
    }

    if (name.rfind(query, 0) == 0) {
        return 9000 - static_cast<int>(name.size() - query.size());
    }

    size_t position = 0;
    while ((position = name.find(query, position)) != std::wstring_view::npos) {
        if (position == 0 || name[position - 1] == L' ') {
            int wordIndex = 0;
            for (size_t k = 0; k < position; ++k) {
                if (name[k] == L' ') ++wordIndex;
            }
            return 8000 - wordIndex * 20 - static_cast<int>(name.size());
        }
        ++position;
    }

    bool exactAcronym = false;
    if (MatchAcronym(name, query, exactAcronym)) {
        return exactAcronym
            ? 7800 - static_cast<int>(name.size())
            : 7400 - static_cast<int>(name.size());
    }

    if (MatchTokens(name, query)) {
        return 7200 - static_cast<int>(name.size());
    }

    const size_t containedAt = name.find(query);
    if (containedAt != std::wstring_view::npos) {
        return 7000 - static_cast<int>(containedAt * 8 + name.size());
    }

    size_t queryIndex = 0;
    size_t previous = std::wstring_view::npos;
    int gaps = 0;
    int consecutive = 0;
    int bestConsecutive = 0;
    int boundaryHits = 0;
    for (size_t i = 0; i < name.size() && queryIndex < query.size(); ++i) {
        if (name[i] != query[queryIndex]) continue;
        if (i == 0 || name[i - 1] == L' ') {
            ++boundaryHits;
        }
        if (previous != std::wstring_view::npos) {
            gaps += static_cast<int>(i - previous - 1);
        }
        consecutive = previous != std::wstring_view::npos && i == previous + 1
            ? consecutive + 1 : 1;
        bestConsecutive = (std::max)(bestConsecutive, consecutive);
        previous = i;
        ++queryIndex;
    }
    return queryIndex == query.size()
        ? 5000 + bestConsecutive * 20 + boundaryHits * 50 - gaps * 5 - static_cast<int>(name.size())
        : -1;
}

inline int ScoreApp(
    std::wstring_view normalizedName,
    const std::vector<std::wstring>& aliases,
    std::wstring_view query,
    int recencyRank = -1
) {
    if (query.empty()) return -1;

    int best = MatchScore(normalizedName, query);

    for (const auto& alias : aliases) {
        const int s = MatchScore(alias, query);
        if (s >= 0) {
            const int adjusted = s >= 10000 ? 9500 : (s >= 8000 ? s - 600 : s - 1200);
            if (adjusted > best) best = adjusted;
        }
    }

    if (best >= 0 && recencyRank >= 0) {
        static constexpr int kRecencyBoost[] = {800, 650, 500, 400, 300, 200, 150, 100};
        if (recencyRank < static_cast<int>(sizeof(kRecencyBoost) / sizeof(kRecencyBoost[0]))) {
            best += kRecencyBoost[recencyRank];
        }
    }

    return best;
}

inline int ScoreFile(
    std::wstring_view normalizedName,
    std::wstring_view query,
    bool isDirectory = false
) {
    if (query.empty()) return -1;
    int s = MatchScore(normalizedName, query);
    if (s < 0) return -1;

    // Scale score to [1000 - 4000] range so matching applications (4500+)
    // will ALWAYS rank strictly above files and folders.
    int fileScore = 1000 + (s * 3000) / 10000;
    if (isDirectory) fileScore += 40;
    return fileScore;
}

// UTF-16 positions are shared with DirectWrite and the Windows clipboard.
class SearchInput {
public:
    static constexpr size_t kLimit = 1024;
    std::wstring text;
    size_t caret = 0;
    size_t anchor = 0;

    size_t Start() const { return (std::min)(caret, anchor); }
    size_t End() const { return (std::max)(caret, anchor); }
    bool HasSelection() const { return caret != anchor; }

    void Clear() { text.clear(); caret = anchor = 0; }
    void SelectAll() { anchor = 0; caret = text.size(); }

    size_t Previous(size_t position) const {
        if (position == 0) return 0;
        --position;
        if (position > 0 && text[position] >= 0xDC00 && text[position] <= 0xDFFF &&
            text[position - 1] >= 0xD800 && text[position - 1] <= 0xDBFF) {
            --position;
        }
        return position;
    }

    size_t Next(size_t position) const {
        if (position >= text.size()) return text.size();
        if (text[position] >= 0xD800 && text[position] <= 0xDBFF &&
            position + 1 < text.size() && text[position + 1] >= 0xDC00 &&
            text[position + 1] <= 0xDFFF) {
            return position + 2;
        }
        return position + 1;
    }

    void MoveTo(size_t position, bool selecting) {
        caret = (std::min)(position, text.size());
        // Never leave a caret between a UTF-16 surrogate pair.
        if (caret > 0 && caret < text.size() &&
            text[caret] >= 0xDC00 && text[caret] <= 0xDFFF &&
            text[caret - 1] >= 0xD800 && text[caret - 1] <= 0xDBFF) {
            --caret;
        }
        if (!selecting) anchor = caret;
    }

    void Move(bool right, bool selecting, bool word) {
        if (HasSelection() && !selecting && !word) {
            MoveTo(right ? End() : Start(), false);
            return;
        }
        size_t position = caret;
        if (word && right) {
            while (position < text.size() && !iswspace(text[position])) position = Next(position);
            while (position < text.size() && iswspace(text[position])) position = Next(position);
        } else if (word) {
            while (position > 0 && iswspace(text[Previous(position)])) position = Previous(position);
            while (position > 0 && !iswspace(text[Previous(position)])) position = Previous(position);
        } else {
            position = right ? Next(position) : Previous(position);
        }
        MoveTo(position, selecting);
    }

    void Insert(std::wstring_view value) {
        std::wstring clean;
        for (wchar_t ch : value) {
            if (ch >= L' ' && ch != 0x7F) clean.push_back(ch);
        }
        const size_t available = kLimit - (text.size() - (End() - Start()));
        if (clean.size() > available) {
            clean.resize(available);
            if (!clean.empty() && clean.back() >= 0xD800 && clean.back() <= 0xDBFF) {
                clean.pop_back();
            }
        }
        const size_t start = Start();
        text.replace(start, End() - start, clean);
        caret = anchor = start + clean.size();
    }

    void Erase(bool backward, bool word = false) {
        if (!HasSelection()) Move(!backward, true, word);
        Insert(L"");
    }
};

inline bool IsUninstaller(std::wstring_view name) {
    const std::wstring normalized = Normalize(name);
    return normalized.rfind(L"uninstall", 0) == 0 ||
           normalized.rfind(L"unins", 0) == 0 ||
           normalized.rfind(L"remove ", 0) == 0 ||
           normalized.find(L"uninstaller") != std::wstring::npos ||
           normalized == L"uninst";
}

inline bool IsHelperBinary(std::wstring_view name) {
    const std::wstring normalized = Normalize(name);
    return normalized == L"crashpad handler" ||
           normalized == L"crashpad_handler" ||
           normalized == L"squirrel" ||
           normalized == L"notification helper" ||
           normalized == L"notification_helper" ||
           normalized == L"elevate" ||
           normalized == L"installer" ||
           normalized == L"update";
}

inline bool IsLaunchableExtension(std::wstring_view ext) {
    std::wstring lower;
    lower.reserve(ext.size());
    for (wchar_t ch : ext) lower.push_back(static_cast<wchar_t>(towlower(ch)));
    return lower == L".lnk" || lower == L".exe" ||
           lower == L".appref-ms" || lower == L".url";
}

// Percent-encode query string for URL embedding.
inline std::wstring UrlEncode(std::wstring_view text) {
    if (text.empty()) return L"";
    const int utf8Len = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (utf8Len <= 0) return L"";
    std::string utf8(utf8Len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), utf8.data(), utf8Len, nullptr, nullptr);

    std::wstring encoded;
    encoded.reserve(utf8.size() * 3);
    for (unsigned char ch : utf8) {
        if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
            (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
            encoded.push_back(static_cast<wchar_t>(ch));
        } else if (ch == ' ') {
            encoded.push_back(L'+');
        } else {
            wchar_t hex[4];
            swprintf_s(hex, L"%%%02X", ch);
            encoded.append(hex);
        }
    }
    return encoded;
}

// Open web search in default browser for the given query.
inline bool OpenWebSearch(std::wstring_view query) {
    if (query.empty()) return false;
    const std::wstring url = L"https://www.google.com/search?q=" + UrlEncode(query);
    const INT_PTR result = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    return result > 32;
}

} // namespace takeoff

namespace quicklaunch = takeoff;
