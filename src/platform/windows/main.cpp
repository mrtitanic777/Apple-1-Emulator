// Contributor: Phillip Allison (github.com/philtimmes)
// This file includes changes Phillip contributed to the Apple-1 Emulator.
// See CONTRIBUTORS.md for the full list of his work.

// main.cpp - WinMain entry.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <timeapi.h>
#include "app.h"
#include "main_window.h"
#include "d2d_helpers.h"
#include <cwchar>
#include <exception>
#include <string>

#pragma comment(lib, "winmm.lib")

namespace {

// Return the directory of the running executable, with a trailing
// backslash.  Used so debug logs land next to the .exe regardless of
// what working directory the user launched from.
std::string exe_directory() {
    wchar_t buf[MAX_PATH] = L"";
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n == 0) return {};
    // Trim back to the last backslash.
    while (n > 0 && buf[n - 1] != L'\\' && buf[n - 1] != L'/') --n;
    buf[n] = 0;
    int needed = WideCharToMultiByte(CP_UTF8, 0, buf, -1, nullptr, 0,
                                     nullptr, nullptr);
    std::string out(needed > 0 ? needed - 1 : 0, '\0');
    if (needed > 0)
        WideCharToMultiByte(CP_UTF8, 0, buf, -1, out.data(), needed,
                            nullptr, nullptr);
    return out;
}

// Search the wide command line for "--debug".  Recognises both
// "--debug" (default log path = <exe_dir>\debug.log) and
// "--debug=<path>".  Returns the chosen log path on match, empty
// string on no match.
std::string parse_debug_arg(LPCWSTR cmdline) {
    if (!cmdline) return {};
    const wchar_t* needle = L"--debug";
    const wchar_t* p = wcsstr(cmdline, needle);
    if (!p) return {};
    p += wcslen(needle);
    if (*p == L'=') {
        // --debug=<path>
        ++p;
        const wchar_t* start = p;
        while (*p && *p != L' ' && *p != L'\t') ++p;
        std::wstring w(start, p);
        int needed = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1,
                                         nullptr, 0, nullptr, nullptr);
        std::string out(needed > 0 ? needed - 1 : 0, '\0');
        if (needed > 0)
            WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, out.data(),
                                needed, nullptr, nullptr);
        return out.empty() ? exe_directory() + "debug.log" : out;
    }
    return exe_directory() + "debug.log";
}

} // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR pCmdLine, int nCmdShow) {
    // Bump the system timer resolution to 1 ms.  Default Windows scheduler
    // granularity is ~15.6 ms, which makes our 16.7 ms-per-char teletype
    // sleep jitter wildly (chars print in clumps of 1-2 every other tick).
    // 1 ms resolution gives near-perfect 60 cps pacing.
    timeBeginPeriod(1);

    // Initialize COM for the dialog APIs (file dialogs need apartment-
    // threaded COM).
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) {
        MessageBoxW(nullptr, L"CoInitializeEx failed", L"Apple-1",
                    MB_OK | MB_ICONERROR);
        timeEndPeriod(1);
        return 1;
    }

    const std::string debug_log_path = parse_debug_arg(pCmdLine);
    if (!debug_log_path.empty()) {
        std::string msg = "Debug logging enabled.\n\nLog file:\n"
                          + debug_log_path + "\n\nNibble dump on disk mount:\n"
                          + debug_log_path + ".nib";
        MessageBoxA(nullptr, msg.c_str(), "Apple-1 Debug",
                    MB_OK | MB_ICONINFORMATION);
    }

    int exit_code = 0;
    try {
        apple1::create_app("", "", debug_log_path);
        apple1::win::MainWindow main_window;
        if (!main_window.create(hInstance, nCmdShow)) {
            MessageBoxW(nullptr, L"Window creation failed", L"Apple-1",
                        MB_OK | MB_ICONERROR);
            exit_code = 1;
        } else {
            exit_code = main_window.run_message_loop();
        }
        apple1::destroy_app();
    } catch (const std::exception& e) {
        std::string msg = "Fatal: ";
        msg += e.what();
        MessageBoxA(nullptr, msg.c_str(), "Apple-1",
                    MB_OK | MB_ICONERROR);
        exit_code = 1;
    }

    apple1::win::shutdown_factories();
    CoUninitialize();
    timeEndPeriod(1);
    return exit_code;
}
