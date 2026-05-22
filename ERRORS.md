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

**Solution:** make the `0x0519` / `?GETFO` helper return held function bits directly and stop using the staged ordinary byte as the primary result.

**Note for next time:** when a Smaky application combines a function key with an ordinary key, check the whole chain in order: host event path, matrix/text split, then the `?GETFO` accessor policy. A visible change from `z` to `û` means the matrix layer changed, but the function-read path may still be wrong.

