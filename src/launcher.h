/* launcher.h – Smaky 6 SDL2 startup configuration dialog */
#ifndef LAUNCHER_H
#define LAUNCHER_H

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
    int         dx0_is_harddisk;   /* 0=floppy, 1=harddisk; -1=not set */
    char       *dx0_path;          /* NULL = not set */
    char       *dx1_path;          /* NULL = not set (always floppy) */
    int         autoboot;          /* 0/1; -1 = not set */

    /* Screen */
    int         scale;             /* 1..4; -1 = not set */
    int         phosphor_white;    /* 0=green, 1=white; -1 = not set */
    int         scanlines;         /* 0/1; -1 = not set */
    int         no_display_off;    /* 0/1; -1 = not set */

    /* Sound */
    int         beeper;            /* 0/1; -1 = not set */
} LauncherConfig;

/*
 * launcher_run() opens the SDL configuration dialog, blocks until the user
 * clicks Start (or closes the window), then fills *cfg and returns.
 *
 * Returns  0 on normal Start,
 *         -1 if the user closed the window without clicking Start (caller
 *            should treat this as an exit request).
 *
 * In headless mode (SDL_VIDEODRIVER=dummy) the function fills *cfg with
 * defaults and returns 0 immediately without creating any window.
 */
int launcher_run(LauncherConfig *cfg);

#endif /* LAUNCHER_H */
