    .module crt0
    .globl _main
    .include "generated_sm6_symbols_sdcc.inc"

    .area _CODE

start::
    ld sp,#0xFFFF
    ; Push exit address onto stack, then jump to _main
    ; When _main returns via RET, it will pop and jump to exit_to_cli
    ld hl,#exit_to_cli
    push hl
    jp _main

exit_to_cli::
    ; Proper exit sequence
    ld a,#0x44
    ld hl,#SMAKY6_SM6_BUFLIN
    jp 0x56AE
    
    ; Should never reach here
halt:
    jr halt

_smaky6_emit_text::
    rst 0x20
    .db 0x06
    ret
