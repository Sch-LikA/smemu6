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

**Solution:** fix the whole chain: suppress the SDL text bypass, make the `0x0519` / `?GETFO` helper return held function bits directly, and for alphabetic FNCT shortcuts prefer SDL logical key symbols over raw scancodes.

**Note for next time:** when a Smaky application combines a function key with an ordinary key, check the whole chain in order: host event path, matrix/text split, `?GETFO` accessor policy, and finally host keyboard layout. A visible progression from `z` to `û` to `ù` means each stage was fixed in turn, with the last one exposing a QWERTZ-vs-QWERTY shortcut mapping bug.

