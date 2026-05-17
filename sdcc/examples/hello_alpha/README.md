# Experimental Smaky 6 SDCC Hello

This directory is the first proof-of-execution scaffold for ordinary `.SM`
programs.

Current constraints:

- default `load=entry=0x6000` via the shared build script, with optional
	example-local overrides through `layout.conf`
- ordinary `.SM` metadata with `flags=1`
- no libc startup, no initialized globals, no heap
- output goes directly to alpha RAM at `0x4000`
- the current startup returns to the CLI through the verified `0x56AE` path

Build the example from the repository root:

```bash
sdcc/build_smaky6_sdcc_example.sh
```

Stage it into a bootable DX0 hostdir and run it headlessly:

```bash
sdcc/run_smaky6_sdcc_example.sh build/smemu6
```

The run script writes its temporary hostdir and logs under `tmp/sdcc-hello_alpha/`.
