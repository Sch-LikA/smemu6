    .module crt0
    .globl _main
    .globl _smaky6_emit_text
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

write_exit_snapshot:
    ld hl,#(SMAKY6_SM6_ALPHA + (12 * 64))
    ld de,#exit_row12_af
    call copy_zstr

    ld b,h
    ld c,l
    ld hl,#0
    add hl,sp
    ld de,#4
    add hl,de
    call load_de_from_addr_hl
    ld h,b
    ld l,c
    call write_hex16_de

    ld de,#exit_row12_hl
    call copy_zstr

    ld b,h
    ld c,l
    ld hl,#0
    add hl,sp
    ld de,#2
    add hl,de
    call load_de_from_addr_hl
    ld h,b
    ld l,c
    call write_hex16_de

    ld de,#exit_row12_sp
    call copy_zstr

    ld b,h
    ld c,l
    ld hl,#0
    add hl,sp
    ld de,#6
    add hl,de
    ex de,hl
    ld h,b
    ld l,c
    call write_hex16_de

    ld hl,#(SMAKY6_SM6_ALPHA + (13 * 64))
    ld de,#exit_row13_4550
    call copy_zstr

    ld b,h
    ld c,l
    ld hl,#SMAKY6_SM6_OUTCAR
    call load_de_from_addr_hl
    ld h,b
    ld l,c
    call write_hex16_de

    ld de,#exit_row13_4554
    call copy_zstr

    ld b,h
    ld c,l
    ld hl,#SMAKY6_SM6_LPSTAT
    dec hl
    call load_de_from_addr_hl
    ld h,b
    ld l,c
    call write_hex16_de

    ld de,#exit_row13_455c
    call copy_zstr

    ld b,h
    ld c,l
    ld hl,#0x455C
    call load_de_from_addr_hl
    ld h,b
    ld l,c
    call write_hex16_de

    ld de,#exit_row13_4562
    call copy_zstr

    ld b,h
    ld c,l
    ld hl,#0x4562
    call load_de_from_addr_hl
    ld h,b
    ld l,c
    call write_hex16_de
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

load_de_from_addr_hl:
    ld e,(hl)
    inc hl
    ld d,(hl)
    ret

exit_row12_af:
    .ascii "AF="
    .db 0

exit_row12_hl:
    .ascii " HL="
    .db 0

exit_row12_sp:
    .ascii " SP="
    .db 0

exit_row13_4550:
    .ascii "50="
    .db 0

exit_row13_4554:
    .ascii " 54="
    .db 0

exit_row13_455c:
    .ascii " 5C="
    .db 0

exit_row13_4562:
    .ascii " 62="
    .db 0