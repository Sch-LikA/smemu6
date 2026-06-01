#ifndef SMEMU6_DEBUG_VIEWPORT_H
#define SMEMU6_DEBUG_VIEWPORT_H

/* Compute the first visible row for the disassembly pane.
 * The result keeps focus near focus_row and adjusts further when selection
 * would otherwise scroll outside the viewport. */
int debug_disasm_view_start(int count,
                            int focus,
                            int selected,
                            int visible_rows,
                            int focus_row);

#endif