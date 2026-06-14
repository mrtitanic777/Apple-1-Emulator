// Contributor: Phillip Allison (github.com/philtimmes)
// This file includes changes Phillip contributed to the Apple-1 Emulator.
// See CONTRIBUTORS.md for the full list of his work.

// main_window.cpp - the main display window.

#include "main_window.h"
#include "resource.h"
#include "app.h"
#include "disasm.h"
#include <commdlg.h>
#include <shellapi.h>
#include <string>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <cwctype>

namespace apple1::win {

namespace {

const wchar_t* kClassName  = L"Apple1MainWindow";
const wchar_t* kWindowName = L"Apple-1 Emulator";

constexpr UINT_PTR kRenderTimerId = 1;
constexpr UINT     kRenderTimerMs = 16;     // ~60 Hz (SetTimer pacing, not monitor VSYNC)

// Helper: GetOpenFileName / GetSaveFileName wrappers returning UTF-8.
std::string open_file_dialog(HWND owner, const wchar_t* filter,
                             const wchar_t* title) {
    wchar_t buf[MAX_PATH] = L"";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = owner;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile   = buf;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrTitle  = title;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (!GetOpenFileNameW(&ofn)) return {};
    // Convert wide -> UTF-8.
    int needed = WideCharToMultiByte(CP_UTF8, 0, buf, -1, nullptr, 0,
                                     nullptr, nullptr);
    std::string out(needed > 0 ? needed - 1 : 0, '\0');
    if (needed > 0)
        WideCharToMultiByte(CP_UTF8, 0, buf, -1, out.data(), needed,
                            nullptr, nullptr);
    return out;
}

std::string save_file_dialog(HWND owner, const wchar_t* filter,
                             const wchar_t* title, const wchar_t* defext) {
    wchar_t buf[MAX_PATH] = L"";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = owner;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile   = buf;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrTitle  = title;
    ofn.lpstrDefExt = defext;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (!GetSaveFileNameW(&ofn)) return {};
    int needed = WideCharToMultiByte(CP_UTF8, 0, buf, -1, nullptr, 0,
                                     nullptr, nullptr);
    std::string out(needed > 0 ? needed - 1 : 0, '\0');
    if (needed > 0)
        WideCharToMultiByte(CP_UTF8, 0, buf, -1, out.data(), needed,
                            nullptr, nullptr);
    return out;
}

// Minimal hex-input dialog.  Returns true and address on OK, false on cancel.
// We use a simple modal dialog built from a DLGTEMPLATE in memory; spares
// us a .rc dialog resource.
struct HexInputData {
    const wchar_t* title;
    const wchar_t* prompt;
    wchar_t result[16];
    bool ok;
};

INT_PTR CALLBACK hex_input_proc(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    static HexInputData* data = nullptr;
    switch (msg) {
        case WM_INITDIALOG:
            data = reinterpret_cast<HexInputData*>(lParam);
            SetWindowTextW(dlg, data->title);
            SetDlgItemTextW(dlg, 1001, data->prompt);
            SetFocus(GetDlgItem(dlg, 1002));
            return FALSE;       // we set focus ourselves
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDOK:
                    if (data) {
                        GetDlgItemTextW(dlg, 1002, data->result, 16);
                        data->ok = true;
                    }
                    EndDialog(dlg, 1);
                    return TRUE;
                case IDCANCEL:
                    if (data) data->ok = false;
                    EndDialog(dlg, 0);
                    return TRUE;
            }
            break;
    }
    return FALSE;
}

// Build a DLGTEMPLATE in memory.  Word-aligned structure with class array,
// title array, then each control entry.
struct DlgTemplateBuf {
    BYTE  bytes[512];
    BYTE* p;

    DlgTemplateBuf() : p(bytes) {}
    void align(size_t n = 4) {
        while ((reinterpret_cast<uintptr_t>(p) - reinterpret_cast<uintptr_t>(bytes)) % n) ++p;
    }
    template <typename T> void write(const T& v) {
        std::memcpy(p, &v, sizeof(T));
        p += sizeof(T);
    }
    void write_wstr(const wchar_t* s) {
        size_t n = wcslen(s) + 1;
        std::memcpy(p, s, n * sizeof(wchar_t));
        p += n * sizeof(wchar_t);
    }
    LPCDLGTEMPLATE templ() const { return reinterpret_cast<LPCDLGTEMPLATE>(bytes); }
};

bool prompt_hex(HWND parent, const wchar_t* title, const wchar_t* prompt,
                u32* out_value) {
    // Build dialog template: caption "x", font "MS Shell Dlg", 1 static, 1 edit,
    // OK + Cancel buttons.
    DlgTemplateBuf t;
    DLGTEMPLATE dt{};
    dt.style = DS_MODALFRAME | DS_CENTER | DS_SETFONT | WS_POPUP | WS_CAPTION | WS_SYSMENU;
    dt.cdit  = 4;
    dt.x = 0; dt.y = 0;
    dt.cx = 200; dt.cy = 80;
    t.write(dt);
    t.write<WORD>(0);            // menu
    t.write<WORD>(0);            // class
    t.write_wstr(L"");           // title (overwritten in WM_INITDIALOG)
    t.write<WORD>(8);            // font size
    t.write_wstr(L"MS Shell Dlg");

    auto add_ctl = [&](short x, short y, short cx, short cy, DWORD style,
                       WORD cls, WORD id, const wchar_t* text) {
        t.align(4);
        DLGITEMTEMPLATE it{};
        it.style = style;
        it.x = x; it.y = y; it.cx = cx; it.cy = cy;
        it.id = id;
        t.write(it);
        t.write<WORD>(0xFFFF);   // atom class
        t.write<WORD>(cls);
        t.write_wstr(text);
        t.write<WORD>(0);        // no creation data
    };

    add_ctl(8,  8, 184, 12, WS_CHILD | WS_VISIBLE,                 0x0082, 1001, L"");
    add_ctl(8, 24, 184, 14, WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL,
                                                                    0x0081, 1002, L"");
    add_ctl(48, 50, 50, 14, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 0x0080, IDOK,     L"OK");
    add_ctl(108, 50, 50, 14, WS_CHILD | WS_VISIBLE | WS_TABSTOP,    0x0080, IDCANCEL, L"Cancel");

    HexInputData data{ title, prompt, L"", false };
    DialogBoxIndirectParamW(GetModuleHandleW(nullptr), t.templ(),
                            parent, hex_input_proc,
                            reinterpret_cast<LPARAM>(&data));
    if (!data.ok || data.result[0] == 0) return false;
    wchar_t* end = nullptr;
    unsigned long v = wcstoul(data.result, &end, 16);
    *out_value = static_cast<u32>(v);
    return true;
}

} // namespace

bool MainWindow::create(HINSTANCE hInstance, int nCmdShow) {
    hInstance_ = hInstance;

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = &MainWindow::proc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;       // D2D paints everything
    wc.lpszClassName = kClassName;
    wc.lpszMenuName  = MAKEINTRESOURCEW(IDR_MAIN_MENU);
    // App icon - shown in the title bar, alt-tab list, and taskbar.
    wc.hIcon   = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_APPICON));
    wc.hIconSm = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_APPICON));
    if (!RegisterClassExW(&wc)) return false;

    accel_ = LoadAcceleratorsW(hInstance, MAKEINTRESOURCEW(IDR_MAIN_ACCEL));

    // Default size: 3x scale plus a CRT-style 3-cell bezel on each side.
    // Client area = (kCols+6)*7*3 wide, (kRows+6)*8*3 tall.
    int cli_w = (kCols + 6) * 7 * 3;        // 966
    int cli_h = (kRows + 6) * 8 * 3;        // 720
    RECT rc = { 0, 0, cli_w, cli_h };
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, TRUE);
    hwnd_ = CreateWindowExW(
        0, kClassName, kWindowName,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rc.right - rc.left, rc.bottom - rc.top,
        nullptr, nullptr, hInstance, this);
    if (!hwnd_) return false;

    renderer_.attach(hwnd_, app().rom_set().chargen, &app().settings());

    debugger_window_ = std::make_unique<DebuggerWindow>();
    debugger_window_->create(hInstance, hwnd_);

    timer_id_ = SetTimer(hwnd_, kRenderTimerId, kRenderTimerMs, nullptr);

    sync_settings_menu();
    sync_expansions_menu();

    ShowWindow(hwnd_, nCmdShow);
    UpdateWindow(hwnd_);
    return true;
}

int MainWindow::run_message_loop() {
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (!TranslateAcceleratorW(hwnd_, accel_, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return static_cast<int>(msg.wParam);
}

LRESULT CALLBACK MainWindow::proc(HWND hwnd, UINT msg,
                                  WPARAM wParam, LPARAM lParam) {
    MainWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        self = static_cast<MainWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    } else {
        self = reinterpret_cast<MainWindow*>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) return self->handle(msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT MainWindow::handle(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_SIZING: {
            // Lock the aspect ratio AND snap to integer-scale multiples
            // of the natural glyph size while the user drags.  This
            // guarantees every glyph pixel ends up the same size on
            // screen (no half-pixel artifacts where one column is 2px
            // wide and the next is 3px).
            //
            // Target client = (kCols+6)*kGlyphW*scale x (kRows+6)*kGlyphH*scale
            constexpr int unit_w = (kCols + 6) * 7;     // 322
            constexpr int unit_h = (kRows + 6) * 8;     // 240
            constexpr float aspect = static_cast<float>(unit_w)
                                   / static_cast<float>(unit_h);

            RECT* rect = reinterpret_cast<RECT*>(lParam);
            RECT chrome = { 0, 0, 0, 0 };
            DWORD style = GetWindowLongW(hwnd_, GWL_STYLE);
            AdjustWindowRect(&chrome, style, TRUE);
            int chrome_w = chrome.right - chrome.left;
            int chrome_h = chrome.bottom - chrome.top;

            int cli_w = (rect->right - rect->left) - chrome_w;
            int cli_h = (rect->bottom - rect->top) - chrome_h;
            if (cli_w < 1) cli_w = 1;
            if (cli_h < 1) cli_h = 1;

            // Pick which dimension is the master based on drag edge,
            // then derive scale from that and snap both to integer
            // multiples.
            int edge = static_cast<int>(wParam);
            bool drag_h = (edge == WMSZ_TOP || edge == WMSZ_BOTTOM);
            bool drag_w = (edge == WMSZ_LEFT || edge == WMSZ_RIGHT);

            int scale;
            if (drag_h && !drag_w) {
                scale = cli_h / unit_h;
            } else if (drag_w && !drag_h) {
                scale = cli_w / unit_w;
            } else {
                // Corner drag - pick whichever gives the larger of the
                // two scale options (so the window grows in both dims).
                int s_w = cli_w / unit_w;
                int s_h = cli_h / unit_h;
                scale = (s_w > s_h) ? s_w : s_h;
            }
            if (scale < 1) scale = 1;

            cli_w = unit_w * scale;
            cli_h = unit_h * scale;

            int outer_w = cli_w + chrome_w;
            int outer_h = cli_h + chrome_h;

            // Apply by adjusting the side opposite the anchor so the
            // drag-handle stays under the cursor.
            switch (edge) {
                case WMSZ_LEFT:
                case WMSZ_BOTTOMLEFT:
                    rect->left   = rect->right  - outer_w;
                    rect->bottom = rect->top    + outer_h;
                    break;
                case WMSZ_RIGHT:
                case WMSZ_TOPRIGHT:
                    rect->right  = rect->left   + outer_w;
                    rect->top    = rect->bottom - outer_h;
                    break;
                case WMSZ_TOP:
                case WMSZ_TOPLEFT:
                    rect->top    = rect->bottom - outer_h;
                    rect->left   = rect->right  - outer_w;
                    break;
                case WMSZ_BOTTOM:
                case WMSZ_BOTTOMRIGHT:
                default:
                    rect->right  = rect->left   + outer_w;
                    rect->bottom = rect->top    + outer_h;
                    break;
            }
            (void)aspect;   // unused now (we snap by integer scale)
            return TRUE;
        }
        case WM_SIZE:
            renderer_.on_resize(LOWORD(lParam), HIWORD(lParam));
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hwnd_, &ps);
            renderer_.render(app().display());
            EndPaint(hwnd_, &ps);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_TIMER:
            // Check for pending tape / disk requests from the CPU thread.
            // The file dialog must be opened on the GUI thread, so the CPU
            // thread merely sets a flag and we react here.
            if (app().bus().tape_requested() && !prompting_for_tape_) {
                cmd_prompt_for_tape();
            }
            if (app().bus().disk_requested() && !prompting_for_disk_) {
                cmd_prompt_for_disk();
            }
            InvalidateRect(hwnd_, nullptr, FALSE);
            if (debugger_window_) debugger_window_->redraw();
            return 0;
        case WM_CHAR:
            on_char(static_cast<wchar_t>(wParam));
            return 0;
        case WM_COMMAND:
            on_command(LOWORD(wParam));
            return 0;
        case WM_DESTROY:
            if (timer_id_) KillTimer(hwnd_, timer_id_);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd_, msg, wParam, lParam);
}

void MainWindow::on_char(wchar_t ch) {
    if (ch == 0x08 || ch == 0x7F) {       // backspace / delete
        app().bus().feed_key('_');
        return;
    }
    if (ch == 0x0D || ch == 0x0A) {        // enter
        app().bus().feed_key('\r');
        return;
    }
    if (ch >= 0x20 && ch < 0x7F) {
        char c = static_cast<char>(ch);
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
        app().bus().feed_key(c);
    }
}

void MainWindow::on_command(WORD id) {
    switch (id) {
        case IDM_FILE_OPEN:
            cmd_file_open();
            break;
        case IDM_FILE_EXIT:
            DestroyWindow(hwnd_);
            break;
        case IDM_CPU_CLEAR_SCREEN:
            app().clear_screen();
            break;
        case IDM_CPU_RESET:
            app().reset_cpu();
            break;
        case IDM_CPU_PAUSE:
            app().debugger().toggle_pause();
            break;
        case IDM_CPU_STEP:
            // request_step() pauses + advances one instruction in one shot,
            // so the prior is_paused() guard is no longer needed.
            app().debugger().request_step();
            break;
        case IDM_CPU_STEP_OVER:
            if (app().debugger().is_paused()) {
                auto d = disasm::decode(app().bus(), app().cpu().pc());
                u16 ret = static_cast<u16>(app().cpu().pc() + d.length);
                app().debugger().request_step_over(ret);
            }
            break;
        case IDM_CASSETTE_SAVE:
            cmd_cassette_save();
            break;
        case IDM_DISK_MOUNT:
            cmd_disk_mount();
            break;
        case IDM_DISK_EJECT:
            cmd_disk_eject();
            break;
        case IDM_DEBUGGER_SHOW:
            if (debugger_window_) debugger_window_->show();
            break;
        case IDM_DEBUGGER_TOGGLE_BP:
            app().debugger().toggle_breakpoint(app().cpu().pc());
            break;
        case IDM_DEBUGGER_GOTO_MEM:
            cmd_debugger_goto_memory();
            break;
        case IDM_DEBUGGER_CLEAR_BPS:
            app().debugger().clear_all_breakpoints();
            break;
        case IDM_VIEW_SCALE_TINY: cmd_set_scale(1); break;
        case IDM_VIEW_SCALE_1X:   cmd_set_scale(2); break;
        case IDM_VIEW_SCALE_2X:   cmd_set_scale(3); break;
        case IDM_VIEW_SCALE_3X:   cmd_set_scale(5); break;
        case IDM_VIEW_SCALE_HUGE: cmd_set_scale(7); break;

        case IDM_SETTINGS_SCANLINES:
            app().settings().set_scanlines(!app().settings().scanlines());
            sync_settings_menu();
            break;
        case IDM_SETTINGS_DOT_ARTIFACT:
            app().settings().set_dot_artifact(!app().settings().dot_artifact());
            sync_settings_menu();
            break;
        case IDM_SETTINGS_TELETYPE_PACING:
            app().settings().set_teletype_pacing(!app().settings().teletype_pacing());
            app().display().set_pacing(app().settings().teletype_pacing());
            sync_settings_menu();
            break;
        case IDM_SETTINGS_VIGNETTE:
            app().settings().set_vignette(!app().settings().vignette());
            sync_settings_menu();
            break;
        case IDM_SETTINGS_PHOSPHOR_WHITE:
            app().settings().set_phosphor(Phosphor::White);
            renderer_.invalidate_colors();
            sync_settings_menu();
            break;
        case IDM_SETTINGS_PHOSPHOR_GREEN:
            app().settings().set_phosphor(Phosphor::Green);
            renderer_.invalidate_colors();
            sync_settings_menu();
            break;
        case IDM_SETTINGS_PHOSPHOR_AMBER:
            app().settings().set_phosphor(Phosphor::Amber);
            renderer_.invalidate_colors();
            sync_settings_menu();
            break;
        case IDM_SETTINGS_DISK_LATCH_BIT:
            app().settings().set_disk_latch(DiskLatch::Bit);
            app().bus().disk().set_byte_latch(false);
            sync_settings_menu();
            break;
        case IDM_SETTINGS_DISK_LATCH_BYTE:
            app().settings().set_disk_latch(DiskLatch::Byte);
            app().bus().disk().set_byte_latch(true);
            sync_settings_menu();
            break;

        case IDM_EXPANSION_RAM_NONE: cmd_set_ram_expansion(RamExpansion::None); break;
        case IDM_EXPANSION_RAM_8K:   cmd_set_ram_expansion(RamExpansion::K8);   break;
        case IDM_EXPANSION_RAM_16K:  cmd_set_ram_expansion(RamExpansion::K16);  break;
        case IDM_EXPANSION_RAM_24K:  cmd_set_ram_expansion(RamExpansion::K24);  break;

        case IDM_EXPANSION_IO_NONE:     cmd_set_io_card(IoCard::None);     break;
        case IDM_EXPANSION_IO_CASSETTE: cmd_set_io_card(IoCard::Cassette); break;
        case IDM_EXPANSION_IO_DISK1:    cmd_set_io_card(IoCard::Disk1);    break;

        case IDM_HELP_ABOUT:
            cmd_about();
            break;
    }
}

void MainWindow::cmd_file_open() {
    std::string path = open_file_dialog(hwnd_,
        L"All supported\0*.wav;*.bin;*.txt;*.dsk\0"
        L"Cassette WAV\0*.wav\0"
        L"Raw binary\0*.bin\0"
        L"Wozmon text\0*.txt\0"
        L"Disk II image\0*.dsk\0"
        L"All files\0*.*\0\0",
        L"Open file");
    if (path.empty()) return;
    app().load_file(path);
}

void MainWindow::cmd_disk_mount() {
    std::string path = open_file_dialog(hwnd_,
        L"Disk II image\0*.dsk\0All files\0*.*\0\0",
        L"Mount Disk II image");
    if (path.empty()) return;

    // load_file routes .dsk through fileio dispatcher -> bus.disk().mount_dsk(),
    // pausing the CPU thread for the duration.
    app().load_file(path);

    if (app().bus().disk().mounted()) {
        // Mounting a disk implies the user wants the Disk 1 card live;
        // auto-select it so the boot ROM and soft switches are mapped.
        if (app().settings().io_card() != IoCard::Disk1) {
            cmd_set_io_card(IoCard::Disk1);
        }
        // No confirmation dialog - the title bar / debugger window
        // shows the mounted image, that's feedback enough.
    } else {
        MessageBoxA(hwnd_,
                    "Mount failed. The .dsk image must be exactly 143360 bytes.",
                    "Disk II", MB_OK | MB_ICONERROR);
    }
}

void MainWindow::cmd_disk_eject() {
    if (!app().bus().disk().mounted()) {
        MessageBoxA(hwnd_, "No disk is mounted.",
                    "Disk II", MB_OK | MB_ICONINFORMATION);
        return;
    }
    // Pause the CPU thread before tearing down the nibble buffers - the
    // CPU may be in the middle of disk_.read().  Mirrors what app::load_file
    // does around the bus mutation.
    bool was_paused = app().debugger().is_paused();
    if (!was_paused) app().debugger().toggle_pause();
    Sleep(40);                       // let the CPU loop notice the flag
    app().bus().disk().eject();
    if (!was_paused) app().debugger().toggle_pause();

    MessageBoxA(hwnd_, "Disk ejected.",
                "Disk II", MB_OK | MB_ICONINFORMATION);
}

void MainWindow::cmd_cassette_save() {
    u32 start = 0, end = 0;
    if (!prompt_hex(hwnd_, L"Cassette Save", L"Start address (hex):", &start))
        return;
    if (!prompt_hex(hwnd_, L"Cassette Save", L"End address (hex):", &end))
        return;
    std::string path = save_file_dialog(hwnd_,
        L"Cassette WAV\0*.wav\0All files\0*.*\0\0",
        L"Save cassette as", L"wav");
    if (path.empty()) return;

    std::string err;
    app().save_cassette(static_cast<u16>(start), static_cast<u16>(end),
                        path, &err);
    if (!err.empty()) {
        std::string m = "Save failed: " + err;
        MessageBoxA(hwnd_, m.c_str(), "Cassette Save", MB_OK | MB_ICONERROR);
    } else {
        char m[256];
        std::snprintf(m, sizeof(m),
                      "Saved $%04X-$%04X (%u bytes).",
                      static_cast<unsigned>(start),
                      static_cast<unsigned>(end),
                      static_cast<unsigned>(end - start + 1));
        MessageBoxA(hwnd_, m, "Cassette Save", MB_OK | MB_ICONINFORMATION);
    }
}

void MainWindow::cmd_prompt_for_tape() {
    prompting_for_tape_ = true;

    // Pause the CPU while the dialog is open so the ACI's polling loop
    // doesn't time out before the user picks a file.
    bool was_paused = app().debugger().is_paused();
    if (!was_paused) app().debugger().toggle_pause();

    std::string path = open_file_dialog(hwnd_,
        L"Cassette WAV\0*.wav\0All files\0*.*\0\0",
        L"ACI: Select cassette WAV to read");

    if (path.empty()) {
        // User cancelled - stop re-prompting until reset.  ACI will time
        // out naturally on resume because $C0xx never flips.
        app().bus().set_tape_cancelled();
        app().bus().clear_tape_request();
        if (!was_paused) app().debugger().toggle_pause();
        prompting_for_tape_ = false;
        return;
    }

    std::size_t count = 0;
    std::string err;
    app().stage_cassette(path, &count, &err);
    if (!err.empty()) {
        std::string m = "Failed to load WAV: " + err;
        MessageBoxA(hwnd_, m.c_str(), "Cassette", MB_OK | MB_ICONERROR);
        app().bus().set_tape_cancelled();
    }
    app().bus().clear_tape_request();
    if (!was_paused) app().debugger().toggle_pause();
    prompting_for_tape_ = false;
}

void MainWindow::cmd_prompt_for_disk() {
    prompting_for_disk_ = true;

    // Pause the CPU while the dialog is open so the boot ROM's polling
    // loop doesn't keep spinning while the user picks a file.
    bool was_paused = app().debugger().is_paused();
    if (!was_paused) app().debugger().toggle_pause();

    std::string path = open_file_dialog(hwnd_,
        L"Disk II image\0*.dsk\0All files\0*.*\0\0",
        L"Disk 1: Select .dsk image to mount");

    if (path.empty()) {
        // User cancelled - stop re-prompting until they reselect Disk 1.
        // Boot ROM will continue spinning at BPL until the user resets
        // or mounts an image manually.
        app().bus().set_disk_cancelled();
        app().bus().clear_disk_request();
        if (!was_paused) app().debugger().toggle_pause();
        prompting_for_disk_ = false;
        return;
    }

    // load_file routes .dsk to bus.disk().mount_dsk() and pauses the CPU
    // for the duration; we already paused but it'll just no-op the toggle.
    app().load_file(path);
    if (!app().bus().disk().mounted()) {
        MessageBoxA(hwnd_,
                    "Mount failed. The .dsk image must be exactly 143360 bytes.",
                    "Disk 1", MB_OK | MB_ICONERROR);
        app().bus().set_disk_cancelled();
    }
    app().bus().clear_disk_request();
    if (!was_paused) app().debugger().toggle_pause();
    prompting_for_disk_ = false;
}

void MainWindow::sync_expansions_menu() {
    HMENU m = GetMenu(hwnd_);
    if (!m) return;
    auto check = [&](UINT id, bool on) {
        CheckMenuItem(m, id, MF_BYCOMMAND | (on ? MF_CHECKED : MF_UNCHECKED));
    };
    RamExpansion r = app().settings().ram_expansion();
    check(IDM_EXPANSION_RAM_NONE, r == RamExpansion::None);
    check(IDM_EXPANSION_RAM_8K,   r == RamExpansion::K8);
    check(IDM_EXPANSION_RAM_16K,  r == RamExpansion::K16);
    check(IDM_EXPANSION_RAM_24K,  r == RamExpansion::K24);
    IoCard c = app().settings().io_card();
    check(IDM_EXPANSION_IO_NONE,     c == IoCard::None);
    check(IDM_EXPANSION_IO_CASSETTE, c == IoCard::Cassette);
    check(IDM_EXPANSION_IO_DISK1,    c == IoCard::Disk1);
}

void MainWindow::cmd_set_ram_expansion(RamExpansion r) {
    // Pause the CPU while we change the visible RAM extent so a partial
    // read can't slip through during the transition.  The backing buffer
    // itself is always 32KB so no reallocation happens.
    bool was_paused = app().debugger().is_paused();
    if (!was_paused) app().debugger().toggle_pause();
    Sleep(40);
    app().settings().set_ram_expansion(r);
    app().bus().set_ram_expansion(r);
    if (!was_paused) app().debugger().toggle_pause();
    sync_expansions_menu();
}

void MainWindow::cmd_set_io_card(IoCard c) {
    // Same pause-window discipline as cmd_disk_eject - the CPU may be
    // mid-read inside the soft-switch dispatch.
    bool was_paused = app().debugger().is_paused();
    if (!was_paused) app().debugger().toggle_pause();
    Sleep(40);
    app().settings().set_io_card(c);
    app().bus().set_io_card(c);
    // Reset ACI tape state when switching away from the cassette so the
    // next time it's selected we don't replay stale flip-flop history.
    if (c != IoCard::Cassette) app().bus().reset_tape_state();
    // Re-arm the disk auto-prompt when the user explicitly picks Disk 1.
    if (c == IoCard::Disk1) app().bus().reset_disk_request_state();
    if (!was_paused) app().debugger().toggle_pause();
    sync_expansions_menu();
}

void MainWindow::sync_settings_menu() {
    HMENU m = GetMenu(hwnd_);
    if (!m) return;
    auto check = [&](UINT id, bool on) {
        CheckMenuItem(m, id, MF_BYCOMMAND | (on ? MF_CHECKED : MF_UNCHECKED));
    };
    const Settings& s = app().settings();
    check(IDM_SETTINGS_SCANLINES,        s.scanlines());
    check(IDM_SETTINGS_DOT_ARTIFACT,     s.dot_artifact());
    check(IDM_SETTINGS_TELETYPE_PACING,  s.teletype_pacing());
    check(IDM_SETTINGS_VIGNETTE,         s.vignette());
    check(IDM_SETTINGS_PHOSPHOR_WHITE,   s.phosphor() == Phosphor::White);
    check(IDM_SETTINGS_PHOSPHOR_GREEN,   s.phosphor() == Phosphor::Green);
    check(IDM_SETTINGS_PHOSPHOR_AMBER,   s.phosphor() == Phosphor::Amber);
    // Radio set: bit-level vs byte-level disk latch.
    CheckMenuRadioItem(m, IDM_SETTINGS_DISK_LATCH_BIT,
                          IDM_SETTINGS_DISK_LATCH_BYTE,
                          s.disk_latch() == DiskLatch::Byte
                              ? IDM_SETTINGS_DISK_LATCH_BYTE
                              : IDM_SETTINGS_DISK_LATCH_BIT,
                          MF_BYCOMMAND);
}

void MainWindow::cmd_debugger_goto_memory() {
    u32 addr = 0;
    if (!prompt_hex(hwnd_, L"Goto memory", L"Address (hex):", &addr)) return;
    app().debugger().set_memory_view_addr(static_cast<u16>(addr));
}

void MainWindow::cmd_about() {
    MessageBoxW(hwnd_,
        L"Apple-1 Emulator\n\n"
        L"Cycle-accurate MOS 6502 with the real 1976 ACI ROM.\n"
        L"Direct2D display.\n",
        L"About",
        MB_OK | MB_ICONINFORMATION);
}

void MainWindow::cmd_set_scale(int factor) {
    // Each cell is the natural 2513 glyph size (7x8) times the scale
    // factor.  We also include a 3-cell CRT-style bezel on each side
    // (so total padding is 6 cells wide, 6 cells tall) to fit the
    // bottom-bezel dot artifact pattern.
    int cli_w = (kCols + 6) * 7 * factor;
    int cli_h = (kRows + 6) * 8 * factor;

    RECT rc = { 0, 0, cli_w, cli_h };
    AdjustWindowRect(&rc, GetWindowLongW(hwnd_, GWL_STYLE), TRUE);
    SetWindowPos(hwnd_, nullptr, 0, 0,
                 rc.right - rc.left, rc.bottom - rc.top,
                 SWP_NOMOVE | SWP_NOZORDER);
}

} // namespace apple1::win
