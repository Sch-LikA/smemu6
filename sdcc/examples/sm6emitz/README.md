# Experimental Smaky 6 SDCC RST 20 / 0x06 Probe

This directory is a standalone SDCC probe for the documented
`RST 20 / 0x06` zero-terminated string helper.

Current focus:

- reuses the same ordinary `.SM` first-target contract as `hello_alpha`
- calls a documented `RST 20` helper through an example-local assembly wrapper
- treats the C string pointer as the `HL` input expected by that helper
- keeps direct alpha-RAM text on screen so the expected emitter output location is clear
- returns to the CLI through the verified `0x56AE` exit path

Build from the repository root:

```bash
sdcc/build_smaky6_sdcc_example.sh sm6emitz
```

Run headlessly through the standard hostdir flow:

```bash
sdcc/run_smaky6_sdcc_example.sh build/smemu6 sm6emitz
```
