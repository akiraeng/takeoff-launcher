#pragma once

#include "pinyin_data.h"

#include <algorithm>
#include <cstdint>
#include <cwctype>
#include <string>
#include <string_view>
#include <vector>

namespace takeoff {

inline constexpr bool IsChineseChar(wchar_t ch) noexcept {
    return (ch >= 0x4E00 && ch <= 0x9FFF) || ch == 0x3007;
}

inline bool HasChinese(std::wstring_view text) noexcept {
    for (wchar_t ch : text) {
        if (IsChineseChar(ch)) return true;
    }
    return false;
}

struct CharPinyins {
    std::string_view items[4];
    size_t count = 0;
};

inline CharPinyins GetPinyinsForChar(wchar_t ch) noexcept {
    CharPinyins res;
    if (ch == 0x3007) {
        if (pinyin_data::kLingSyllableId < pinyin_data::kSyllableCount) {
            res.items[0] = pinyin_data::kSyllables[pinyin_data::kLingSyllableId];
            res.count = 1;
        }
        return res;
    }
    if (ch < 0x4E00 || ch > 0x9FFF) return res;

    const uint16_t offset = static_cast<uint16_t>(ch - 0x4E00);
    const uint16_t primaryId = pinyin_data::kPrimaryPinyin[offset];
    if (primaryId > 0 && primaryId < pinyin_data::kSyllableCount) {
        res.items[res.count++] = pinyin_data::kSyllables[primaryId];
    }

    // Polyphone alternatives
    auto it = std::lower_bound(
        std::begin(pinyin_data::kPolyphones),
        std::end(pinyin_data::kPolyphones),
        offset,
        [](const pinyin_data::PolyphoneEntry& entry, uint16_t off) {
            return entry.offset < off;
        }
    );
    if (it != std::end(pinyin_data::kPolyphones) && it->offset == offset) {
        for (int i = 0; i < 3; ++i) {
            const uint16_t altId = it->alts[i];
            if (altId > 0 && altId < pinyin_data::kSyllableCount) {
                std::string_view altS = pinyin_data::kSyllables[altId];
                bool exists = false;
                for (size_t k = 0; k < res.count; ++k) {
                    if (res.items[k] == altS) { exists = true; break; }
                }
                if (!exists && res.count < 4) {
                    res.items[res.count++] = altS;
                }
            }
        }
    }
    return res;
}

namespace detail {

struct PhoneticToken {
    std::vector<std::wstring> syllables;
    bool isChinese = false;
};

inline std::vector<PhoneticToken> TokenizeForPinyin(std::wstring_view text) {
    std::vector<PhoneticToken> tokens;
    size_t i = 0;
    while (i < text.size()) {
        const wchar_t ch = text[i];
        if (IsChineseChar(ch)) {
            CharPinyins cp = GetPinyinsForChar(ch);
            PhoneticToken token;
            token.isChinese = true;
            if (cp.count > 0) {
                // Keep at most 2 distinct syllables for combinations
                const size_t take = (std::min)(cp.count, size_t{2});
                for (size_t k = 0; k < take; ++k) {
                    std::wstring ws;
                    ws.reserve(cp.items[k].size());
                    for (char c : cp.items[k]) ws.push_back(static_cast<wchar_t>(c));
                    if (std::find(token.syllables.begin(), token.syllables.end(), ws) == token.syllables.end()) {
                        token.syllables.push_back(std::move(ws));
                    }
                }
            }
            if (token.syllables.empty()) {
                token.syllables.push_back(L"");
            }
            tokens.push_back(std::move(token));
            ++i;
        } else if (iswalnum(ch) && ch < 128) {
            size_t start = i;
            while (i < text.size() && iswalnum(text[i]) && text[i] < 128) {
                ++i;
            }
            std::wstring chunk;
            chunk.reserve(i - start);
            for (size_t k = start; k < i; ++k) {
                chunk.push_back(static_cast<wchar_t>(towlower(text[k])));
            }
            PhoneticToken token;
            token.isChinese = false;
            token.syllables.push_back(std::move(chunk));
            tokens.push_back(std::move(token));
        } else {
            ++i;
        }
    }
    return tokens;
}

inline void ExpandTokenCombos(
    const std::vector<PhoneticToken>& tokens,
    std::vector<std::wstring>& outInitials,
    std::vector<std::wstring>& outFullWithSpaces) {
    if (tokens.empty()) return;

    outInitials = {L""};
    outFullWithSpaces = {L""};

    for (const auto& token : tokens) {
        std::vector<std::wstring> nextInitials;
        std::vector<std::wstring> nextFull;

        for (const auto& syl : token.syllables) {
            std::wstring initStr;
            if (token.isChinese) {
                if (!syl.empty()) initStr.push_back(syl[0]);
            } else {
                initStr = syl;
            }

            for (const auto& prefix : outInitials) {
                nextInitials.push_back(prefix + initStr);
            }
            for (const auto& prefix : outFullWithSpaces) {
                if (prefix.empty()) {
                    nextFull.push_back(syl);
                } else {
                    nextFull.push_back(prefix + L" " + syl);
                }
            }
        }

        // Limit branching to 4 combinations at each stage
        if (nextInitials.size() > 4) nextInitials.resize(4);
        if (nextFull.size() > 4) nextFull.resize(4);

        outInitials = std::move(nextInitials);
        outFullWithSpaces = std::move(nextFull);
    }
}

inline void CollectAliasesForSegment(std::wstring_view segment, std::vector<std::wstring>& results) {
    auto tokens = TokenizeForPinyin(segment);
    bool hasCn = false;
    for (const auto& t : tokens) {
        if (t.isChinese) { hasCn = true; break; }
    }
    if (!hasCn) return;

    std::vector<std::wstring> initials;
    std::vector<std::wstring> fullWithSpaces;
    ExpandTokenCombos(tokens, initials, fullWithSpaces);

    auto addUnique = [&](std::wstring s) {
        if (s.empty()) return;
        if (std::find(results.begin(), results.end(), s) == results.end()) {
            results.push_back(std::move(s));
        }
    };

    for (auto& init : initials) {
        addUnique(std::move(init));
    }
    for (auto& full : fullWithSpaces) {
        std::wstring condensed;
        condensed.reserve(full.size());
        for (wchar_t c : full) {
            if (c != L' ') condensed.push_back(c);
        }
        addUnique(full);
        addUnique(std::move(condensed));
    }
}

} // namespace detail

inline std::vector<std::wstring> GeneratePinyinAliases(std::wstring_view name) {
    if (!HasChinese(name)) return {};

    std::vector<std::wstring> results;
    results.reserve(16);

    // 1. Full name aliases
    detail::CollectAliasesForSegment(name, results);

    // 2. Base name before delimiter / parentheses if it contains Chinese
    const size_t delimPos = name.find_first_of(L"([{-_");
    if (delimPos != std::wstring_view::npos && delimPos > 0) {
        std::wstring_view base = name.substr(0, delimPos);
        while (!base.empty() && iswspace(base.back())) {
            base.remove_suffix(1);
        }
        if (HasChinese(base)) {
            detail::CollectAliasesForSegment(base, results);
        }
    }

    // 3. Contiguous Chinese character chunks (length >= 2) for mixed-language names
    // (e.g. "AutoCAD 中文版" -> "中文版", "Visual Studio 社区中文版" -> "社区中文版")
    size_t i = 0;
    while (i < name.size()) {
        if (IsChineseChar(name[i])) {
            size_t start = i;
            while (i < name.size() && IsChineseChar(name[i])) {
                ++i;
            }
            if (i - start >= 2 && (start > 0 || i < name.size())) {
                std::wstring_view chunk = name.substr(start, i - start);
                detail::CollectAliasesForSegment(chunk, results);
            }
        } else {
            ++i;
        }
    }

    // Limit maximum aliases to keep memory compact
    if (results.size() > 16) {
        results.resize(16);
    }

    return results;
}

} // namespace takeoff
