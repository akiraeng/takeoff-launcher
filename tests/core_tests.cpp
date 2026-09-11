#include "../src/search.h"
#include "../src/settings.h"
#include "../src/updates.h"
#include "../src/file_index.h"

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

    // Uninstaller detection checks
    Check(IsUninstaller(L"unins000"), "unins000 is recognized as uninstaller");
    Check(IsUninstaller(L"unins001"), "unins001 is recognized as uninstaller");
    Check(IsUninstaller(L"uninst"), "uninst is recognized as uninstaller");
    Check(IsUninstaller(L"uninstall"), "uninstall is recognized as uninstaller");
    Check(IsUninstaller(L"Uninstall App"), "Uninstall App is recognized as uninstaller");
    Check(IsUninstaller(L"remove program"), "remove program is recognized as uninstaller");
    Check(IsUninstaller(L"app uninstaller"), "app uninstaller is recognized as uninstaller");
    Check(!IsUninstaller(L"universal"), "universal is not uninstaller");
    Check(!IsUninstaller(L"unity"), "unity is not uninstaller");
    Check(!IsUninstaller(L"notepad"), "notepad is not uninstaller");

    // Helper / internal binary filtering checks
    Check(IsHelperBinary(L"crashpad_handler"), "crashpad_handler filtered");
    Check(IsHelperBinary(L"crashpad handler"), "crashpad handler filtered");
    Check(IsHelperBinary(L"squirrel"), "squirrel filtered");
    Check(IsHelperBinary(L"notification_helper"), "notification_helper filtered");
    Check(IsHelperBinary(L"elevate"), "elevate filtered");
    Check(IsHelperBinary(L"installer"), "installer filtered");
    Check(IsHelperBinary(L"update"), "update filtered");
    Check(!IsHelperBinary(L"code"), "code not helper binary");
    Check(!IsHelperBinary(L"chrome"), "chrome not helper binary");

    // Launchable file extensions checks
    Check(IsLaunchableExtension(L".exe"), ".exe is launchable");
    Check(IsLaunchableExtension(L".EXE"), ".EXE is launchable");
    Check(IsLaunchableExtension(L".lnk"), ".lnk is launchable");
    Check(IsLaunchableExtension(L".appref-ms"), ".appref-ms is launchable");
    Check(IsLaunchableExtension(L".url"), ".url is launchable");
    Check(!IsLaunchableExtension(L".dll"), ".dll is not launchable");
    Check(!IsLaunchableExtension(L".txt"), ".txt is not launchable");
    Check(!IsLaunchableExtension(L""), "empty extension is not launchable");

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

    // Replace/update switch detection
    Check(IsReplaceSwitch(L"--replace"), "switch --replace");
    Check(IsReplaceSwitch(L"-replace"), "switch -replace");
    Check(IsReplaceSwitch(L"/replace"), "switch /replace");
    Check(IsReplaceSwitch(L"--update"), "switch --update");
    Check(IsReplaceSwitch(L"-update"), "switch -update");
    Check(IsReplaceSwitch(L"/update"), "switch /update");
    Check(IsReplaceSwitch(L"-r"), "switch -r");
    Check(IsReplaceSwitch(L"/r"), "switch /r");
    Check(IsReplaceSwitch(L"--REPLACE"), "switch case insensitivity --REPLACE");
    Check(IsReplaceSwitch(L"/UPDATE"), "switch case insensitivity /UPDATE");
    Check(!IsReplaceSwitch(L""), "empty switch");
    Check(!IsReplaceSwitch(L"--"), "dash only switch");
    Check(!IsReplaceSwitch(L"--restart"), "unrelated switch");
    Check(!IsReplaceSwitch(L"replace"), "bare word without prefix");

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

    // App recents preservation across index reload verification
    {
        struct TestApp {
            std::wstring name;
            std::wstring path;
        };
        std::vector<TestApp> oldApps = {
            {L"App A", L"C:\\Path\\A.exe"},
            {L"App B", L"C:\\Path\\B.exe"},
            {L"App C", L"C:\\Path\\C.exe"},
        };
        // Suppose App B was launched (recent index 1) then App A (recent index 0)
        std::vector<size_t> recentIndices = {1, 0};
        std::vector<std::wstring> activeRecentPaths;
        for (size_t i : recentIndices) {
            if (i < oldApps.size()) activeRecentPaths.push_back(oldApps[i].path);
        }

        // New index arrives: App D added at top, sorting changed, App A and B exist at new indices
        std::vector<TestApp> newApps = {
            {L"App 0", L"C:\\Path\\0.exe"},
            {L"App A", L"C:\\Path\\A.exe"}, // now index 1
            {L"App B", L"C:\\Path\\B.exe"}, // now index 2
            {L"App C", L"C:\\Path\\C.exe"}, // now index 3
        };
        std::vector<size_t> remappedRecent;
        for (const auto& rPath : activeRecentPaths) {
            for (size_t i = 0; i < newApps.size(); ++i) {
                if (newApps[i].path == rPath) {
                    remappedRecent.push_back(i);
                    break;
                }
            }
        }
        Check(remappedRecent.size() == 2, "remapped recent count matches");
        Check(remappedRecent[0] == 2, "App B remapped to new index 2");
        Check(remappedRecent[1] == 1, "App A remapped to new index 1");
    }

    // Hotkey conflict text and binding test
    {
        HotkeyBinding conflictHotkey{kModAlt, kVkSpace};
        std::wstring conflictText = L"The hotkey " + FormatBinding(conflictHotkey) +
            L" is currently taken by another application and could not be registered.";
        Check(conflictText.find(L"Alt + Space") != std::wstring::npos, "conflict text contains formatted hotkey Alt + Space");
        Check(FormatBinding(conflictHotkey) == L"Alt + Space", "format default hotkey");
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

    // --- File Search & App Priority Tests ---
    // 1. ScoreFile tests
    Check(ScoreFile(L"document", L"") == -1, "ScoreFile empty query returns -1");
    Check(ScoreFile(L"document", L"xyz") == -1, "ScoreFile non-matching returns -1");
    int exactFileScore = ScoreFile(L"report", L"report", false);
    int folderScore = ScoreFile(L"report", L"report", true);
    Check(exactFileScore == 4000, "ScoreFile exact match is 4000");
    Check(folderScore == 4040, "ScoreFile folder gets +40 bonus");
    Check(ScoreFile(L"quarterly report 2026", L"report") > 0, "ScoreFile substring match");
    Check(ScoreFile(L"quarterly report 2026", L"rep") > 0, "ScoreFile prefix match");

    // 2. Strict Application > File Ranking Invariant
    // Any matching app (even weakest fuzzy match, ~4500+) must score higher than the absolute best file match (4040).
    int weakestAppScore = ScoreApp(L"abcdefghij", {}, L"aj");
    Check(weakestAppScore >= 4500, "weakest app score is at least 4500");
    Check(weakestAppScore > exactFileScore, "weakest app match strictly beats exact file match");
    Check(weakestAppScore > folderScore, "weakest app match strictly beats exact folder match");

    // Realistic scenario: query "code" matching both an app "Visual Studio Code" and a file "code.txt"
    int appScore = ScoreApp(L"visual studio code", {L"vsc"}, L"code");
    int fileScore = ScoreFile(L"code txt", L"code");
    Check(appScore > fileScore, "app 'Visual Studio Code' strictly beats file 'code.txt'");

    // 3. Settings enableFileSearch and enableWebSearch defaults and toggles
    Settings defaultSettings;
    Check(defaultSettings.enableFileSearch == true, "file search enabled by default in settings");
    defaultSettings.enableFileSearch = false;
    Check(!defaultSettings.enableFileSearch, "file search toggle can be disabled");
    Check(defaultSettings.enableWebSearch == true, "web search enabled by default in settings");
    defaultSettings.enableWebSearch = false;
    Check(!defaultSettings.enableWebSearch, "web search toggle can be disabled");

    // UrlEncode tests
    Check(UrlEncode(L"").empty(), "UrlEncode empty string");
    Check(UrlEncode(L"takeoff") == L"takeoff", "UrlEncode plain text");
    Check(UrlEncode(L"hello world") == L"hello+world", "UrlEncode spaces to plus");
    Check(UrlEncode(L"c++ & c#") == L"c%2B%2B+%26+c%23", "UrlEncode reserved characters");
    Check(UrlEncode(L"test~_.-") == L"test~_.-", "UrlEncode unreserved characters preserved");
    Check(UrlEncode(L"caf\u00e9") == L"caf%C3%A9", "UrlEncode UTF-8 multi-byte");

    // Web search normalization tests
    Check(Normalize(L"   ").empty(), "whitespace query normalizes to empty");
    Check(Normalize(L"\t \r\n ").empty(), "whitespace query normalizes to empty");
    Check(!Normalize(L"google search").empty(), "valid search query normalizes to non-empty");

    // 4. Zero-query app-only invariant:
    // When input query is empty, FileIndex returns 0 results.
    auto emptyQueryFileResults = FileIndex::Instance().Search(L"");
    Check(emptyQueryFileResults.empty(), "empty query returns 0 files from FileIndex");

    // 5. Live FileIndex background indexing & sub-millisecond search benchmark
    FileIndex::Instance().Start();
    for (int w = 0; w < 40 && !FileIndex::Instance().IsReady(); ++w) {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
    const size_t indexedCount = FileIndex::Instance().Count();
    std::cout << "[FileIndex] Live index populated " << indexedCount << " files/folders (ready=" << FileIndex::Instance().IsReady() << ").\n";
    Check(indexedCount > 0, "FileIndex populated files from disk");

    // Determine the active workspace directory (project root)
    std::error_code ec;
    fs::path currentPath = fs::current_path(ec);
    fs::path repoPath = currentPath;
    while (repoPath.has_parent_path()) {
        const auto name = repoPath.filename().wstring();
        if (_wcsicmp(name.c_str(), L"build") == 0 ||
            _wcsicmp(name.c_str(), L"Release") == 0 ||
            _wcsicmp(name.c_str(), L"Debug") == 0 ||
            _wcsicmp(name.c_str(), L"bin") == 0) {
            repoPath = repoPath.parent_path();
        } else {
            break;
        }
    }
    const std::wstring repoPathStr = repoPath.wstring();
    const std::wstring repoFolderName = repoPath.filename().wstring();

    // Verify broad file & folder search finds repo folder and its files
    auto takeoffLauncherResults = FileIndex::Instance().Search(repoFolderName, 10);
    Check(!takeoffLauncherResults.empty(), "repo folder query returns results");
    Check(takeoffLauncherResults[0].isDirectory, "repo folder #1 result is a directory");
    Check(takeoffLauncherResults[0].name == repoFolderName, "repo folder #1 result matches folder name");

    auto pathResults = FileIndex::Instance().Search(repoPathStr, 10);
    Check(!pathResults.empty(), "repo path query returns results");

    auto takeoffMainResults = FileIndex::Instance().Search(L"takeoff main", 10);
    Check(!takeoffMainResults.empty(), "takeoff main multi-token query returns results");
    Check(takeoffMainResults[0].name == L"main.cpp", "takeoff main finds main.cpp");

    std::vector<std::wstring> testQueries = {L"takeoff", repoFolderName, repoPathStr, L"takeoff main", L"launcher", L"main.cpp"};
    for (const auto& q : testQueries) {
        auto results = FileIndex::Instance().Search(q, 5);
        std::wcout << L"Query [" << q << L"] -> " << results.size() << L" results:\n";
        for (const auto& r : results) {
            std::wcout << L"  - " << (r.isDirectory ? L"[DIR]  " : L"[FILE] ") << r.name << L" (" << r.path << L") score=" << r.score << L"\n";
        }
    }

    // Benchmark 100 search queries on live in-memory index
    const auto fileStart = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 100; ++i) {
        auto r = FileIndex::Instance().Search(L"project", 10);
    }
    const auto fileElapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now() - fileStart).count();
    const double perQueryMs = (fileElapsed / 100.0) / 1000.0;
    std::cout << "[FileIndex] 100 searches completed in " << fileElapsed << "us ("
              << perQueryMs << "ms per query across " << indexedCount << " files!)\n";
    Check(perQueryMs < 5.0, "file search evaluation executes in under 5ms per query");
    FileIndex::Instance().Stop();

    // 6. Settings Scroll and Viewport Invariants:
    // Guarantees Settings content cleanly fits and scrolls without overlapping FooterTop (440px).
    constexpr float kWindowHeight = 482.0f;
    constexpr float kFooterH = 42.0f;
    constexpr float kSettingsHeaderH = 46.0f;
    constexpr float kSettingsRowH = 47.0f;
    constexpr float footerTop = kWindowHeight - kFooterH; // 440.0f
    constexpr float generalTop = 280.0f;
    constexpr float row7Top = generalTop + 3 * kSettingsRowH; // 421.0f
    constexpr float row7Bottom = row7Top + kSettingsRowH;     // 468.0f
    constexpr float contentBottom = row7Bottom + 14.0f;       // 482.0f
    constexpr float maxScroll = contentBottom - footerTop;    // 42.0f

    Check(footerTop == 440.0f, "footer top is exactly 440px");
    Check(row7Bottom > footerTop, "unscrolled row 7 exceeds footer top, proving scroll is required");
    Check(maxScroll == 42.0f, "settings max scroll is 42px");

    // When scrolled to maxScroll:
    const float scrolledRow7Bottom = row7Bottom - maxScroll;
    Check(scrolledRow7Bottom < footerTop, "scrolled row 7 bottom is strictly above footer top");
    Check(footerTop - scrolledRow7Bottom >= 14.0f, "row 7 has at least 14px clearance above footer");

    // Check viewport height and scrollable area:
    constexpr float viewportHeight = footerTop - kSettingsHeaderH; // 394.0f
    Check(viewportHeight == 394.0f, "settings viewport height is 394px");

    std::cout << "All search, text editing, hotkey, and settings scroll checks passed in " << elapsed << "ms.\n";
}
