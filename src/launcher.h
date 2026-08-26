// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2024-2026 Marcel Prisi
/* launcher.h – Smaky 6 SDL2 startup configuration dialog */
#ifndef LAUNCHER_H
#define LAUNCHER_H

enum LauncherStorageMode {
    LAUNCHER_STORAGE_FLOPPY = 0,
    LAUNCHER_STORAGE_HOSTDIR = 1,
    LAUNCHER_STORAGE_HARDDISK = 2,
};

/*
 * LauncherHints carries CLI-provided values into launcher_run().
 * Any field left at its sentinel is ignored and the launcher default is used.
 *   char *  → NULL    (not provided on CLI)
 *   int     → -1      (not provided on CLI)
 */
typedef struct {
    int         dx0_mode;          /* enum LauncherStorageMode; -1=not set */
    const char *dx0_path;          /* NULL = not provided */
    int         dx1_mode;          /* floppy or hostdir; -1=not set */
    const char *dx1_path;          /* NULL = not provided */
    int         scale;             /* 1..4; -1 = not set */
    int         phosphor_white;    /* 0=green, 1=white; -1 = not set */
    int         scanlines;         /* 0/1; -1 = not set */
    int         no_display_off;    /* 0/1; -1 = not set */
    int         beeper;            /* 0/1; -1 = not set */
    int         drive_sound;       /* 0/1; -1 = not set */
    int         psg;               /* 0/1; -1 = not set */
} LauncherHints;

/*
 * LauncherConfig is filled by launcher_run() and consumed by main().
 * All string pointers point into heap-allocated memory that survives
 * beyond launcher_run() (caller must not free them).
 *
 * Fields that were NOT touched by the launcher keep their sentinel values:
 *   char *  → NULL   (means "not set; keep CLI / default value")
 *   int     → -1     (means "not set; keep CLI / default value")
 *   bool    → -1     (same sentinel)
 */
typedef struct {
    /* Storage */
    int         dx0_mode;          /* enum LauncherStorageMode; -1 = not set */
    char       *dx0_path;          /* NULL = not set */
    int         dx1_mode;          /* floppy or hostdir; -1 = not set */
    char       *dx1_path;          /* NULL = not set */

    /* Screen */
    int         scale;             /* 1..4; -1 = not set */
    int         phosphor_white;    /* 0=green, 1=white; -1 = not set */
    int         scanlines;         /* 0/1; -1 = not set */
    int         no_display_off;    /* 0/1; -1 = not set */

    /* Sound */
    int         beeper;            /* 0/1; -1 = not set */
    int         drive_sound;       /* 0/1; -1 = not set */
    int         psg;               /* 0/1; -1 = not set */
} LauncherConfig;

/*
 * launcher_run() opens the SDL configuration dialog, blocks until the user
 * clicks Start (or closes the window), then fills *cfg and returns.
 *
 * hints may be NULL or point to a LauncherHints struct with CLI-provided
 * values that pre-populate the launcher controls.
 *
 * Returns  0 on normal Start,
 *         -1 if the user closed the window without clicking Start (caller
 *            should treat this as an exit request).
 *
 * In headless mode (SDL_VIDEODRIVER=dummy) the function fills *cfg with
 * defaults and returns 0 immediately without creating any window.
 */
int launcher_run(LauncherConfig *cfg, const LauncherHints *hints);

#endif /* LAUNCHER_H */
