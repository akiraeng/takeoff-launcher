#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <imm.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "resource.h"
#include "search.h"
#include "settings.h"
#include "updates.h"

namespace fs = std::filesystem;
using Microsoft::WRL::ComPtr;
using takeoff::AppCategory;
using takeoff::MatchScore;
using takeoff::Normalize;
using takeoff::ScoreApp;
using takeoff::SearchInput;
using takeoff::Settings;

namespace {

#if defined(TAKEOFF_UI_TEST) || defined(QUICKLAUNCH_UI_TEST)
constexpr bool kUiTest = true;
constexpr wchar_t kWindowClass[] = L"TakeoffTestWindow";
constexpr wchar_t kMutexName[] = L"Local\\Takeoff.UiTest";
#else
constexpr bool kUiTest = false;
constexpr wchar_t kWindowClass[] = L"TakeoffWindow";
constexpr wchar_t kMutexName[] = L"Local\\Takeoff.SingleInstance";
#endif
constexpr int kHotkeyId = 1;
constexpr UINT kAppsReadyMessage = WM_APP + 1;
constexpr UINT kShowLauncherMessage = WM_APP + 2;
constexpr UINT kTrayMessage = WM_APP + 3;
constexpr UINT kIconReadyMessage = WM_APP + 4;
constexpr UINT kUpdateCheckCompletedMessage = WM_APP + 5;
constexpr UINT kExitLauncherMessage = WM_APP + 6;
constexpr UINT_PTR kCaretTimer = 1;
constexpr UINT_PTR kHotkeyTimer = 2;
constexpr UINT_PTR kRenderRetryTimer = 3;
constexpr UINT_PTR kTrimTimer = 4;
constexpr UINT_PTR kUpdateCheckTimer = 5;

struct AppEntry {
    std::wstring name;
    std::wstring normalizedName;
    std::wstring path;
    AppCategory category = AppCategory::Application;
    std::vector<std::wstring> aliases;
    std::wstring iconPath;
    std::wstring parameters;
};

struct RankedResult {
    size_t appIndex;
    int score;
};

int ScaleForDpi(int value, UINT dpi) {
    return MulDiv(value, static_cast<int>(dpi), 96);
}

bool IsLaunchableFile(const fs::path& path) {
    std::wstring extension = path.extension().wstring();
    std::transform(
        extension.begin(), extension.end(), extension.begin(),
        [](wchar_t ch) { return static_cast<wchar_t>(towlower(ch)); });
    return extension == L".lnk" || extension == L".exe" ||
           extension == L".appref-ms" || extension == L".url";
}

bool IsUninstaller(std::wstring_view name) {
    const std::wstring normalized = Normalize(name);
    return normalized.rfind(L"uninstall", 0) == 0 ||
           normalized.rfind(L"remove ", 0) == 0 ||
           normalized.find(L"uninstaller") != std::wstring::npos;
}

void ScanDirectory(const fs::path& root, std::vector<AppEntry>& apps) {
    std::error_code error;
    if (!fs::exists(root, error)) return;
    fs::recursive_directory_iterator iterator(
        root, fs::directory_options::skip_permission_denied, error);
    const fs::recursive_directory_iterator end;
    while (iterator != end) {
        if (error) {
            error.clear();
            iterator.increment(error);
            continue;
        }
        const fs::directory_entry& entry = *iterator;
        if (entry.is_regular_file(error) && IsLaunchableFile(entry.path())) {
            std::wstring name = entry.path().stem().wstring();
            if (!name.empty() && !IsUninstaller(name)) {
                apps.push_back({name, Normalize(name), entry.path().wstring()});
            }
        }
        iterator.increment(error);
    }
}

void ScanAppsFolder(std::vector<AppEntry>& apps) {
    PIDLIST_ABSOLUTE appsFolderId = nullptr;
    if (FAILED(SHGetKnownFolderIDList(
            FOLDERID_AppsFolder, KF_FLAG_DEFAULT, nullptr, &appsFolderId))) return;
    IShellFolder* appsFolder = nullptr;
    if (FAILED(SHBindToObject(
            nullptr, appsFolderId, nullptr, IID_PPV_ARGS(&appsFolder)))) {
        CoTaskMemFree(appsFolderId);
        return;
    }
    IEnumIDList* enumerator = nullptr;
    if (SUCCEEDED(appsFolder->EnumObjects(
            nullptr, SHCONTF_FOLDERS | SHCONTF_NONFOLDERS, &enumerator))) {
        PITEMID_CHILD child = nullptr;
        while (enumerator->Next(1, &child, nullptr) == S_OK) {
            STRRET displayNameResult{};
            wchar_t displayName[MAX_PATH]{};
            if (SUCCEEDED(appsFolder->GetDisplayNameOf(
                    child, SHGDN_NORMAL, &displayNameResult)) &&
                SUCCEEDED(StrRetToBufW(
                    &displayNameResult, child, displayName, MAX_PATH))) {
                IShellItem* item = nullptr;
                if (SUCCEEDED(SHCreateItemWithParent(
                        appsFolderId, appsFolder, child, IID_PPV_ARGS(&item)))) {
                    PWSTR parsingName = nullptr;
                    if (SUCCEEDED(item->GetDisplayName(
                            SIGDN_PARENTRELATIVEPARSING, &parsingName))) {
                        std::wstring name(displayName);
                        if (!name.empty() && !IsUninstaller(name)) {
                            apps.push_back({
                                std::move(name), Normalize(displayName),
                                std::wstring(L"shell:AppsFolder\\") + parsingName,
                            });
                        }
                        CoTaskMemFree(parsingName);
                    }
                    item->Release();
                }
            }
            CoTaskMemFree(child);
            child = nullptr;
        }
        enumerator->Release();
    }
    appsFolder->Release();
    CoTaskMemFree(appsFolderId);
}

void AddSystemItems(std::vector<AppEntry>& apps) {
    wchar_t systemDirectory[MAX_PATH]{};
    GetSystemDirectoryW(systemDirectory, MAX_PATH);
    const std::wstring sysDir(systemDirectory);
    wchar_t windowsDirectory[MAX_PATH]{};
    GetWindowsDirectoryW(windowsDirectory, MAX_PATH);
    const std::wstring winDir(windowsDirectory);

    struct SystemItemDef {
        const wchar_t* name;
        std::wstring path;
        std::wstring iconPath;
        std::initializer_list<const wchar_t*> aliases;
        bool isFile;
        std::wstring parameters = {};
    };

    const SystemItemDef items[] = {
        // --- Windows Settings ---
        {L"Settings", L"ms-settings:", sysDir + L"\\control.exe",
            {L"settings", L"preferences", L"options", L"config", L"control panel"}, false},
        {L"Windows Update", L"ms-settings:windowsupdate", sysDir + L"\\control.exe",
            {L"update", L"windows update", L"patch", L"check for updates", L"upgrade", L"wu"}, false},
        {L"Display Settings", L"ms-settings:display", sysDir + L"\\control.exe",
            {L"display", L"monitor", L"screen", L"resolution", L"scale", L"brightness", L"refresh rate", L"hdr"}, false},
        {L"Sound Settings", L"ms-settings:sound", sysDir + L"\\control.exe",
            {L"sound", L"audio", L"volume", L"speaker", L"microphone", L"headphones", L"output device"}, false},
        {L"Bluetooth & Devices", L"ms-settings:bluetooth", sysDir + L"\\control.exe",
            {L"bluetooth", L"bt", L"pair device", L"wireless", L"connect"}, false},
        {L"Wi-Fi Settings", L"ms-settings:network-wifi", sysDir + L"\\control.exe",
            {L"wifi", L"wi fi", L"wireless", L"wlan", L"internet", L"network"}, false},
        {L"Network Status", L"ms-settings:network-status", sysDir + L"\\control.exe",
            {L"network", L"internet", L"ethernet", L"lan", L"ip address", L"connection status"}, false},
        {L"Installed Apps", L"ms-settings:appsfeatures", sysDir + L"\\control.exe",
            {L"apps", L"installed apps", L"apps and features", L"uninstall", L"remove programs", L"add remove"}, false},
        {L"Default Apps", L"ms-settings:defaultapps", sysDir + L"\\control.exe",
            {L"default apps", L"default browser", L"file associations", L"open with"}, false},
        {L"Taskbar Settings", L"ms-settings:taskbar", sysDir + L"\\control.exe",
            {L"taskbar", L"taskbar settings", L"system tray", L"notification area"}, false},
        {L"Notifications", L"ms-settings:notifications", sysDir + L"\\control.exe",
            {L"notifications", L"focus assist", L"do not disturb", L"alerts"}, false},
        {L"Power & Battery", L"ms-settings:powersleep", sysDir + L"\\control.exe",
            {L"power", L"battery", L"sleep", L"energy saver", L"screen timeout"}, false},
        {L"Storage Settings", L"ms-settings:storagesense", sysDir + L"\\control.exe",
            {L"storage", L"disk space", L"free space", L"storage sense", L"clean disk"}, false},
        {L"Personalization / Background", L"ms-settings:personalization-background", sysDir + L"\\control.exe",
            {L"background", L"wallpaper", L"desktop background", L"personalization", L"theme"}, false},
        {L"Colors & Dark Mode", L"ms-settings:personalization-colors", sysDir + L"\\control.exe",
            {L"dark mode", L"light mode", L"accent color", L"colors", L"theme color"}, false},
        {L"Lock Screen", L"ms-settings:lockscreen", sysDir + L"\\control.exe",
            {L"lock screen", L"screensaver", L"lockscreen"}, false},
        {L"Date & Time", L"ms-settings:dateandtime", sysDir + L"\\control.exe",
            {L"date", L"time", L"clock", L"timezone", L"set time", L"ntp"}, false},
        {L"Sign-in Options", L"ms-settings:signinoptions", sysDir + L"\\control.exe",
            {L"sign in", L"pin", L"password", L"windows hello", L"fingerprint", L"face recognition"}, false},
        {L"Windows Security", L"ms-settings:windowsdefender", sysDir + L"\\control.exe",
            {L"security", L"windows security", L"defender", L"antivirus", L"virus protection", L"firewall"}, false},
        {L"Privacy & Security", L"ms-settings:privacy", sysDir + L"\\control.exe",
            {L"privacy", L"permissions", L"camera access", L"microphone access"}, false},
        {L"Printers & Scanners", L"ms-settings:printers", sysDir + L"\\control.exe",
            {L"printers", L"scanners", L"print", L"add printer"}, false},
        {L"Mouse Settings", L"ms-settings:mousetouchpad", sysDir + L"\\control.exe",
            {L"mouse", L"pointer", L"cursor speed", L"sensitivity"}, false},
        {L"Touchpad Settings", L"ms-settings:devices-touchpad", sysDir + L"\\control.exe",
            {L"touchpad", L"trackpad", L"gestures"}, false},
        {L"Keyboard & Typing", L"ms-settings:typing", sysDir + L"\\control.exe",
            {L"typing", L"keyboard", L"autocorrect", L"spell check"}, false},
        {L"Accessibility", L"ms-settings:easeofaccess-display", sysDir + L"\\control.exe",
            {L"accessibility", L"ease of access", L"text size", L"magnifier", L"high contrast"}, false},
        {L"Startup Apps", L"ms-settings:startupapps", sysDir + L"\\control.exe",
            {L"startup", L"startup apps", L"boot apps", L"autostart"}, false},

        // --- Windows Utilities ---
        {L"Task Manager", sysDir + L"\\taskmgr.exe", {},
            {L"taskmgr", L"task manager", L"processes", L"kill", L"end task", L"performance", L"cpu", L"ram", L"memory", L"tm"}, true},
        {L"Control Panel", sysDir + L"\\control.exe", {},
            {L"control panel", L"control", L"cpl", L"settings", L"cp"}, true},
        {L"Command Prompt", sysDir + L"\\cmd.exe", {},
            {L"cmd", L"command prompt", L"cli", L"shell", L"terminal", L"console", L"dos"}, true},
        {L"Windows PowerShell", sysDir + L"\\WindowsPowerShell\\v1.0\\powershell.exe", {},
            {L"powershell", L"ps", L"shell", L"terminal", L"cli"}, true},
        {L"Registry Editor", winDir + L"\\regedit.exe", {},
            {L"regedit", L"registry editor", L"registry", L"regedt32"}, true},
        {L"Disk Cleanup", sysDir + L"\\cleanmgr.exe", {},
            {L"cleanmgr", L"disk cleanup", L"clean disk", L"free space", L"temp files"}, true},
        {L"Snipping Tool", sysDir + L"\\SnippingTool.exe", {},
            {L"snipping tool", L"snip", L"screenshot", L"capture", L"screen clip"}, true},
        {L"Paint", sysDir + L"\\mspaint.exe", {},
            {L"paint", L"mspaint", L"draw", L"image editor", L"bitmap"}, true},
        {L"Notepad", winDir + L"\\notepad.exe", {},
            {L"notepad", L"text editor", L"txt", L"notes"}, true},
        {L"Calculator", sysDir + L"\\calc.exe", {},
            {L"calc", L"calculator", L"math", L"compute"}, true},
        {L"Character Map", sysDir + L"\\charmap.exe", {},
            {L"charmap", L"character map", L"symbols", L"unicode", L"special characters"}, true},
        {L"Remote Desktop Connection", sysDir + L"\\mstsc.exe", {},
            {L"mstsc", L"remote desktop", L"rdp", L"rdc"}, true},
        {L"Volume Mixer", sysDir + L"\\sndvol.exe", {},
            {L"sndvol", L"volume mixer", L"audio mixer", L"sound levels"}, true},
        {L"DirectX Diagnostic Tool", sysDir + L"\\dxdiag.exe", {},
            {L"dxdiag", L"directx", L"gpu", L"graphics info"}, true},
        {L"System Information", sysDir + L"\\msinfo32.exe", {},
            {L"msinfo32", L"system information", L"sysinfo", L"hardware info", L"specs", L"si"}, true},
        {L"Resource Monitor", sysDir + L"\\resmon.exe", {},
            {L"resmon", L"resource monitor", L"disk activity", L"network activity", L"memory"}, true},
        {L"Quick Assist", sysDir + L"\\quickassist.exe", {},
            {L"quick assist", L"quickassist", L"remote help"}, true},
        {L"Magnifier", sysDir + L"\\magnify.exe", {},
            {L"magnify", L"magnifier", L"zoom"}, true},
        {L"On-Screen Keyboard", sysDir + L"\\osk.exe", {},
            {L"osk", L"on screen keyboard", L"virtual keyboard"}, true},

        // --- Management Tools ---
        {L"Services", sysDir + L"\\services.msc", {},
            {L"services", L"services.msc", L"background services", L"daemon"}, true},
        {L"Device Manager", sysDir + L"\\devmgmt.msc", {},
            {L"devmgmt", L"device manager", L"devmgmt.msc", L"hardware", L"drivers", L"ports", L"usb", L"dm"}, true},
        {L"Disk Management", sysDir + L"\\diskmgmt.msc", {},
            {L"diskmgmt", L"disk management", L"diskmgmt.msc", L"partition", L"volumes", L"format drive"}, true},
        {L"Event Viewer", sysDir + L"\\eventvwr.msc", {},
            {L"eventvwr", L"event viewer", L"eventvwr.msc", L"logs", L"error logs", L"system log"}, true},
        {L"Computer Management", sysDir + L"\\compmgmt.msc", {},
            {L"compmgmt", L"computer management", L"compmgmt.msc", L"admin tools"}, true},
        {L"Group Policy Editor", sysDir + L"\\gpedit.msc", {},
            {L"gpedit", L"group policy", L"gpedit.msc", L"policy editor", L"gpe"}, true},
        {L"Local Security Policy", sysDir + L"\\secpol.msc", {},
            {L"secpol", L"security policy", L"secpol.msc"}, true},
        {L"Task Scheduler", sysDir + L"\\taskschd.msc", {},
            {L"taskschd", L"task scheduler", L"taskschd.msc", L"scheduled tasks", L"cron"}, true},
        {L"Windows Defender Firewall", sysDir + L"\\wf.msc", {},
            {L"wf.msc", L"firewall", L"windows firewall", L"advanced firewall", L"firewall rules"}, true},
        {L"System Configuration", sysDir + L"\\msconfig.exe", {},
            {L"msconfig", L"system configuration", L"boot", L"safe mode"}, true},
        {L"Environment Variables", sysDir + L"\\rundll32.exe", sysDir + L"\\sysdm.cpl",
            {L"env", L"environment variables", L"sysdm.cpl", L"system properties", L"path"}, true, L"sysdm.cpl,EditEnvironmentVariables"},
        {L"Network Connections", sysDir + L"\\control.exe", sysDir + L"\\ncpa.cpl",
            {L"ncpa.cpl", L"network connections", L"adapters", L"ethernet", L"wifi adapter"}, true, L"ncpa.cpl"},
        {L"Programs and Features", sysDir + L"\\control.exe", sysDir + L"\\appwiz.cpl",
            {L"appwiz.cpl", L"programs and features", L"uninstall", L"add remove programs"}, true, L"appwiz.cpl"},
        {L"Power Options", sysDir + L"\\control.exe", sysDir + L"\\powercfg.cpl",
            {L"powercfg.cpl", L"power options", L"power plan", L"sleep settings"}, true, L"powercfg.cpl"},
    };

    for (const auto& def : items) {
        if (def.isFile) {
            std::error_code ec;
            const std::wstring& checkPath = !def.iconPath.empty() ? def.iconPath : def.path;
            if (!fs::exists(checkPath, ec)) continue;
        }

        std::wstring name(def.name);
        std::wstring norm = Normalize(name);
        std::wstring pathStr = def.path;
        std::wstring iconStr = def.iconPath;
        std::wstring paramsStr = def.parameters;

        std::vector<std::wstring> aliasVec;
        for (const auto* a : def.aliases) {
            aliasVec.push_back(Normalize(a));
        }

        auto existing = std::find_if(apps.begin(), apps.end(), [&](const AppEntry& e) {
            return e.normalizedName == norm;
        });
        if (existing != apps.end()) {
            existing->category = AppCategory::System;
            existing->path = std::move(pathStr);
            existing->parameters = std::move(paramsStr);
            for (auto& a : aliasVec) {
                if (std::find(existing->aliases.begin(), existing->aliases.end(), a) == existing->aliases.end()) {
                    existing->aliases.push_back(std::move(a));
                }
            }
            if (!iconStr.empty()) {
                existing->iconPath = std::move(iconStr);
            }
        } else {
            apps.push_back({
                std::move(name),
                std::move(norm),
                std::move(pathStr),
                AppCategory::System,
                std::move(aliasVec),
                std::move(iconStr),
                std::move(paramsStr)
            });
        }
    }
}

std::vector<AppEntry> BuildAppIndex() {
    std::vector<AppEntry> apps;
    if constexpr (kUiTest) {
        wchar_t systemDirectory[MAX_PATH]{};
        GetSystemDirectoryW(systemDirectory, MAX_PATH);
        const std::wstring sysDir(systemDirectory);
        for (const wchar_t* name : {L"Calculator", L"Calendar", L"Camera", L"Clock",
                L"File Explorer", L"Firefox", L"Microsoft Edge", L"Notepad", L"Paint",
                L"Photos", L"Settings", L"Visual Studio Code", L"Windows Terminal"}) {
            // Known system icons exercise WIC decoding without indexing personal apps.
            const wchar_t* icon = name == std::wstring_view(L"Calculator") ? L"\\calc.exe"
                : name == std::wstring_view(L"Notepad") ? L"\\notepad.exe" : L"\\control.exe";
            const bool isSys = (name == std::wstring_view(L"Calculator") ||
                                name == std::wstring_view(L"Notepad") ||
                                name == std::wstring_view(L"Settings") ||
                                name == std::wstring_view(L"Windows Terminal") ||
                                name == std::wstring_view(L"Paint"));
            AppEntry entry{
                name,
                Normalize(name),
                sysDir + icon,
                isSys ? AppCategory::System : AppCategory::Application
            };
            if (name == std::wstring_view(L"Calculator")) {
                entry.aliases = {Normalize(L"calc"), Normalize(L"math")};
            } else if (name == std::wstring_view(L"Visual Studio Code")) {
                entry.aliases = {Normalize(L"vsc"), Normalize(L"vscode"), Normalize(L"code")};
            } else if (name == std::wstring_view(L"Windows Terminal")) {
                entry.aliases = {Normalize(L"wt"), Normalize(L"terminal")};
            }
            apps.push_back(std::move(entry));
        }
        return apps;
    }
    PWSTR knownFolderPath = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(
            FOLDERID_StartMenu, KF_FLAG_DEFAULT, nullptr, &knownFolderPath))) {
        ScanDirectory(knownFolderPath, apps);
        CoTaskMemFree(knownFolderPath);
        knownFolderPath = nullptr;
    }
    if (SUCCEEDED(SHGetKnownFolderPath(
            FOLDERID_CommonStartMenu, KF_FLAG_DEFAULT, nullptr, &knownFolderPath))) {
        ScanDirectory(knownFolderPath, apps);
        CoTaskMemFree(knownFolderPath);
    }
    ScanAppsFolder(apps);
    std::unordered_set<std::wstring> seenNames;
    std::vector<AppEntry> uniqueApps;
    uniqueApps.reserve(apps.size() + 70);
    for (auto& app : apps) {
        if (seenNames.insert(app.normalizedName).second) {
            uniqueApps.push_back(std::move(app));
        }
    }
    AddSystemItems(uniqueApps);
    std::sort(uniqueApps.begin(), uniqueApps.end(), [](const AppEntry& left, const AppEntry& right) {
        return CompareStringOrdinal(
            left.name.c_str(), -1, right.name.c_str(), -1, TRUE) == CSTR_LESS_THAN;
    });
    return uniqueApps;
}

struct IconRequest {
    std::wstring path;
    UINT size = 0;
};

struct IconResult {
    std::wstring path;
    UINT size = 0;
    ComPtr<IWICBitmapSource> source;
};

// Runs on the icon worker thread. Extracts the shell icon, then materializes
// the downscaled pixels into a small standalone bitmap so the cache never pins
// the original 256px decode (which would cost ~256KB per cached icon).
ComPtr<IWICBitmapSource> LoadIconSource(IWICImagingFactory* wic,
    const std::wstring& path, UINT size) {
    ComPtr<IWICBitmapSource> source;
    ComPtr<IShellItemImageFactory> imageFactory;
    if (SUCCEEDED(SHCreateItemFromParsingName(path.c_str(), nullptr,
            IID_PPV_ARGS(&imageFactory)))) {
        HBITMAP bitmap = nullptr;
        // Request the jumbo 256px icon so we downscale a sharp source
        // instead of upscaling the small 32px variant.
        if (SUCCEEDED(imageFactory->GetImage({256, 256},
                SIIGBF_ICONONLY | SIIGBF_BIGGERSIZEOK, &bitmap))) {
            ComPtr<IWICBitmap> converted;
            if (SUCCEEDED(wic->CreateBitmapFromHBITMAP(bitmap, nullptr,
                    WICBitmapUsePremultipliedAlpha, &converted))) {
                source = converted;
            }
            DeleteObject(bitmap);
        }
    }
    if (!source) {
        SHFILEINFOW info{};
        if (SHGetFileInfoW(path.c_str(), 0, &info, sizeof(info),
                SHGFI_ICON | SHGFI_LARGEICON)) {
            ComPtr<IWICBitmap> converted;
            if (SUCCEEDED(wic->CreateBitmapFromHICON(info.hIcon, &converted))) {
                source = converted;
            }
            DestroyIcon(info.hIcon);
        }
    }
    if (!source) return nullptr;
    ComPtr<IWICBitmapScaler> scaler;
    ComPtr<IWICBitmap> scaled;
    if (SUCCEEDED(wic->CreateBitmapScaler(&scaler)) &&
        SUCCEEDED(scaler->Initialize(source.Get(), size, size,
            WICBitmapInterpolationModeFant)) &&
        SUCCEEDED(wic->CreateBitmapFromSource(scaler.Get(), WICBitmapCacheOnLoad,
            &scaled))) {
        return scaled;
    }
    return source;
}

#include "launcher.h"

bool ShouldStartMinimized(int nCmdShow) {
    if (nCmdShow == SW_HIDE || nCmdShow == SW_SHOWMINIMIZED ||
        nCmdShow == SW_MINIMIZE || nCmdShow == SW_SHOWMINNOACTIVE) {
        return true;
    }

    STARTUPINFOW si{sizeof(si)};
    GetStartupInfoW(&si);
    if ((si.dwFlags & STARTF_USESHOWWINDOW) &&
        (si.wShowWindow == SW_HIDE ||
         si.wShowWindow == SW_SHOWMINIMIZED ||
         si.wShowWindow == SW_MINIMIZE ||
         si.wShowWindow == SW_SHOWMINNOACTIVE)) {
        return true;
    }

    int argc = 0;
    if (LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc)) {
        bool minimized = false;
        for (int i = 1; i < argc; ++i) {
            if (takeoff::IsMinimizedSwitch(argv[i])) {
                minimized = true;
                break;
            }
        }
        LocalFree(argv);
        if (minimized) return true;
    }

    return false;
}

bool ShouldReplaceRunningInstance() {
    int argc = 0;
    if (LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc)) {
        bool replace = false;
        for (int i = 1; i < argc; ++i) {
            if (takeoff::IsReplaceSwitch(argv[i])) {
                replace = true;
                break;
            }
        }
        LocalFree(argv);
        if (replace) return true;
    }

    return false;
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int nCmdShow) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const bool startMinimized = ShouldStartMinimized(nCmdShow);
    const bool replaceInstance = ShouldReplaceRunningInstance();

    HANDLE mutex = CreateMutexW(nullptr, FALSE, kMutexName);
    if (mutex == nullptr) return 1;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (startMinimized && !replaceInstance) {
            CloseHandle(mutex);
            return 0;
        }

        HWND existing = FindWindowW(kWindowClass, nullptr);
        if (!existing) {
            EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
                wchar_t className[256];
                if (GetClassNameW(hwnd, className, static_cast<int>(std::size(className)))) {
                    if (wcscmp(className, kWindowClass) == 0) {
                        *reinterpret_cast<HWND*>(lParam) = hwnd;
                        return FALSE;
                    }
                }
                return TRUE;
            }, reinterpret_cast<LPARAM>(&existing));
        }

        if (existing != nullptr) {
            DWORD pid = 0;
            GetWindowThreadProcessId(existing, &pid);
            HANDLE process = pid != 0 ? OpenProcess(SYNCHRONIZE | PROCESS_TERMINATE, FALSE, pid) : nullptr;
            PostMessageW(existing, kExitLauncherMessage, 0, 0);
            if (process != nullptr) {
                if (WaitForSingleObject(process, 500) == WAIT_TIMEOUT) {
                    TerminateProcess(process, 0);
                    WaitForSingleObject(process, 500);
                }
                CloseHandle(process);
            }
        }
        CloseHandle(mutex);
        mutex = nullptr;

        for (int attempt = 0; attempt < 20; ++attempt) {
            mutex = CreateMutexW(nullptr, FALSE, kMutexName);
            if (mutex != nullptr && GetLastError() != ERROR_ALREADY_EXISTS) {
                break;
            }
            if (mutex != nullptr) {
                CloseHandle(mutex);
                mutex = nullptr;
            }
            Sleep(100);
        }
        if (mutex == nullptr) {
            return 1;
        }
    }

    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    int exitCode = 1;
    {
        // Release graphics/COM resources before uninitializing the apartment.
        LauncherWindow launcher;
        if (launcher.Create(instance)) {
            const HWND hwnd = launcher.Handle();
            std::thread indexThread([hwnd] {
                const HRESULT indexComResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
                auto apps = std::make_unique<std::vector<AppEntry>>();
                try {
                    *apps = BuildAppIndex();
                } catch (const fs::filesystem_error&) {
                    // An inaccessible shell entry must not terminate the launcher.
                }
                if (PostMessageW(hwnd, kAppsReadyMessage, 0,
                        reinterpret_cast<LPARAM>(apps.get()))) {
                    apps.release();
                }
                if (SUCCEEDED(indexComResult)) CoUninitialize();
            });
            if (!startMinimized) {
                PostMessageW(hwnd, kShowLauncherMessage, 0, 0);
            }
            MSG message{};
            BOOL status = 0;
            while ((status = GetMessageW(&message, nullptr, 0, 0)) > 0) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            indexThread.join();
            // A shutdown can overtake the asynchronous index and icon delivery.
            while (PeekMessageW(&message, nullptr, kAppsReadyMessage, kAppsReadyMessage, PM_REMOVE)) {
                delete reinterpret_cast<std::vector<AppEntry>*>(message.lParam);
            }
            while (PeekMessageW(&message, nullptr, kIconReadyMessage, kIconReadyMessage, PM_REMOVE)) {
                delete reinterpret_cast<IconResult*>(message.lParam);
            }
            exitCode = status == -1 ? 1 : 0;
        }
    }
    if (SUCCEEDED(comResult)) CoUninitialize();
    CloseHandle(mutex);
    return exitCode;
}
