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

**Task:** Make SIGMA move the inverse selection with `r/d/f/c`

**Attempts:**
1. **Attempt 1:** treated the bug as a renderer problem; adding inverse-mask `-scrdump` rows proved the highlight attribute was being redrawn correctly, but the mask never moved after `f`.
2. **Attempt 2:** preserved held ordinary keys across SAMOS circular-buffer commits; this improved the traces and kept the key logically held longer, but SIGMA still only blinked the current block.
3. **Attempt 3:** removed the first-read bit-7 prefix for direct/text ordinary keys; build and visible tests still produced no movement, so the probe was reverted.
4. **Attempt 4:** forced real `r/d/f/c` SDL keys through the matrix path and suppressed their text events; focused manual tracing showed SIGMA still consumed the same `E6` then `66` stream and still did not move the block, so the probe was reverted.

**Current working floor:** keep the validated inverse-video renderer, keep the inverse-mask `-scrdump` diagnostics, keep held ordinary keys surviving circular-buffer commits, and keep the normal direct-key prefix and text-input behavior.

**Note for next time:** before changing host keyboard routing again, first inspect what SIGMA itself reads or compares. Real SDL `f` and injected plain `f` already reach the guest in the same way, so more host-side permutations are likely noise until Sigma-side expectations are understood.

**Task:** Make `PROGRA+z` work in SMILE without regressing ordinary typing

**Attempts:**
1. **Attempt 1:** enabled `S471_LAYER_FNCT` for held `PROGRA`; this fixed one real gap, but SMILE still received plain `z` because text-capable keys were bypassing the matrix path through `SDL_TEXTINPUT`.
2. **Attempt 2:** suppressed `SDL_TEXTINPUT` while the FNCT layer was active; this removed the plain `z`, but SMILE then saw `û` because the `?GETFO` accessor still preferred the staged ordinary byte in `0x457E`.
3. **Attempt 3:** made `?GETFO` return held function bits directly; that removed the `û`, but the user still got `ù` because the FNCT shortcut path was using raw scancodes and their host keyboard layout was QWERTZ.
4. **Attempt 4:** corrected the QWERTZ logical-letter handling inside the FNCT-layer path; the user then returned to `û`, which showed the deeper problem was not host layout any more but the assumption that `PROGRA` should remap the ordinary key through `S471_LAYER_FNCT` at all.
5. **Attempt 5:** kept `PROGRA` separate from the matrix layer; plain `z` returned, but `PROGRA+END` still echoed the ordinary END glyph, which exposed a different bug: the first ordinary CLA read was still being prefixed as `0x80 | key_code` and could overwrite the function workspace.
6. **Attempt 6:** removed that first ordinary-key CLA prefix; build and boot still passed, but user validation showed the regression immediately because no normal key worked at the CLI prompt any more.
7. **Attempt 7:** treated `?GETFO` as a single `pc == 0x0519` read-site; existing traces already showed the actual `0x457E` read at `pc == 0x0516`, so the hook could silently miss live function-bit delivery even though the rest of the function-key path looked correct.
8. **Attempt 8:** suppressed the ordinary-key bit-7 prefix while a function key was already held; build and boot still passed, but user validation showed `PROGRA+z` and `PROGRA+END` then had no visible effect at all.
9. **Attempt 9:** added a consume-on-read side effect to the staged-byte-first `?GETFO` policy; build still passed, but the standard headless boot check immediately showed stray prompt characters, so that side effect had to be removed.
10. **Attempt 10:** kept the staged-byte-first `?GETFO` policy without consume-on-read; build and boot passed, but user validation showed the old repeat bug reappeared and function keys again echoed regular characters.

**Current working floor:** keep the normal keyboard path intact: suppress the SDL text bypass, make the `0x0516..0x0519` `?GETFO` helper return held function bits directly, do not let `PROGRA` rewrite the ordinary matrix key through `S471_LAYER_FNCT`, keep the audited bit-7-first ordinary CLA delivery for normal post-boot keys, and keep the no-key/function CLA path on the hardware-style `0x80 | fonct_bits` form.

**Current simultaneous-key hypothesis:** the remaining mismatch is now more likely in the simultaneous ordinary/function scan ordering itself than in the high-level `?GETFO` policy. The narrower `?GETFO` change is below baseline because it revives known bugs.

**Note for next time:** when a Smaky application combines a function key with an ordinary key, check the whole chain in order: host event path, matrix/text split, `?GETFO` accessor policy, host keyboard layout, whether the function key should modify the ordinary matrix byte at all, and whether a proposed CLA-path change also preserves ordinary CLI typing. A fix that passes build and boot but kills all prompt typing is almost certainly touching the wrong abstraction layer.

