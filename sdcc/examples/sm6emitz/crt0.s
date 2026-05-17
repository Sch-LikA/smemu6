    .module crt0
    .globl _main
    .globl _smaky6_emit_text
    .include "generated_sm6_symbols_sdcc.inc"

    .area _CODE

start::
    ld sp,#0xF000
    call _main

    ld hl,#(SMAKY6_SM6_ALPHA + (19 * 64))
    ld de,#after_main_label
    call copy_zstr

    ld hl,#0
    add hl,sp
    ex de,hl
    ld hl,#(SMAKY6_SM6_ALPHA + (19 * 64) + 14)
    call write_hex16_de

    ld (hl),#' '
    inc hl
    ld (hl),#'T'
    inc hl
    ld (hl),#'O'
    inc hl
    ld (hl),#'P'
    inc hl
    ld (hl),#'='
    inc hl

    ld b,h
    ld c,l
    ld hl,#0
    add hl,sp
    ld e,(hl)
    inc hl
    ld d,(hl)
    ld h,b
    ld l,c
    call write_hex16_de
    ld (hl),#'!'

    ; After ?DITEX, return through the documented system exit path.
    jp SMAKY6_SM6__RTN

_smaky6_emit_text::
    rst 0x20
    .db 0x06

    ld hl,#(SMAKY6_SM6_ALPHA + (16 * 64))
    ld (hl),#'S'
    inc hl
    ld (hl),#'P'
    inc hl
    ld (hl),#'='
    inc hl

    ld hl,#0
    add hl,sp
    ex de,hl
    ld hl,#(SMAKY6_SM6_ALPHA + (16 * 64) + 3)
    call write_hex16_de

    ld (hl),#' '
    inc hl
    ld (hl),#'T'
    inc hl
    ld (hl),#'O'
    inc hl
    ld (hl),#'P'
    inc hl
    ld (hl),#'='
    inc hl

    ld b,h
    ld c,l
    ld hl,#0
    add hl,sp
    ld e,(hl)
    inc hl
    ld d,(hl)
    ld h,b
    ld l,c
    call write_hex16_de

    ld (hl),#'!'
001$:
    ret

copy_zstr:
00101$:
    ld a,(de)
    or a,a
    ret z
    ld (hl),a
    inc hl
    inc de
    jr 00101$

write_hex16_de:
    ld a,d
    call write_hex8_a
    ld a,e
    call write_hex8_a
    ret

write_hex8_a:
    push af
    rrca
    rrca
    rrca
    rrca
    call write_hex_nibble_a
    pop af
    call write_hex_nibble_a
    ret

write_hex_nibble_a:
    and #0x0f
    add a,#0x30
    cp #0x3a
    jr c,00102$
    add a,#0x07
00102$:
    ld (hl),a
    inc hl
    ret

after_main_label:
    .ascii "AFTER MAIN SP="
    .db 0