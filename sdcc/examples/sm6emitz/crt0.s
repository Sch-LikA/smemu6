    .module crt0
    .globl _main
    .globl _smaky6_emit_text
    .include "generated_sm6_symbols_sdcc.inc"

    .area _CODE

start::
    call _main

    ; Verified ordinary .SM exit path back to the Sys2-2 CLI.
    ld a,#0x44
    ld hl,#SMAKY6_SM6_BUFLIN
    jp 0x56AE

_smaky6_emit_text::
    rst 0x20
    .db 0x06
001$:
    ret