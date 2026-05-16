    .module crt0
    .globl _main

    .area _CODE

start::
    ld sp,#0xF000
    call _main

halt:
    jr halt