// Contributor: Phillip Allison (github.com/philtimmes)
// This file includes changes Phillip contributed to the Apple-1 Emulator.
// See CONTRIBUTORS.md for the full list of his work.

// resource.h - menu / accelerator IDs shared between resource.rc and code.

#pragma once

// File menu
#define IDM_FILE_OPEN              101
#define IDM_FILE_EXIT              102

// CPU menu
#define IDM_CPU_CLEAR_SCREEN       200
#define IDM_CPU_RESET              201
#define IDM_CPU_PAUSE              202
#define IDM_CPU_STEP               203
#define IDM_CPU_STEP_OVER          204
#define IDM_CPU_RUN_TO_PC          205

// Cassette menu
#define IDM_CASSETTE_SAVE          301
#define IDM_CASSETTE_LOAD_FAST     302
#define IDM_CASSETTE_STAGE_ACI     303

// Disk II menu
#define IDM_DISK_MOUNT             350
#define IDM_DISK_EJECT             351

// Debugger menu
#define IDM_DEBUGGER_SHOW          401
#define IDM_DEBUGGER_TOGGLE_BP     402
#define IDM_DEBUGGER_GOTO_MEM      403
#define IDM_DEBUGGER_CLEAR_BPS     404

// View menu
#define IDM_VIEW_SCALE_TINY        500
#define IDM_VIEW_SCALE_1X          501
#define IDM_VIEW_SCALE_2X          502
#define IDM_VIEW_SCALE_3X          503
#define IDM_VIEW_SCALE_HUGE        504

// Settings menu
#define IDM_SETTINGS_SCANLINES         700
#define IDM_SETTINGS_DOT_ARTIFACT      701
#define IDM_SETTINGS_TELETYPE_PACING   702
#define IDM_SETTINGS_VIGNETTE          703
#define IDM_SETTINGS_PHOSPHOR_WHITE    710
#define IDM_SETTINGS_PHOSPHOR_GREEN    711
#define IDM_SETTINGS_PHOSPHOR_AMBER    712
#define IDM_SETTINGS_DISK_LATCH_BIT    720
#define IDM_SETTINGS_DISK_LATCH_BYTE   721

// Expansions menu
#define IDM_EXPANSION_RAM_NONE         800
#define IDM_EXPANSION_RAM_8K           801
#define IDM_EXPANSION_RAM_16K          802
#define IDM_EXPANSION_RAM_24K          803
#define IDM_EXPANSION_IO_NONE          810
#define IDM_EXPANSION_IO_CASSETTE      811
#define IDM_EXPANSION_IO_DISK1         812

// Help menu
#define IDM_HELP_ABOUT             601

// Resources
#define IDR_MAIN_MENU              1000
#define IDR_MAIN_ACCEL             900
#define IDI_APPICON                1100
