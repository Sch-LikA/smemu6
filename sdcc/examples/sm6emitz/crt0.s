    .module crt0
    .globl _main
    .globl _smaky6_emit_text
    .include "generated_sm6_symbols_sdcc.inc"

    .area _CODE

start::
    ld sp,#0xF000
    call _main

    ; After ?DITEX, return through the documented system exit path.
    jp SMAKY6_SM6__RTN

_smaky6_emit_text::
    rst 0x20
    .db 0x06
001$:
    ret