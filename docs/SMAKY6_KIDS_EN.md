# The Smaky 6 Explained for Kids
## How a Real Retro Computer Works!

*For curious minds aged 10 and up who want to understand how computers really work.*

---

## Welcome!

In front of you is the **Smaky 6**, a computer made in Switzerland in the 1980s —
when your parents or grandparents might have been as young as you are now!

Back then, computers had no mouse, no icons to click, no YouTube videos.
To do anything, you had to **type commands on the keyboard**.
That's what you'll learn in this guide!

---

## Part 1: How Does a Computer Work?

### The Computer's Brain: the Processor (CPU)

Imagine the computer is a kitchen.

The **processor** (or CPU) is the chef.
It follows recipes (the programs) and does one thing at a time, very very fast.

The Smaky 6's processor is called the **Zilog Z80**.
It works at a speed of **2.5 million operations per second**!

Is that a lot? Yes... in 1980.
Today, your phone does **billions** of operations per second.
But the Z80 was enough to do calculations, display text, play games,
and write programs. The essentials!

> **Remember:**
> CPU = the chef of the computer.
> It executes instructions, one by one, very quickly.

---

### Working Memory (RAM): the Worktop

Still in our kitchen: **RAM** (Random Access Memory) is the worktop.
It's where the chef puts the ingredients being used right now.

The Smaky 6 has **64 KB of RAM**.
"KB" means "kilobytes". 1 byte can store one letter or one digit.
64 KB ≈ 64,000 letters.

It's like having only two sheets of A4 paper to write down **everything**
your program needs at the same time.

*Comparison:* Your computer or tablet probably has 4,000,000 KB (= 4 GB) of RAM.
That's **65,000 times more** than the Smaky 6!

> **Important:**
> RAM is **temporary**. When you turn the computer off, everything in RAM
> disappears. That's why we save things to floppy disk!

---

### Read-Only Memory (ROM): the Basic Recipe Book

**ROM** (Read-Only Memory) is a recipe book that never changes.
It holds the computer's startup instructions.

On the Smaky 6, it's called the **"Phantom ROM"**.
It contains a tiny program (only 2 KB!) that knows how to read the floppy disk
and start the real operating system.

> **Difference between ROM and RAM:**
> - ROM = read-only, never changes, survives a restart
> - RAM = read and write, erased when power is off

---

### The Floppy Disk: the Recipe Drawer

The **floppy disk** is like a drawer full of recipes.
It keeps information even when the computer is turned off.

The Smaky 6 uses **Micropolis** floppy disks:
- 77 tracks (like the grooves on a vinyl record)
- 16 sectors per track
- 256 bytes per sector
- **Total ≈ 315,000 bytes = 315 KB**

A program like Microsoft Word today is about 500,000 KB.
It would **not fit at all** on a Smaky 6 floppy disk!

> **Tip:** The Smaky 6 has two floppy drives:
> - **DX0:** = the bottom drive (like a main drawer)
> - **DX1:** = the top drive (like a backup drawer)

---

### The Screen: the Window

The Smaky 6's screen can display:
- **Text**: 20 rows of 64 characters (like 20 rows of 64 boxes)
- **Drawings**: over 30,000 glowing dots that can be turned on or off

There are no colours — everything is in **black and white**
(or green on black, depending on the monitor model).

---

## Part 2: Starting the Smaky 6

### How to Start?

1. Insert the **system disk** into drive **DX0:** (the bottom one)
2. Turn the computer on
3. Press **SHIFT-BREAK** (the SHIFT key + the BREAK key)
4. Wait for the `>` message to appear

The `>` is called a **command prompt**. It's the computer saying:
"I'm ready! What would you like to do?"

### What You'll See at Boot:

```
ROM de chargement rev 1-7
SAMOS  rev 2-8
DX0:
>
```

- **ROM de chargement**: the Phantom ROM is doing its job
- **SAMOS**: the operating system (like Windows, but much simpler)
- **DX0:** : the computer loaded from drive DX0:
- **`>`**: ready!

---

### The Magic Boot Keys

| Key                    | What happens                                       |
|------------------------|----------------------------------------------------|
| `SHIFT-BREAK`          | Normal startup (from DX0:)                         |
| `FUNCTION-SHIFT-BREAK` | Start from DX1: (the other disk)                   |
| `FUNCTION-BREAK`       | Memory test (checks that the RAM is working)       |

---

## Part 3: Your First Commands

### The LIST Command: see your files

```
> LIST
```

Type `LIST` and press ENTER.
The computer shows you all the files on the floppy disk.

It's like opening a drawer to see what's inside!

> **On your computer today:** You'd double-click a folder to see its contents.
> This is the same thing, but in text.

---

### The COPY Command: copy a file

```
> COPY MY_GAME.SM DX1:
```

This command copies the file `MY_GAME.SM` from the main drive (DX0:)
to the second drive (DX1:).

**Super useful tip:** Press the `TAB` key and the computer
automatically types `DX1:` for you! The Smaky 6's creators had
thought of everything.

> **On your computer today:** Ctrl+C then Ctrl+V — but the Smaky 6
> had no mouse to select files with!

---

### The DELETE Command: delete a file

```
> DELETE OLD_FILE.BS
```

**Watch out!** On the Smaky 6, there is no recycle bin.
When you delete a file, it is **truly** deleted, right away.
The computer doesn't ask "are you sure?"

It's fast... but it can be scary!

---

### The TYPE Command: read a text file

```
> TYPE RECIPE.BS
```

Shows the contents of a file on screen, line by line.

> **On your computer today:** You'd open the file in a text editor
> or use "Preview".

---

### Running a Program

To run a program, you simply type its name (without the `.SM` extension):

```
> BASIC
```

This command launches the BASIC interpreter — a programming language
that many children of the era used to write their first programs!

---

## Part 4: Files and Their Names

### What Are Files Called?

On the Smaky 6, each file has a **name** and an **extension** separated by a dot.

| Example name  | What it is                          |
|---------------|-------------------------------------|
| `GAME.SM`     | A program (game) you can run        |
| `TEXT.BS`     | Text written in BASIC               |
| `DRAWING.IM`  | A picture                           |
| `PROJECTS.DR` | A folder (subdirectory)             |
| `SYS.SY`      | A system file (important!)          |

> **Important:** Never delete `.SY` files!
> These are the operating system files. Without them, the computer
> can't start any more. It's like deleting the chef from the kitchen!

---

## Part 5: What Is an Operating System?

You may have heard of Windows, macOS or Linux.
These are **operating systems** (OS for short).

On the Smaky 6, the system is called **SAMOS** (SMAky OS).

### What Is It For?

The operating system bridges the gap between **you** (typing commands)
and the **hardware** (the processor, the floppy disk, the screen).

Without an operating system, you'd have to tell the computer exactly where
to write each bit on the disk, how to light up each dot on the screen...
The operating system simplifies all of that.

### How Does It Start?

1. The **Phantom ROM** (2 KB) starts up and looks for the floppy disk
2. It loads **SYS.SY** from the floppy into RAM
3. SYS.SY loads **CLI.SY** (the command-line interpreter)
4. CLI.SY displays `>` and waits for your commands

It's like a domino chain: each step starts the next one!

---

## Part 6: Error Messages

Sometimes something doesn't go as planned.
The Smaky 6 then displays an `ERROR` message followed by a number.

The numbers are written in **octal** — a way of counting
using only the digits 0 to 7 (instead of 0 to 9).

| Message      | What it means                        | What to do                      |
|--------------|--------------------------------------|---------------------------------|
| `ERROR 014`  | File not found                       | Check the filename              |
| `ERROR 013`  | The file already exists              | Choose a different name         |
| `ERROR 032`  | Floppy disk is full                  | Delete some files               |
| `ERROR 043`  | Cannot load the file                 | Check the floppy disk           |
| `ERROR 004`  | Protected file (permanent)           | Cannot be deleted               |

> **Why octal?**
> The first computer engineers often worked in octal (base 8)
> because it's convenient with groups of 3 bits.
> Binary (0s and 1s) is the computer's language —
> octal was a shorter way to write it down.

---

## Part 7: Sound

The Smaky 6 has a **speaker** that can produce sounds.

When the system goes "BEEP!", here's what actually happens:

1. The program writes a value to **port 0x03** (a special address for sound)
2. This rapidly moves a tiny membrane inside the speaker
3. The membrane vibrates and creates a sound wave
4. Your ear hears "BEEP!"

The Smaky 6's beep is a **square wave** at about **584 Hz** —
that's between D and E-flat on a piano!

---

## Part 8: The Special Keyboard

The Smaky 6 has a keyboard that's a bit different from today's keyboards.
As well as the normal letters and numbers, it has **7 special function keys**:

| Key      | On the emulator | What it does                 |
|----------|-----------------|------------------------------|
| `CURSOR` | F1              | Depends on the program       |
| `SEARCH` | F2              | Depends on the program       |
| `KILL`   | F3              | Stops whatever is running    |
| `PROGRA` | F4              | Depends on the program       |
| `SHOW`   | F5              | Depends on the program       |
| `COPY`   | F6              | Depends on the program       |
| `CHANGE` | F7              | Depends on the program       |

Each program decides what most of these keys do.
Only `KILL` (F3 in the emulator mapping) is the one you can usually trust to
mean **stop**.

---

## Summary: the Essential Commands

| Command            | What it does                          |
|--------------------|---------------------------------------|
| `LIST`             | See the files                         |
| `COPY NAME DX1:`   | Copy a file to DX1:                   |
| `DELETE NAME`      | Delete a file                         |
| `TYPE NAME`        | Read a text file                      |
| `BASIC`            | Launch the BASIC interpreter          |
| `SMILE`            | Launch the SMILE assembler            |
| `INIT DX1:`        | Prepare a new floppy disk             |
| `MODE G`           | Display graphics                      |
| `MODE A`           | Display text only                     |

---

## Want to Learn More?

If you want to know even more about how computers work:

- **Binary:** All computers only understand 0 and 1.
  The letter "A" = `01000001` in binary.

- **The BASIC language:** An easy programming language to learn.
  On the Smaky 6, you could write:
  ```
  10 PRINT "HELLO!"
  20 GOTO 10
  ```
  And the computer would print "HELLO!" forever!

- **Assembly language:** The language closest to machine code.
  You write the actual instructions that the Z80 understands.
  It's difficult but very powerful!

- **How a processor works:** The Z80 reads one instruction at a time,
  does the operation (add two numbers, jump to another place in the program,
  read from a key...), and moves on to the next instruction.
  It does this 2,500,000 times every second!

---

*This guide was written to explain how the Smaky 6 works —
a real Swiss computer from the 1980s that has been recreated as an emulator.
The original Smaky 6 was designed by Jean-Daniel Nicoud at EPFL in Lausanne, Switzerland.*
