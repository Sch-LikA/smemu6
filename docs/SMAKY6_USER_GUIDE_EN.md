# Smaky 6 — User Guide

*Smaky 6 rev 2-8, SAMOS OS, CLI.SY*

---

## Table of Contents

1. [Overview](#1-overview)
2. [Getting Started](#2-getting-started)
3. [The Command Line](#3-the-command-line)
4. [File Management](#4-file-management)
5. [Directories](#5-directories)
6. [Running Programs](#6-running-programs)
7. [Floppy Disk Management](#7-floppy-disk-management)
8. [Display Modes](#8-display-modes)
9. [Date and Time](#9-date-and-time)
10. [Transfers and Peripherals](#10-transfers-and-peripherals)
11. [System Monitor](#11-system-monitor)
12. [Special Keys](#12-special-keys)
13. [File Extensions](#13-file-extensions)
14. [Error Messages](#14-error-messages)
15. [Quick Reference Table](#15-quick-reference-table)

---

## 1. Overview

The **Smaky 6** is a Swiss microcomputer designed by Jean-Daniel Nicoud at EPFL
(Lausanne) around 1978–1982.  It is powered by a **Zilog Z80 at 2.5 MHz**, has
**64 KB of RAM**, and a **text display of 20 lines × 64 characters** combined with a
superimposable graphics layer of over 30,000 pixels.

The operating system is called **SAMOS** (*SMAky OS*).  The user interface is handled
by **CLI.SY**, a command-line interpreter loaded from the system disk at boot.

**Hardware specifications:**

| Component           | Detail                                                |
|---------------------|-------------------------------------------------------|
| CPU                 | Zilog Z80, 2.5 MHz                                    |
| RAM                 | 64 KB                                                 |
| ROM (Phantom)       | 2 KB (boot manager)                                   |
| Text display        | 20 lines × 64 columns                                 |
| Graphics display    | > 30,000 pixels, superimposable                       |
| Keyboard            | QWERTZ Swiss layout, 57 keys + 7 function keys        |
| Floppy              | Micropolis, 77 tracks × 16 sectors × 256 bytes ≈ 315 KB/disk |
| Drives              | DX0: (lower), DX1: (upper)                            |
| Serial interfaces   | 2 × USART (paper tape, modem)                         |
| Parallel interface  | 1 (printer, external device)                          |
| Speaker             | Programmable buzzer (bit-banged)                      |
| Clock               | Real-time clock with battery backup                   |

---

## 2. Getting Started

Insert a **system disk** into drive **DX0:** (the lower drive) and power on the
Smaky 6.  The Phantom ROM displays a startup prompt.

**Boot key combinations:**

| Combination             | Effect                                             |
|-------------------------|----------------------------------------------------|
| `SHIFT-BREAK`           | Normal boot from **DX0:**                          |
| `FUNCTION-SHIFT-BREAK`  | Boot from **DX1:**                                 |
| `BREAK`                 | PDP-11 format loader (from USART 14)               |
| `FUNCTION-BREAK`        | Memory test (POST)                                 |

After loading, the **`>`** prompt appears — the system is ready.

*Typical boot sequence:*

```
ROM de chargement rev 1-7
SAMOS  rev 2-8
DX0:
>
```

---

## 3. The Command Line

The **`>`** prompt means the system is waiting for a command.  Commands are
**not** case-sensitive — `LIST`, `list`, and `List` are all equivalent.

**Keyboard shortcuts:**

| Key                   | Effect                                                    |
|-----------------------|-----------------------------------------------------------|
| `TAB`                 | Inserts `DX1:` into the command line                      |
| `BREAK` (ESC)         | Cancels current line; if empty, recalls last command      |
| `KILL` (function key) | Aborts current peripheral transfer                        |
| `SHIFT-BREAK`         | Hardware reset → reboot from DX0:                         |

**General syntax:**

```
> COMMAND [argument]
```

Arguments containing spaces must be enclosed in single quotes (`'`).

---

## 4. File Management

### 4.1 List Files

```
> LIST
> LIST DX1:
> LIST MYFILE.SM
```

Displays the contents of the current directory, or a specific drive/file.
Columns show: name, size in bytes, protection attributes.

Equivalents: DOS `DIR` — Unix `ls -l`

---

### 4.2 Copy a File

```
> COPY SOURCE.SM
> COPY SOURCE.SM DEST.SM
> COPY DX0:SOURCE.SM DX1:DEST.SM
```

Copies a file on the same drive or from one drive to another.
If the destination filename is not specified, the source name is kept.

**Tip:** Press `TAB` to insert `DX1:` automatically.

Equivalents: DOS `COPY SOURCE DEST` — Unix `cp SOURCE DEST`

---

### 4.3 Delete a File

```
> DELETE MYFILE.SM
```

Permanently deletes a file.  No confirmation is requested.
A file marked **permanent** (`ERROR 4`) cannot be deleted.

Equivalents: DOS `DEL` — Unix `rm`

---

### 4.4 Display File Contents

```
> TYPE MYFILE.BS
```

Displays the contents of a text file on screen.  Press `KILL` to interrupt.

Equivalents: DOS `TYPE` — Unix `cat` / `less`

---

### 4.5 Print a File

```
> PRINT MYFILE.BS
> PRINT MYFILE.BS $LP
```

Prints a file on the connected printer (requires `LP.SY` loaded).

Equivalents: DOS `PRINT` — Unix `lpr`

---

### 4.6 Append Files

```
> APPEND FILE1.BS FILE2.BS
```

Appends the contents of `FILE2.BS` to the end of `FILE1.BS`.

Equivalents: DOS `COPY A+B A` — Unix `cat B >> A`

---

### 4.7 Rename a File

```
> SET OLD.SM NEW.SM
```

Renames (or moves) a file.  Can also modify protection attributes.

Equivalents: DOS `REN` — Unix `mv`

---

### 4.8 Compact Disk Space

```
> COMPRESS
```

Compacts the directory by removing deleted entries and consolidating free space.
Run periodically to maintain performance.

Equivalents: DOS `DEFRAG` (partial analogy) — Unix: no direct equivalent.

---

## 5. Directories

The Smaky 6 uses **subdirectories** stored as files with the `.DR` extension.

```
> CDIR              — displays current directory
> CDIR PROJECTS     — enters the PROJECTS subdirectory
> CDIR ..           — goes up one level (if supported)
> CLEAR             — clears screen and returns to root directory
```

Equivalents: DOS `CD` / `DIR` — Unix `cd` / `ls`

---

## 6. Running Programs

To run a program (`.SM` file), simply type its name without the extension:

```
> SMILE           — launches the SMILE assembler
> PASCAL          — launches the Pascal interpreter
> BASIC           — launches the BASIC interpreter
```

`.MC` files are **CLI macros** (command scripts) and are invoked the same way.
The system distinguishes them automatically.

Equivalents: DOS `.EXE` / `.COM` — Unix `./program`

### LOAD

```
> LOAD
```

Reloads the last executed program from disk.  Useful after a crash or to quickly
re-run the same program.

---

## 7. Floppy Disk Management

### 7.1 Initialize (Format) a Disk

```
> INIT DX1:
```

Formats and initialises a blank floppy in drive **DX1:**.
**All data is erased.**  The disk must be in the drive before running this command.

Equivalents: DOS `FORMAT A:` — Unix `mkfs`

---

### 7.2 Set Default Drive

```
> DX0:
> DX1:
```

Selects the default drive for subsequent operations.
The `TAB` shortcut inserts `DX1:` directly in the command line.

Equivalents: DOS `A:` / `B:` — Unix `cd /mnt/dx0`

---

## 8. Display Modes

The Smaky 6 has two independent video layers: **text (alpha)** and **graphics**.
The `MODE` command enables them separately or together.

```
> MODE          — text-only mode (same as MODE A)
> MODE A        — text only
> MODE G        — graphics only
> MODE G P      — graphics, fine-pixel variant
> MODE 2        — text AND graphics superimposed
> MODE 2 P      — text + graphics, fine pixels
```

---

## 9. Date and Time

```
> STIME  15:30     — sets the time to 15:30
> SDATE  09/05/83  — sets the date to 9 May 1983
> SDAY   SATURDAY  — sets the day of the week
```

Equivalents: DOS `TIME` / `DATE` — Unix `date -s "..."`

---

## 10. Transfers and Peripherals

Peripherals are referenced with a `$` prefix in `COPY`, `TYPE`, `PRINT`, `APPEND`:

| Name   | Dir.   | Hardware                                    |
|--------|--------|---------------------------------------------|
| `$PR`  | Input  | Paper tape reader (USART 4, 20 mA loop)     |
| `$PP`  | Output | Paper tape punch (USART 4)                  |
| `$PI`  | Input  | Parallel interface                          |
| `$PO`  | Output | Parallel interface                          |
| `$MI`  | Input  | Modem (USART 6)                             |
| `$MO`  | Output | Modem (USART 6)                             |
| `$LP`  | Output | Line printer (overlay LP.SY required)       |
| `$KEY` | Input  | Keyboard                                    |
| `$DIS` | Output | Display                                     |

**Examples:**

```
> COPY MYFILE.BS $LP        — print to line printer
> COPY $PR MYFILE.BS        — read paper tape into a file
> COPY MYFILE.BS $MO        — send a file via modem
```

Press **KILL** to interrupt any transfer.

---

## 11. System Monitor

```
> MON
```

Enters the **machine monitor** (SYSMON).  This low-level mode lets you examine and
modify memory, execute machine code, and debug programs.

To return to the CLI command line, type the monitor's exit command.

Equivalents: DOS `DEBUG` — Unix `gdb` / `xxd`

---

## 12. Special Keys

| Key                     | Context         | Effect                                           |
|-------------------------|-----------------|--------------------------------------------------|
| `TAB`                   | Command line    | Inserts `DX1:` into the line                     |
| `BREAK` / `ESC`         | Command line    | Cancels line; if empty, recalls previous one     |
| `KILL`                  | Anywhere        | Aborts current transfer or print job             |
| `SHIFT-BREAK`           | Boot / runtime  | Hard reset → reboot from DX0:                    |
| `FUNCTION-SHIFT-BREAK`  | Boot            | Reboot from DX1:                                 |
| `FUNCTION-BREAK`        | Boot            | Memory test (POST)                               |
| `CURSOR`  (F1)          | Programs        | Program function key                             |
| `SEARCH`  (F2)          | Programs        | Program function key                             |
| `KILL`    (F3)          | Programs        | Program function key / abort transfer            |
| `PROGRA`  (F4)          | Programs        | Program function key                             |
| `SHOW`    (F5)          | Programs        | Program function key                             |
| `COPY`    (F6)          | Programs        | Program function key                             |
| `CHANGE`  (F7)          | Programs        | Program function key                             |

---

## 13. File Extensions

| Extension | Type                                    |
|-----------|-----------------------------------------|
| `.SY`     | System file (OS)                        |
| `.SM`     | Executable program                      |
| `.MC`     | CLI macro (command script)              |
| `.BS`     | BASIC source                            |
| `.SR`     | SMILE assembler source                  |
| `.DR`     | Directory (subdirectory)                |
| `.LS`     | Assembler listing                       |
| `.ST`     | Symbol table                            |
| `.IM`     | 1-bpp bitmap image                      |
| `.HP`     | Help file (text)                        |

The official user manual also calls out two concrete `.ST` files commonly found
on system media:

- `FLO.ST`: symbol table used by programs with `.REF FLO`
- `SM6.ST`: short symbol table used by programs with `.REF SM6`

Together with `LP.SY` (printer management overlay), these files are a useful
reminder that system disks can carry development-oriented metadata in addition
to runnable programs. For emulator and tooling work, `FLO.ST` and `SM6.ST`
look especially promising as recoverable symbol sources for future Smaky 6
development tooling such as the SDCC bring-up effort.

---

## 14. Error Messages

Errors are displayed as `ERROR nnn` where `nnn` is the code in **octal**.

| Dec | Oct  | English message (manual)  | French string (ER.SY)            |
|-----|------|---------------------------|----------------------------------|
|   1 | 001  | Write protect file        | fichier protégé écriture         |
|   2 | 002  | Read protect file         | fichier protégé lecture          |
|   4 | 004  | Permanent file            | fichier permanent                |
|   5 | 005  | Line too long             | ligne trop longue                |
|   6 | 006  | End of file               | fin de fichier                   |
|   7 | 007  | File end overflow         | dépassement fin de fichier       |
|  10 | 012  | File in use for writing   | fichier ouvert en écriture       |
|  11 | 013  | File already exists       | fichier déjà existant            |
|  12 | 014  | File does not exist       | fichier inexistant               |
|  13 | 015  | Illegal filename          | nom de fichier illégal           |
|  14 | 016  | Illegal reservation       | réservation illégale             |
|  16 | 020  | Cannot load file          | chargement impossible            |
|  17 | 021  | Out of file               | plus de fichier                  |
|  20 | 024  | File in use for reading   | fichier ouvert en lecture        |
|  21 | 025  | Unknown device            | périphérique inconnu             |
|  22 | 026  | Channel error             | erreur de canal                  |
|  23 | 027  | File(s) in use            | fichier(s) en cours              |
|  24 | 030  | All channels in use       | tous les canaux occupés          |
|  25 | 031  | Directory full            | répertoire plein                 |
|  26 | 032  | Disk full                 | disque plein                     |
|  30 | 036  | Device timeout            | timeout périphérique             |
|  31 | 037  | Write protect tab set     | languette de protection          |
|  32 | 040  | Write error               | erreur d'écriture                |
|  33 | 041  | Read error                | erreur de lecture                |
|  34 | 042  | No starting address       | pas d'adresse de départ          |
|  35 | 043  | Bad load                  | chargement erroné                |
|  36 | 044  | Buffer full               | tampon plein                     |
| 110 | 156  | Illegal order             | ordre illégal                    |
| 114 | 162  | System error              | erreur système                   |
| 115 | 163  | Map error                 | erreur de map                    |

**Example:** `ERROR 043` = octal 043 = decimal 35 = *Bad load* (unreadable disk).

---

## 15. Quick Reference Table

| Smaky 6 command  | Description                     | DOS              | Unix/Linux           |
|------------------|---------------------------------|------------------|----------------------|
| `LIST`           | List directory                  | `DIR`            | `ls -l`              |
| `LIST DX1:`      | List drive DX1:                 | `DIR B:`         | `ls /mnt/dx1`        |
| `CDIR NAME`      | Change directory                | `CD NAME`        | `cd NAME`            |
| `COPY SRC DEST`  | Copy a file                     | `COPY SRC DEST`  | `cp SRC DEST`        |
| `DELETE NAME`    | Delete a file                   | `DEL NAME`       | `rm NAME`            |
| `SET OLD NEW`    | Rename a file                   | `REN OLD NEW`    | `mv OLD NEW`         |
| `TYPE NAME`      | Display file contents           | `TYPE NAME`      | `cat NAME`           |
| `PRINT NAME`     | Print a file                    | `PRINT NAME`     | `lpr NAME`           |
| `APPEND A B`     | Append files                    | `COPY A+B A`     | `cat B >> A`         |
| `COMPRESS`       | Compact disk space              | `DEFRAG`         | *(n/a)*              |
| `INIT DX1:`      | Format a floppy disk            | `FORMAT B:`      | `mkfs /dev/fd1`      |
| `MODE A`         | Text-only display               | `MODE CO80`      | *(n/a)*              |
| `MODE G`         | Graphics-only display           | `MODE BW320`     | *(n/a)*              |
| `MODE 2`         | Text + graphics                 | *(n/a)*          | *(n/a)*              |
| `STIME HH:MM`    | Set the time                    | `TIME`           | `date -s "HH:MM"`    |
| `SDATE DD/MM/YY` | Set the date                    | `DATE`           | `date -s "..."`      |
| `HELP`           | Online help                     | `HELP`           | `man` / `--help`     |
| `MON`            | Machine monitor (debugger)      | `DEBUG`          | `gdb` / `xxd`        |
| `LOAD`           | Reload last program             | *(n/a)*          | *(n/a)*              |
| `STP`            | Stop / halt the system          | *(n/a)*          | `halt`               |
| `PROGNAME`       | Run a program                   | `PROGNAME.EXE`   | `./progname`         |
| `DX0:` / `DX1:` | Select default drive            | `A:` / `B:`      | `cd /mnt/dx0`        |
| `SHIFT-BREAK`    | Hard reset                      | `Ctrl+Alt+Del`   | `reboot`             |

---

*Document based on: original Smaky 6 rev 2-8 manual (EPFL/Epsitec), reverse-engineering
of CLI.SY and SYS.SY, and the Smemu6 emulator.*
