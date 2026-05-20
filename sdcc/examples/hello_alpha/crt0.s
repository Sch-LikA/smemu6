    .module crt0
    .globl _main
    .include "generated_sm6_symbols_sdcc.inc"

SMAKY6_CLI_REPROMPT_SINK .equ 0x56AE

    .area _CODE

start::
    ld sp,#0xF000
    call _main

    ; Verified ordinary .SM exit path back to the Sys2-2 CLI.
    ld a,#0x44
    ld hl,#SMAKY6_SM6_BUFLIN
    jp SMAKY6_CLI_REPROMPT_SINK

halt:
    jr halt