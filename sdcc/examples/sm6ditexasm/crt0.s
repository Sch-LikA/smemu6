    .module crt0
    .include "generated_sm6_symbols_sdcc.inc"

SMAKY6_CLI_REPROMPT_SINK .equ 0x56AE

    .area _CODE

start::
    ld sp,#0xF000

    call write_probe_labels

    ld hl,#emit_text
    rst 0x20
    .db 0x06

    ld a,#0x44
    ld hl,#SMAKY6_SM6_BUFLIN
    jp SMAKY6_CLI_REPROMPT_SINK

write_probe_labels:
    ld hl,#(SMAKY6_SM6_ALPHA + (8 * 64))
    ld de,#label_row8
    call copy_zstr

    ld hl,#(SMAKY6_SM6_ALPHA + (9 * 64))
    ld de,#label_row9
    call copy_zstr

    ld hl,#(SMAKY6_SM6_ALPHA + (15 * 64))
    ld de,#label_row15
    call copy_zstr
    ret

copy_zstr:
001$:
    ld a,(de)
    or a,a
    ret z
    ld (hl),a
    inc hl
    inc de
    jr 001$

label_row8:
    .ascii "ASM RST20/06 CALL"
    .db 0

label_row9:
    .ascii "HAND-WRITTEN CRT0"
    .db 0

label_row15:
    .ascii "EXPECTED EMITTER LINE:"
    .db 0

emit_text:
    .ascii "RST20/06 CALL OK"
    .db 0