#pragma once

#include "search.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cwctype>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
#include <windows.h>
#include <shlobj.h>

namespace takeoff {

namespace fs = std::filesystem;

constexpr UINT kFilesReadyMessage = WM_APP + 8;

struct FileItem {
    std::wstring name;
    std::wstring normName;
    std::wstring path;
    std::wstring normPath;
    bool isDirectory = false;
};

struct FileSearchResult {
    std::wstring name;
    std::wstring path;
    bool isDirectory = false;
    int score = 0;
};

class FileIndex {
public:
    static FileIndex& Instance() {
        static FileIndex s_instance;
        return s_instance;
    }

    void Start(HWND notifyHwnd = nullptr) {
        if (running_.exchange(true)) return;
        notifyHwnd_ = notifyHwnd;
        stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        triggerEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        worker_ = std::thread([this]() { WorkerLoop(); });
    }

    void Stop() {
        if (!running_.exchange(false)) return;
        if (stopEvent_) SetEvent(stopEvent_);
        if (worker_.joinable()) {
            worker_.join();
        }
        if (stopEvent_) {
            CloseHandle(stopEvent_);
            stopEvent_ = nullptr;
        }
        if (triggerEvent_) {
            CloseHandle(triggerEvent_);
            triggerEvent_ = nullptr;
        }
    }

    void TriggerReindex() {
        if (triggerEvent_) SetEvent(triggerEvent_);
    }

    bool IsReady() const {
        return ready_.load();
    }

    size_t Count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return index_ ? index_->size() : 0;
    }

    // Ultra-fast in-memory search across filenames and directory paths
    std::vector<FileSearchResult> Search(std::wstring_view query, size_t maxResults = 30) const {
        if (query.empty()) return {};

        std::shared_ptr<const std::vector<FileItem>> index;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            index = index_;
        }
        if (!index || index->empty()) return {};

        const std::wstring normQuery = Normalize(query);
        if (normQuery.empty()) return {};

        // Parse query tokens
        std::vector<std::wstring_view> tokens;
        size_t tStart = 0;
        while (tStart < normQuery.size()) {
            size_t tEnd = normQuery.find(L' ', tStart);
            if (tEnd == std::wstring::npos) tEnd = normQuery.size();
            if (tEnd > tStart) {
                tokens.push_back(std::wstring_view(normQuery.data() + tStart, tEnd - tStart));
            }
            tStart = tEnd + 1;
        }

        struct Candidate {
            int score;
            const FileItem* item;
        };
        std::vector<Candidate> candidates;
        candidates.reserve(128);

        const wchar_t firstChar = normQuery[0];
        const size_t qLen = normQuery.size();
        const bool isSingleToken = (tokens.size() <= 1);
        const bool hasPathSep = (query.find(L'/') != std::wstring_view::npos ||
                                 query.find(L'\\') != std::wstring_view::npos ||
                                 query.find(L':') != std::wstring_view::npos);
        const bool allowPathMatch = hasPathSep || (tokens.size() > 1) || (normQuery.size() >= 3);

        for (const auto& item : *index) {
            int s = -1;

            // 1. Primary match: check if query matches the file/folder name directly
            if (item.normName.size() >= (isSingleToken ? qLen : tokens.back().size())) {
                if (item.normName.find(firstChar) != std::wstring::npos) {
                    s = ScoreFile(item.normName, normQuery, item.isDirectory);
                }
            }

            // 2. Secondary match: path / parent directory match
            if (s <= 0 && allowPathMatch) {
                if (isSingleToken) {
                    // Contiguous substring in path (e.g. folder name in path)
                    size_t pos = item.normPath.find(normQuery);
                    if (pos != std::wstring::npos) {
                        const size_t penalty = (std::min)(item.normPath.size() / 4, size_t{300});
                        int pathScore = 2600 - static_cast<int>(penalty);
                        if (item.isDirectory) pathScore += 40;
                        s = (std::max)(1000, pathScore);
                    }
                } else {
                    // Multi-token match: all tokens must appear in normPath
                    bool allFound = true;
                    for (const auto& token : tokens) {
                        if (item.normPath.find(token) == std::wstring::npos) {
                            allFound = false;
                            break;
                        }
                    }
                    if (allFound) {
                        const bool lastMatchesName = (item.normName.find(tokens.back()) != std::wstring::npos);
                        const size_t penalty = (std::min)(item.normPath.size() / 4, size_t{300});
                        int tokenScore = 2400 + (lastMatchesName ? 600 : 0) - static_cast<int>(penalty);
                        if (item.isDirectory) tokenScore += 40;
                        s = (std::max)(1000, tokenScore);
                    }
                }
            }

            if (s > 0) {
                candidates.push_back({s, &item});
            }
        }

        if (candidates.empty()) return {};

        const size_t count = (std::min)(maxResults, candidates.size());
        std::partial_sort(candidates.begin(), candidates.begin() + count, candidates.end(),
            [](const Candidate& a, const Candidate& b) {
                return a.score > b.score;
            });

        std::vector<FileSearchResult> results;
        results.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            results.push_back({
                candidates[i].item->name,
                candidates[i].item->path,
                candidates[i].item->isDirectory,
                candidates[i].score
            });
        }
        return results;
    }

private:
    FileIndex() = default;
    ~FileIndex() { Stop(); }

    static bool ShouldSkipDirectory(const fs::path& dirPath) {
        std::wstring name = dirPath.filename().wstring();
        if (name.empty()) return false;
        if (name[0] == L'.') return true;

        std::wstring lower = name;
        std::transform(lower.begin(), lower.end(), lower.begin(),
            [](wchar_t ch) { return static_cast<wchar_t>(towlower(ch)); });

        if (lower == L"node_modules" || lower == L"appdata" || lower == L"packages" ||
            lower == L"package cache" || lower == L"temp" || lower == L"tmp" ||
            lower == L"bin" || lower == L"obj" || lower == L"build" || lower == L"target" ||
            lower == L"dist" || lower == L".git" || lower == L".vs" || lower == L".idea" ||
            lower == L"recovery" || lower == L"$recycle.bin" || lower == L"system volume information" ||
            lower == L"crashdumps" || lower == L"windows" || lower == L"program files" ||
            lower == L"program files (x86)" || lower == L"programdata" || lower == L"perflogs") {
            return true;
        }
        return false;
    }

    void AddItem(const fs::path& p, bool isDir, std::vector<FileItem>& items) {
        std::wstring name = p.filename().wstring();
        if (name.empty()) {
            name = p.wstring();
            if (name.empty()) return;
        }
        if (!isDir && (name[0] == L'.' || name[0] == L'~')) return;
        std::wstring norm = Normalize(name);
        std::wstring fullPath = p.wstring();
        std::wstring normPath = Normalize(fullPath);
        items.push_back({std::move(name), std::move(norm), std::move(fullPath), std::move(normPath), isDir});
    }

    void ScanPath(const fs::path& root, std::vector<FileItem>& items, int maxDepth, size_t maxCount) {
        std::error_code ec;
        if (!fs::exists(root, ec)) return;

        fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
        const fs::recursive_directory_iterator end;

        while (it != end && !ec) {
            if (!running_.load()) return;
            if (items.size() >= maxCount) return;

            const auto& entry = *it;
            if (entry.is_directory(ec)) {
                if (it.depth() >= maxDepth || ShouldSkipDirectory(entry.path())) {
                    it.disable_recursion_pending();
                } else {
                    AddItem(entry.path(), true, items);
                }
                it.increment(ec);
                continue;
            }

            if (entry.is_regular_file(ec)) {
                AddItem(entry.path(), false, items);
            }
            it.increment(ec);
        }
    }

    void BuildIndex() {
        constexpr size_t kMaxFiles = 150000;
        std::vector<FileItem> newItems;
        newItems.reserve(50000);

        // 1. Scan primary user folders (Desktop, Documents, Downloads, Pictures, Music, Videos)
        const KNOWNFOLDERID userFolders[] = {
            FOLDERID_Desktop,
            FOLDERID_Documents,
            FOLDERID_Downloads,
            FOLDERID_Pictures,
            FOLDERID_Music,
            FOLDERID_Videos,
        };

        for (const auto& kfid : userFolders) {
            if (!running_.load()) return;
            PWSTR folderPath = nullptr;
            if (SUCCEEDED(SHGetKnownFolderPath(kfid, KF_FLAG_DEFAULT, nullptr, &folderPath))) {
                AddItem(folderPath, true, newItems);
                ScanPath(folderPath, newItems, 8, kMaxFiles);
                CoTaskMemFree(folderPath);
            }
        }

        // 2. Scan %USERPROFILE% roots (e.g. source code directories, projects, etc.)
        PWSTR profilePath = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Profile, KF_FLAG_DEFAULT, nullptr, &profilePath))) {
            std::error_code ec;
            fs::directory_iterator dit(profilePath, fs::directory_options::skip_permission_denied, ec);
            for (const auto& entry : dit) {
                if (!running_.load()) break;
                if (newItems.size() >= kMaxFiles) break;
                if (entry.is_directory(ec)) {
                    std::wstring name = entry.path().filename().wstring();
                    if (!ShouldSkipDirectory(entry.path()) &&
                        _wcsicmp(name.c_str(), L"Desktop") != 0 &&
                        _wcsicmp(name.c_str(), L"Documents") != 0 &&
                        _wcsicmp(name.c_str(), L"Downloads") != 0 &&
                        _wcsicmp(name.c_str(), L"Pictures") != 0 &&
                        _wcsicmp(name.c_str(), L"Music") != 0 &&
                        _wcsicmp(name.c_str(), L"Videos") != 0) {
                        AddItem(entry.path(), true, newItems);
                        ScanPath(entry.path(), newItems, 8, kMaxFiles);
                    }
                } else if (entry.is_regular_file(ec)) {
                    AddItem(entry.path(), false, newItems);
                }
            }
            CoTaskMemFree(profilePath);
        }

        // 3. Scan all fixed and removable drives (e.g. C:\, D:\, X:\)
        wchar_t driveBuffer[512]{};
        if (GetLogicalDriveStringsW(static_cast<DWORD>(std::size(driveBuffer)), driveBuffer)) {
            const wchar_t* drive = driveBuffer;
            while (*drive && running_.load() && newItems.size() < kMaxFiles) {
                const UINT driveType = GetDriveTypeW(drive);
                if (driveType == DRIVE_FIXED || driveType == DRIVE_REMOVABLE) {
                    const wchar_t driveLetter = towupper(drive[0]);
                    const bool isDriveC = (driveLetter == L'C');
                    std::error_code ec;
                    fs::directory_iterator dit(drive, fs::directory_options::skip_permission_denied, ec);
                    for (const auto& entry : dit) {
                        if (!running_.load()) break;
                        if (newItems.size() >= kMaxFiles) break;
                        if (entry.is_directory(ec)) {
                            std::wstring dirName = entry.path().filename().wstring();
                            if (isDriveC && _wcsicmp(dirName.c_str(), L"Users") == 0) {
                                // Skip Users root on C: as user profile was already scanned in step 2
                                continue;
                            }
                            if (!ShouldSkipDirectory(entry.path())) {
                                AddItem(entry.path(), true, newItems);
                                ScanPath(entry.path(), newItems, 8, kMaxFiles);
                            }
                        } else if (entry.is_regular_file(ec)) {
                            AddItem(entry.path(), false, newItems);
                        }
                    }
                }
                drive += wcslen(drive) + 1;
            }
        }

        if (!running_.load()) return;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            index_ = std::make_shared<const std::vector<FileItem>>(std::move(newItems));
            ready_ = true;
        }

        if (notifyHwnd_) {
            PostMessageW(notifyHwnd_, kFilesReadyMessage, 0, 0);
        }
    }

    void WorkerLoop() {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);

        // Initial background index
        BuildIndex();

        // Setup change monitors for active user directories
        PWSTR desktopPath = nullptr, docPath = nullptr, downPath = nullptr;
        SHGetKnownFolderPath(FOLDERID_Desktop, KF_FLAG_DEFAULT, nullptr, &desktopPath);
        SHGetKnownFolderPath(FOLDERID_Documents, KF_FLAG_DEFAULT, nullptr, &docPath);
        SHGetKnownFolderPath(FOLDERID_Downloads, KF_FLAG_DEFAULT, nullptr, &downPath);

        HANDLE hDesktop = desktopPath ? FindFirstChangeNotificationW(desktopPath, TRUE,
            FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME) : INVALID_HANDLE_VALUE;
        HANDLE hDocs = docPath ? FindFirstChangeNotificationW(docPath, TRUE,
            FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME) : INVALID_HANDLE_VALUE;
        HANDLE hDownloads = downPath ? FindFirstChangeNotificationW(downPath, TRUE,
            FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME) : INVALID_HANDLE_VALUE;

        std::vector<HANDLE> waitHandles;
        if (stopEvent_) waitHandles.push_back(stopEvent_);
        if (triggerEvent_) waitHandles.push_back(triggerEvent_);
        if (hDesktop != INVALID_HANDLE_VALUE && hDesktop != nullptr) waitHandles.push_back(hDesktop);
        if (hDocs != INVALID_HANDLE_VALUE && hDocs != nullptr) waitHandles.push_back(hDocs);
        if (hDownloads != INVALID_HANDLE_VALUE && hDownloads != nullptr) waitHandles.push_back(hDownloads);

        while (running_.load()) {
            DWORD wait = WaitForMultipleObjects(
                static_cast<DWORD>(waitHandles.size()),
                waitHandles.data(),
                FALSE,
                300000 // 5-minute periodic idle scan
            );

            if (!running_.load()) break;

            if (wait == WAIT_OBJECT_0) {
                // stopEvent_
                break;
            }

            // Debounce user file operations (e.g. large file write, burst of downloads)
            WaitForSingleObject(stopEvent_, 3000);
            if (!running_.load()) break;

            BuildIndex();

            // Refresh change notification handles
            if (hDesktop != INVALID_HANDLE_VALUE && hDesktop != nullptr) FindNextChangeNotification(hDesktop);
            if (hDocs != INVALID_HANDLE_VALUE && hDocs != nullptr) FindNextChangeNotification(hDocs);
            if (hDownloads != INVALID_HANDLE_VALUE && hDownloads != nullptr) FindNextChangeNotification(hDownloads);
        }

        if (hDesktop != INVALID_HANDLE_VALUE && hDesktop != nullptr) FindCloseChangeNotification(hDesktop);
        if (hDocs != INVALID_HANDLE_VALUE && hDocs != nullptr) FindCloseChangeNotification(hDocs);
        if (hDownloads != INVALID_HANDLE_VALUE && hDownloads != nullptr) FindCloseChangeNotification(hDownloads);

        if (desktopPath) CoTaskMemFree(desktopPath);
        if (docPath) CoTaskMemFree(docPath);
        if (downPath) CoTaskMemFree(downPath);
    }

    std::atomic<bool> running_{false};
    std::atomic<bool> ready_{false};
    mutable std::mutex mutex_;
    std::shared_ptr<const std::vector<FileItem>> index_;
    std::thread worker_;
    HANDLE stopEvent_ = nullptr;
    HANDLE triggerEvent_ = nullptr;
    HWND notifyHwnd_ = nullptr;
};

} // namespace takeoff
