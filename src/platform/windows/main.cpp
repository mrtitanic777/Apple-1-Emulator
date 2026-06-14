// main.cpp - WinMain entry.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <timeapi.h>
#include "app.h"
#include "main_window.h"
#include "d2d_helpers.h"
#include <exception>
#include <string>

#pragma comment(lib, "winmm.lib")

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {
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

    int exit_code = 0;
    try {
        apple1::create_app();
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
