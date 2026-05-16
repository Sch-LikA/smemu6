# Experimental Smaky 6 SDCC Hello

This directory is the first proof-of-execution scaffold for ordinary `.SM`
programs.

Current constraints:

- fixed `load=entry=0x6000`
- ordinary `.SM` metadata with `flags=1`
- no libc startup, no initialized globals, no heap
- output goes directly to alpha RAM at `0x4000`
- the program loops after drawing; clean return to the CLI is still a follow-up

Build the example from the repository root:

```bash
tools/build_smaky6_sdcc_example.sh
```

Stage it into a bootable DX0 hostdir and run it headlessly:

```bash
tools/run_smaky6_sdcc_example.sh build/smemu6
```

The run script writes its temporary hostdir and logs under `tmp/sdcc-hello/`.