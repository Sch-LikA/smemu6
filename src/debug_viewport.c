#include "debug_viewport.h"

/* Clamp one viewport index into the valid instruction-row range.
 * Empty views collapse to row 0 so the caller can keep simple arithmetic. */
static int debug_clamp_index(int value, int count)
{
    if (count <= 0) {
        return 0;
    }
    if (value < 0) {
        return 0;
    }
    if (value >= count) {
        return count - 1;
    }
    return value;
}

/* Choose the first visible disassembly row so the focused instruction stays
 * near focus_row while still forcing the selected row on-screen. */
int debug_disasm_view_start(int count,
                            int focus,
                            int selected,
                            int visible_rows,
                            int focus_row)
{
    int max_start;
    int start;

    if (count <= 0 || visible_rows <= 0) {
        return 0;
    }

    focus = debug_clamp_index(focus, count);
    selected = debug_clamp_index(selected, count);
    if (focus_row < 0) {
        focus_row = 0;
    }
    if (focus_row >= visible_rows) {
        focus_row = visible_rows - 1;
    }

    max_start = count > visible_rows ? count - visible_rows : 0;
    start = focus > focus_row ? focus - focus_row : 0;
    if (start > max_start) {
        start = max_start;
    }
    if (selected < start) {
        start = selected;
    } else if (selected >= start + visible_rows) {
        start = selected - (visible_rows - 1);
        if (start > max_start) {
            start = max_start;
        }
    }

    return start;
}