static void put_text(unsigned short offset, const char *text)
{
    volatile unsigned char *cursor = (volatile unsigned char *)(0x4000u + offset);

    while (*text != '\0') {
        *cursor++ = (unsigned char)*text++;
    }
}

void main(void)
{
    put_text(0u, "SDCC HELLO");
    put_text(64u, "LOAD ENTRY 6000");
    put_text(128u, "ALPHA RAM ONLY");

    for (;;) {
    }
}