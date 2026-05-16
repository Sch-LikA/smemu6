# Experimental Smaky 6 SDCC SM6 Symbol Probe

This directory is a second standalone SDCC probe that consumes recovered
`SM6.ST` symbols beyond the alpha base address.

Current focus:

- reuses the same ordinary `.SM` first-target contract as `hello_alpha`
- consumes `SMAKY6_SM6_ALPHA`, `SMAKY6_SM6_OUTCAR`, and `SMAKY6_SM6_MAXMEM`
- displays both recovered symbol addresses and live workspace reads
- returns to the CLI through the verified `0x56AE` exit path

Build from the repository root:

```bash
sdcc/build_smaky6_sdcc_example.sh sm6peek
```

Run headlessly through the standard hostdir flow:

```bash
sdcc/run_smaky6_sdcc_example.sh build/smemu6 sm6peek
```
