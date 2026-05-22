# ERRORS.md — Failed Approaches & Workarounds

When an approach takes more than 2 attempts to work, log it here.
Check this file before suggesting approaches to similar tasks.

---

## Format

**Task:** [What was being attempted]

**Attempts:**
1. **Attempt 1:** [What was tried, how it failed]
2. **Attempt 2:** [What was tried, how it failed]

**Solution:** [What finally worked]

**Note for next time:** [Key lesson or alternative to remember]

---

## Entries

**Task:** Make `PROGRA+z` work in SMILE without regressing ordinary typing

**Attempts:**
1. **Attempt 1:** enabled `S471_LAYER_FNCT` for held `PROGRA`; this fixed one real gap, but SMILE still received plain `z` because text-capable keys were bypassing the matrix path through `SDL_TEXTINPUT`.
2. **Attempt 2:** suppressed `SDL_TEXTINPUT` while the FNCT layer was active; this removed the plain `z`, but SMILE then saw `û` because the `?GETFO` accessor still preferred the staged ordinary byte in `0x457E`.
3. **Attempt 3:** made `?GETFO` return held function bits directly; that removed the `û`, but the user still got `ù` because the FNCT shortcut path was using raw scancodes and their host keyboard layout was QWERTZ.
4. **Attempt 4:** corrected the QWERTZ logical-letter handling inside the FNCT-layer path; the user then returned to `û`, which showed the deeper problem was not host layout any more but the assumption that `PROGRA` should remap the ordinary key through `S471_LAYER_FNCT` at all.
5. **Attempt 5:** kept `PROGRA` separate from the matrix layer; plain `z` returned, but `PROGRA+END` still echoed the ordinary END glyph, which exposed a different bug: the first ordinary CLA read was still being prefixed as `0x80 | key_code` and could overwrite the function workspace.
6. **Attempt 6:** removed that first ordinary-key CLA prefix; build and boot still passed, but user validation showed the regression immediately because no normal key worked at the CLI prompt any more.
7. **Attempt 7:** treated `?GETFO` as a single `pc == 0x0519` read-site; existing traces already showed the actual `0x457E` read at `pc == 0x0516`, so the hook could silently miss live function-bit delivery even though the rest of the function-key path looked correct.

**Current working floor:** keep the normal keyboard path intact: suppress the SDL text bypass, make the `0x0519` / `?GETFO` helper return held function bits directly, do not let `PROGRA` rewrite the ordinary matrix key through `S471_LAYER_FNCT`, and keep the audited bit-7-first ordinary CLA delivery for normal post-boot keys.

**Note for next time:** when a Smaky application combines a function key with an ordinary key, check the whole chain in order: host event path, matrix/text split, `?GETFO` accessor policy, host keyboard layout, whether the function key should modify the ordinary matrix byte at all, and whether a proposed CLA-path change also preserves ordinary CLI typing. A fix that passes build and boot but kills all prompt typing is almost certainly touching the wrong abstraction layer.

