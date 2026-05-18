    .module crt0
    .globl _main
    .include "generated_sm6_symbols_sdcc.inc"

SMAKY6_CLI_REPROMPT_SINK .equ 0x56AE

    .area _CODE

start::
    ld sp,#0xFFFF
    ; Push exit address onto stack, then jump to _main
    ; When _main returns via RET, it will pop and jump to exit_to_cli
    ld hl,#exit_to_cli
    push hl
    jp _main

exit_to_cli::
    ; Verified ordinary .SM exit path back to the Sys2-2 CLI.
    ld a,#0x44
    ld hl,#SMAKY6_SM6_BUFLIN
    jp SMAKY6_CLI_REPROMPT_SINK

halt:
    jr halt