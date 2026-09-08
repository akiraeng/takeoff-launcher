#include "../src/search.h"
#include "../src/settings.h"
#include "../src/updates.h"

#include <chrono>
#include <cstdlib>
#include <iostream>

void Check(bool condition, const char* description) {
    if (!condition) {
        std::cerr << "FAIL: " << description << '\n';
        std::exit(1);
    }
}

int main() {
    using namespace takeoff;
    Check(Normalize(L"  Visual-Studio.Code! ") == L"visual studio code", "normalization");
    Check(Normalize(L"!!!").empty(), "punctuation-only query");
    Check(MatchScore(L"code", L"code") > MatchScore(L"code editor", L"code"), "exact first");
    Check(MatchScore(L"code editor", L"code") > MatchScore(L"visual code", L"code"), "prefix first");
    Check(MatchScore(L"visual studio code", L"vsc") > 0, "fuzzy match");
    Check(MatchScore(L"notepad", L"xyz") == -1, "no match");
    Check(MatchScore(L"notepad", L"") == -1, "empty query");

    // Acronym and initials matching
    Check(MatchScore(L"visual studio code", L"vsc") > 0, "acronym vsc");
    Check(MatchScore(L"task manager", L"tm") > 0, "acronym tm");
    Check(MatchScore(L"control panel", L"cp") > 0, "acronym cp");
    Check(MatchScore(L"windows update", L"wu") > 0, "acronym wu");
    Check(MatchScore(L"device manager", L"dm") > 0, "acronym dm");

    // Multi-token word prefix matching
    Check(MatchScore(L"visual studio code", L"vs code") > 0, "multi-token vs code");
    Check(MatchScore(L"windows update", L"win upd") > 0, "multi-token win upd");
    Check(MatchScore(L"task manager", L"task man") > 0, "multi-token task man");

    // Word boundary vs substring
    Check(MatchScore(L"visual studio code", L"code") > MatchScore(L"barcode scanner", L"code"),
        "word boundary beats inside word");

    // Condensed match
    Check(MatchScore(L"vs code", L"vscode") >= 9500, "condensed match");

    // Aliases and ScoreApp
    Check(ScoreApp(L"command prompt", {L"cmd"}, L"cmd") >= 9500, "alias exact match for cmd");
    Check(ScoreApp(L"task manager", {L"taskmgr", L"processes"}, L"taskmgr") >= 9500, "alias taskmgr");
    Check(ScoreApp(L"windows update", {L"update"}, L"update") >= 9500, "alias update");
    Check(ScoreApp(L"services", {L"daemon"}, L"daemon") > 0, "alias daemon");

    // Recency ranking priority
    Check(ScoreApp(L"terminal", {}, L"term", 0) > ScoreApp(L"terminal", {}, L"term", 1),
        "most recent ranks higher than second recent");
    Check(ScoreApp(L"terminal", {}, L"term", 1) > ScoreApp(L"terminal", {}, L"term", -1),
        "recent ranks higher than non-recent");

    // AppCategory distinction
    Check(AppCategory::System != AppCategory::Application, "categories are distinct");

    SearchInput input;
    input.Insert(L"hello world");
    input.Move(false, false, true);
    Check(input.caret == 6, "previous word");
    input.Insert(L"new ");
    Check(input.text == L"hello new world", "insert in middle");
    input.Erase(true, true);
    Check(input.text == L"hello world", "delete previous word");
    input.MoveTo(0, false);
    input.Move(true, true, true);
    Check(input.Start() == 0 && input.End() == 6, "word selection");
    input.Insert(L"goodbye ");
    Check(input.text == L"goodbye world", "replace selection");
    input.MoveTo(0, false);
    input.Erase(false);
    Check(input.text == L"oodbye world", "forward delete");
    input.SelectAll();
    input.Insert(L"abc\r\n\tdef");
    Check(input.text == L"abcdef", "single-line paste");
    input.MoveTo(2, false);
    input.MoveTo(5, true);
    input.Move(false, false, false);
    Check(input.caret == 2 && !input.HasSelection(), "collapse selection left");
    input.SelectAll();
    input.Erase(true);
    Check(input.text.empty() && input.caret == 0, "erase all");
    input.Erase(true);
    input.Erase(false);
    Check(input.text.empty(), "empty deletion");
    input.Insert(L"a\xD83D\xDE00z");
    input.Move(false, false, false);
    input.Move(false, false, false);
    Check(input.caret == 1, "move across surrogate pair");
    input.Erase(false);
    Check(input.text == L"az", "delete surrogate pair");
    input.Clear();
    input.Insert(std::wstring(2048, L'x'));
    Check(input.text.size() == SearchInput::kLimit, "input length limit");
    input.SelectAll();
    input.Insert(L"replacement");
    Check(input.text == L"replacement", "replace at limit");

    Settings settings;
    Check(FormatBinding(settings.launcherHotkey) == L"Alt + Space",
        "default launcher hotkey");
    Check(FormatBinding(settings.actionsHotkey) == L"Ctrl + K",
        "default actions hotkey");
    Check(FormatAdminBinding(settings.administratorHotkey) == L"Ctrl + Enter",
        "default administrator hotkey");
    Check(FormatQuickLaunchBinding(settings.quickLaunchHotkey) == L"Alt + 1\u20138",
        "default quick launch hotkey");
    Check(settings.showTrayIcon,
        "notification area icon enabled by default");
    Check(settings.checkForUpdates,
        "automatic update checking enabled by default");

    // Custom bindings formatting
    Check(FormatBinding({kModControl | kModShift, 'P'}) == L"Ctrl + Shift + P",
        "format Ctrl+Shift+P");
    Check(FormatBinding({kModControl | kModAlt, 'D'}) == L"Ctrl + Alt + D",
        "format Ctrl+Alt+D");
    Check(FormatBinding({kModAlt, 0x70}) == L"Alt + F1",
        "format Alt+F1");
    Check(FormatBinding({0, 0, true}) == L"Disabled",
        "format disabled binding");
    Check(FormatAdminBinding({kModControl | kModShift, 0}) == L"Ctrl + Shift + Enter",
        "format admin Ctrl+Shift+Enter");
    Check(FormatAdminBinding({0, 0, true}) == L"Disabled",
        "format disabled admin binding");
    Check(FormatQuickLaunchBinding({kModControl | kModAlt, 0}) == L"Ctrl + Alt + 1\u20138",
        "format quick launch Ctrl+Alt+1-8");
    Check(FormatQuickLaunchBinding({0, 0, true}) == L"Disabled",
        "format disabled quick launch");

    // Reserved in-app keys
    Check(IsReservedInApp({kModControl, 'C'}), "Ctrl+C reserved");
    Check(IsReservedInApp({kModControl, 'V'}), "Ctrl+V reserved");
    Check(IsReservedInApp({kModControl, 'A'}), "Ctrl+A reserved");
    Check(IsReservedInApp({kModControl, 'X'}), "Ctrl+X reserved");
    Check(IsReservedInApp({kModControl, 'Z'}), "Ctrl+Z reserved");
    Check(!IsReservedInApp({kModControl, 'K'}), "Ctrl+K allowed");
    Check(!IsReservedInApp({kModControl | kModShift, 'C'}), "Ctrl+Shift+C allowed");
    Check(IsReservedInApp({0, 'A'}), "bare key reserved");

    // Equality and migration
    Check(MigrateLauncherHotkey(0) == HotkeyBinding{kModAlt, kVkSpace},
        "migrate default launcher hotkey");
    Check(MigrateActionsHotkey(0) == HotkeyBinding{kModControl, 'K'},
        "migrate default actions hotkey");
    Check(MigrateAdminHotkey(0) == HotkeyBinding{kModControl, 0},
        "migrate default admin hotkey");
    Check(MigrateQuickLaunchHotkey(0) == HotkeyBinding{kModAlt, 0},
        "migrate default quick launch hotkey");
    Check(MigrateAdminHotkey(3).disabled, "migrate disabled admin hotkey");
    // System reserved keys
    Check(IsSystemReserved(kModAlt, 0x73), "Alt+F4 is system reserved");
    Check(IsSystemReserved(kModAlt, 0x09), "Alt+Tab is system reserved");
    Check(IsSystemReserved(kModControl | kModShift, 0x1B), "Ctrl+Shift+Esc is system reserved");
    Check(!IsSystemReserved(kModAlt, kVkSpace), "Alt+Space is not system reserved");
    Check(!IsSystemReserved(kModControl, 'K'), "Ctrl+K is not system reserved");

    // Internal conflict detection
    Check(HasInternalConflict(0, {kModControl, 'K'}, settings),
        "launcher conflicts with actions menu");
    Check(HasInternalConflict(0, {kModControl, kVkReturn}, settings),
        "launcher conflicts with admin hotkey");
    Check(HasInternalConflict(0, {kModAlt, '3'}, settings),
        "launcher conflicts with quick launch hotkey");
    Check(!HasInternalConflict(0, {kModControl | kModAlt, 'J'}, settings),
        "distinct launcher combo has no conflict");
    Check(HasInternalConflict(1, {kModAlt, kVkSpace}, settings),
        "actions conflicts with launcher hotkey");
    Check(HasInternalConflict(1, {kModAlt, '2'}, settings),
        "actions conflicts with quick launch");
    Settings enterSettings;
    enterSettings.actionsHotkey = {kModAlt, kVkReturn};
    Check(HasInternalConflict(2, {kModAlt, 0}, enterSettings),
        "admin conflicts if actions is on Enter with same modifier");

    Settings digitSettings;
    digitSettings.actionsHotkey = {kModControl, '5'};
    Check(HasInternalConflict(3, {kModControl, 0}, digitSettings),
        "quick launch conflicts if actions is on 1-8 with same modifier");

    // Minimized/startup switch detection
    Check(IsMinimizedSwitch(L"--minimized"), "switch --minimized");
    Check(IsMinimizedSwitch(L"-minimized"), "switch -minimized");
    Check(IsMinimizedSwitch(L"/minimized"), "switch /minimized");
    Check(IsMinimizedSwitch(L"--startup"), "switch --startup");
    Check(IsMinimizedSwitch(L"-startup"), "switch -startup");
    Check(IsMinimizedSwitch(L"/startup"), "switch /startup");
    Check(IsMinimizedSwitch(L"--hidden"), "switch --hidden");
    Check(IsMinimizedSwitch(L"-hidden"), "switch -hidden");
    Check(IsMinimizedSwitch(L"/hidden"), "switch /hidden");
    Check(IsMinimizedSwitch(L"-m"), "switch -m");
    Check(IsMinimizedSwitch(L"/m"), "switch /m");
    Check(IsMinimizedSwitch(L"--MINIMIZED"), "switch case insensitivity");
    Check(IsMinimizedSwitch(L"/STARTUP"), "switch case insensitivity /STARTUP");
    Check(!IsMinimizedSwitch(L""), "empty switch");
    Check(!IsMinimizedSwitch(L"--"), "dash only switch");
    Check(!IsMinimizedSwitch(L"--maximized"), "unrelated switch");
    Check(!IsMinimizedSwitch(L"minimized"), "bare word without prefix");

    // Update parsing and version comparison checks
    Check(ExtractTagName("{\"tag_name\":\"v1.2.3\"}") == L"v1.2.3", "extract tag_name basic");
    Check(ExtractTagName("{\"id\":10, \"tag_name\" : \"2.0.0\"}") == L"2.0.0", "extract tag_name with whitespace");
    Check(ExtractTagName("{\"message\":\"Not Found\"}").empty(), "extract tag_name not found");
    Check(ExtractTagName("").empty(), "extract tag_name empty");

    Check(IsNewerVersion(L"v1.0.1", L"1.0.0"), "v1.0.1 is newer than 1.0.0");
    Check(IsNewerVersion(L"1.1.0", L"1.0.0"), "1.1.0 is newer than 1.0.0");
    Check(IsNewerVersion(L"v2.0", L"1.9.9"), "v2.0 is newer than 1.9.9");
    Check(IsNewerVersion(L"1.0.0.1", L"1.0.0.0"), "1.0.0.1 is newer than 1.0.0.0");
    Check(!IsNewerVersion(L"1.0.0", L"1.0.0"), "1.0.0 is not newer than 1.0.0");
    Check(!IsNewerVersion(L"v1.0", L"1.0.0"), "v1.0 is not newer than 1.0.0");
    Check(!IsNewerVersion(L"0.9.9", L"1.0.0"), "0.9.9 is not newer than 1.0.0");
    Check(!IsNewerVersion(L"v1.0.0-beta", L"1.0.0"), "v1.0.0-beta is not newer than 1.0.0");

    // 24-hour update interval logic checks
    Check(ShouldCheckForUpdates(0, 100000, true), "check when never checked before");
    Check(!ShouldCheckForUpdates(100000, 100000 + 3600, true), "do not check after only 1 hour");
    Check(ShouldCheckForUpdates(100000, 100000 + 86400, true), "check when exactly 24 hours have passed");
    Check(ShouldCheckForUpdates(100000, 100000 + 100000, true), "check when more than 24 hours have passed");
    Check(!ShouldCheckForUpdates(100000, 100000 + 100000, false), "do not check when disabled");
    Check(ShouldCheckForUpdates(200000, 100000, true), "check when system clock shifted backwards");

    // Live WinHTTP GitHub query verification
    std::wstring liveTag, liveUrl;
    if (QueryLatestReleaseTag(L"api.github.com", L"/repos/microsoft/terminal/releases/latest", liveTag, liveUrl)) {
        Check(!liveTag.empty(), "live GitHub query returned a release tag");
        Check(IsNewerVersion(liveTag, kAppVersion), "live release tag is newer than current 1.0.0");
        std::wcout << L"[LIVE TEST] Successfully queried GitHub API! Latest release: " 
                  << liveTag << L'\n';
    }

    // Performance check: 10,000 matches must execute in under 100ms
    const auto start = std::chrono::high_resolution_clock::now();
    int sum = 0;
    const std::vector<std::wstring> aliases = {L"taskmgr", L"processes", L"kill", L"tm"};
    for (int i = 0; i < 10000; ++i) {
        sum += ScoreApp(L"task manager", aliases, L"tm", i % 8);
        sum += ScoreApp(L"visual studio code", {L"vsc", L"code"}, L"vsc", -1);
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::high_resolution_clock::now() - start).count();
    Check(sum > 0, "benchmark computed positive score");
    Check(elapsed < 100, "10,000 matches executed in under 100ms");

    std::cout << "All search, text editing, and hotkey checks passed in " << elapsed << "ms.\n";
}
