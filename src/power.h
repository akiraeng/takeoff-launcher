#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <string>
#include <string_view>

namespace takeoff {

enum class PowerAction {
    Shutdown,
    Restart,
    Sleep,
    Hibernate,
    Lock,
    SignOut
};

struct PowerIconInfo {
    uint32_t backgroundColor;
    const wchar_t* glyph;
};

inline PowerIconInfo GetPowerIconInfo(PowerAction action) {
    switch (action) {
    case PowerAction::Shutdown:
        return {0xDC2626, L"\uE7E8"}; // Red badge, Power button symbol
    case PowerAction::Restart:
        return {0xEA580C, L"\uE777"}; // Orange badge, Circular restart arrow
    case PowerAction::Sleep:
        return {0x4F46E5, L"\uE708"}; // Indigo badge, Crescent moon
    case PowerAction::Hibernate:
        return {0x0D9488, L"\uE7C8"}; // Teal badge, Battery / Energy saver
    case PowerAction::Lock:
        return {0x7C3AED, L"\uE72E"}; // Purple badge, Padlock
    case PowerAction::SignOut:
        return {0xBE185D, L"\uE7E7"}; // Rose badge, Sign out / leave
    default:
        return {0x4B5563, L"\uE7E8"};
    }
}

inline bool IsPowerCommand(std::wstring_view path) {
    return path.rfind(L"takeoff:power:", 0) == 0;
}

inline bool ParsePowerAction(std::wstring_view path, PowerAction& outAction) {
    if (path == L"takeoff:power:shutdown") {
        outAction = PowerAction::Shutdown;
        return true;
    }
    if (path == L"takeoff:power:restart") {
        outAction = PowerAction::Restart;
        return true;
    }
    if (path == L"takeoff:power:sleep") {
        outAction = PowerAction::Sleep;
        return true;
    }
    if (path == L"takeoff:power:hibernate") {
        outAction = PowerAction::Hibernate;
        return true;
    }
    if (path == L"takeoff:power:lock") {
        outAction = PowerAction::Lock;
        return true;
    }
    if (path == L"takeoff:power:signout" || path == L"takeoff:power:logoff") {
        outAction = PowerAction::SignOut;
        return true;
    }
    return false;
}

inline bool EnableShutdownPrivilege() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) {
        return false;
    }
    TOKEN_PRIVILEGES tp{};
    if (LookupPrivilegeValueW(nullptr, L"SeShutdownPrivilege", &tp.Privileges[0].Luid)) {
        tp.PrivilegeCount = 1;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        AdjustTokenPrivileges(token, FALSE, &tp, sizeof(tp), nullptr, nullptr);
    }
    CloseHandle(token);
    return GetLastError() == ERROR_SUCCESS;
}

inline bool ExecutePowerAction(PowerAction action, bool dryRun = false) {
    if (dryRun) return true;

    switch (action) {
    case PowerAction::Shutdown: {
        EnableShutdownPrivilege();
        const INT_PTR res = reinterpret_cast<INT_PTR>(
            ShellExecuteW(nullptr, L"open", L"shutdown.exe", L"/s /t 0", nullptr, SW_HIDE));
        return res > 32;
    }
    case PowerAction::Restart: {
        EnableShutdownPrivilege();
        const INT_PTR res = reinterpret_cast<INT_PTR>(
            ShellExecuteW(nullptr, L"open", L"shutdown.exe", L"/r /t 0", nullptr, SW_HIDE));
        return res > 32;
    }
    case PowerAction::Sleep: {
        EnableShutdownPrivilege();
        typedef BOOLEAN (WINAPI *SetSuspendStateFunc)(BOOLEAN, BOOLEAN, BOOLEAN);
        HMODULE hPowrProf = LoadLibraryW(L"powrprof.dll");
        if (hPowrProf) {
            auto setSuspendState = reinterpret_cast<SetSuspendStateFunc>(
                GetProcAddress(hPowrProf, "SetSuspendState"));
            if (setSuspendState) {
                BOOLEAN res = setSuspendState(FALSE, FALSE, FALSE);
                FreeLibrary(hPowrProf);
                return res != FALSE;
            }
            FreeLibrary(hPowrProf);
        }
        return false;
    }
    case PowerAction::Hibernate: {
        EnableShutdownPrivilege();
        typedef BOOLEAN (WINAPI *SetSuspendStateFunc)(BOOLEAN, BOOLEAN, BOOLEAN);
        HMODULE hPowrProf = LoadLibraryW(L"powrprof.dll");
        if (hPowrProf) {
            auto setSuspendState = reinterpret_cast<SetSuspendStateFunc>(
                GetProcAddress(hPowrProf, "SetSuspendState"));
            if (setSuspendState) {
                BOOLEAN res = setSuspendState(TRUE, FALSE, FALSE);
                FreeLibrary(hPowrProf);
                if (res != FALSE) return true;
            } else {
                FreeLibrary(hPowrProf);
            }
        }
        const INT_PTR res = reinterpret_cast<INT_PTR>(
            ShellExecuteW(nullptr, L"open", L"shutdown.exe", L"/h", nullptr, SW_HIDE));
        return res > 32;
    }
    case PowerAction::Lock: {
        if (::LockWorkStation()) {
            return true;
        }
        const INT_PTR res = reinterpret_cast<INT_PTR>(
            ShellExecuteW(nullptr, L"open", L"rundll32.exe", L"user32.dll,LockWorkStation", nullptr, SW_HIDE));
        return res > 32;
    }
    case PowerAction::SignOut: {
        const INT_PTR res = reinterpret_cast<INT_PTR>(
            ShellExecuteW(nullptr, L"open", L"shutdown.exe", L"/l", nullptr, SW_HIDE));
        return res > 32;
    }
    default:
        return false;
    }
}

inline bool ExecutePowerCommand(std::wstring_view path, bool dryRun = false) {
    PowerAction action;
    if (ParsePowerAction(path, action)) {
        return ExecutePowerAction(action, dryRun);
    }
    return false;
}

} // namespace takeoff
