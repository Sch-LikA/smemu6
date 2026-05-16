    .module crt0
    .globl _main

    .area _CODE

start::
    ld sp,#0xF000
    call _main

    ; Verified ordinary .SM exit path back to the Sys2-2 CLI.
    ld a,#0x44
    ld hl,#0x45C0
    jp 0x56AE

halt:
    jr halt