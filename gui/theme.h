#pragma once

#include "../lib/types.h"
#include "../drivers/framebuffer.h"

// NeXTSTEP 3D bevel drawing primitives
// The 4-gray system: BLACK, DKGRAY, LTGRAY, WHITE

// Draw a raised 3D element (button, title bar, dock tile)
// Outer: 1px black border
// Inner top/left: 1px white highlight
// Inner bottom/right: 1px dark gray shadow
// Fill: light gray
void nx_draw_raised(int x, int y, int w, int h);
void nx_draw_raised_color(int x, int y, int w, int h, uint32_t face);

// Draw a sunken 3D element (text field, scrollbar trough)
// Outer: 1px black border
// Inner top/left: 1px dark gray shadow
// Inner bottom/right: 1px white highlight
// Fill: white (for text fields) or dark gray (for troughs)
void nx_draw_sunken(int x, int y, int w, int h, uint32_t fill);

// Draw a flat bordered element
void nx_draw_bordered(int x, int y, int w, int h, uint32_t fill);

// Draw NeXT-style scrollbar (both arrows at bottom/right)
void nx_draw_vscrollbar(int x, int y, int h, int content_height, int scroll_pos, int visible_height);
void nx_draw_hscrollbar(int x, int y, int w, int content_width, int scroll_pos, int visible_width);

// Draw NeXT-style button
void nx_draw_button(int x, int y, int w, int h, const char* label, bool pressed, bool is_default);

// Draw text field
void nx_draw_textfield(int x, int y, int w, int h, const char* text, int cursor_pos, bool focused);

// Draw checkbox
void nx_draw_checkbox(int x, int y, const char* label, bool checked);

// Draw radio button
void nx_draw_radio(int x, int y, const char* label, bool selected);

// Draw separator line
void nx_draw_separator(int x, int y, int w);

// Draw NeXT-style resize handle (bottom-right corner)
void nx_draw_resize_handle(int x, int y);

// Draw a drop shadow behind a rectangle
void nx_draw_shadow(int x, int y, int w, int h, int offset);

// Progress bar (NeXT barber-pole style)
void nx_draw_progress(int x, int y, int w, int h, int percent, uint32_t tick);
