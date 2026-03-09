#include "apps.h"
#include "../memory/pmm.h"
#include "../memory/heap.h"

#define SYSMON_HISTORY 60

struct SysmonState {
    uint32_t cpu_history[SYSMON_HISTORY]; // Simulated CPU %
    int hist_idx;
    uint32_t last_tick;
};

static void sysmon_draw(Window* win, int cx, int cy, int cw, int ch) {
    (void)ch;
    SysmonState* s = (SysmonState*)win->userdata;

    fb_fillrect(cx, cy, cw, ch, NX_LTGRAY);

    // Update history
    uint32_t now = timer_get_ticks();
    if (!s || now - s->last_tick >= 50) {
        if (s) {
            s->cpu_history[s->hist_idx] = rand() % 40 + 10; // Simulated CPU load
            s->hist_idx = (s->hist_idx + 1) % SYSMON_HISTORY;
            s->last_tick = now;
        }
    }

    // Title
    font_draw_string(cx + 8, cy + 4, "System Monitor", NX_BLACK, NX_LTGRAY, FONT_MEDIUM);
    nx_draw_separator(cx + 4, cy + 20, cw - 8);

    // Memory info
    uint32_t free_pages = pmm_free_page_count();
    uint32_t total_pages = pmm_total_page_count();
    uint32_t used_pages = total_pages - free_pages;

    char buf[64];
    ksprintf(buf, "Memory: %dKB / %dKB", used_pages * 4, total_pages * 4);
    font_draw_string(cx + 8, cy + 28, buf, NX_BLACK, NX_LTGRAY, FONT_SMALL);

    // Memory bar
    int bar_x = cx + 8, bar_y = cy + 40, bar_w = cw - 16, bar_h = 14;
    nx_draw_sunken(bar_x, bar_y, bar_w, bar_h, NX_WHITE);
    int fill_w = (int)((uint32_t)(bar_w - 2) * used_pages / total_pages);
    uint32_t bar_color = fill_w > (bar_w * 80 / 100) ? NX_RED : NX_SELECT;
    fb_fillrect(bar_x + 1, bar_y + 1, fill_w, bar_h - 2, bar_color);

    ksprintf(buf, "%d%%", (int)(used_pages * 100 / total_pages));
    font_draw_string(bar_x + bar_w / 2 - 10, bar_y + 3, buf, NX_WHITE, bar_color, FONT_SMALL);

    // Heap info
    ksprintf(buf, "Heap: %d used / %d free", heap_used(), heap_free());
    font_draw_string(cx + 8, cy + 60, buf, NX_BLACK, NX_LTGRAY, FONT_SMALL);

    // CPU graph
    font_draw_string(cx + 8, cy + 78, "CPU Activity:", NX_BLACK, NX_LTGRAY, FONT_SMALL);

    int graph_x = cx + 8, graph_y = cy + 92;
    int graph_w = cw - 16, graph_h = ch - 140;
    nx_draw_sunken(graph_x, graph_y, graph_w, graph_h, NX_WHITE);
    fb_fillrect(graph_x + 1, graph_y + 1, graph_w - 2, graph_h - 2, NX_BLACK);

    // Draw grid lines
    for (int g = 1; g < 4; g++) {
        int gy = graph_y + 1 + g * (graph_h - 2) / 4;
        for (int gx = graph_x + 1; gx < graph_x + graph_w - 1; gx += 4)
            fb_putpixel(gx, gy, NX_DKGRAY);
    }

    // Draw CPU history
    if (s) {
        int bar_width = (graph_w - 2) / SYSMON_HISTORY;
        if (bar_width < 1) bar_width = 1;
        for (int i = 0; i < SYSMON_HISTORY; i++) {
            int idx = (s->hist_idx + i) % SYSMON_HISTORY;
            int val = s->cpu_history[idx];
            int h2 = val * (graph_h - 2) / 100;
            int bx = graph_x + 1 + i * bar_width;
            uint32_t color = val > 80 ? NX_RED : (val > 50 ? RGB(255, 200, 0) : NX_GREEN);
            fb_fillrect(bx, graph_y + graph_h - 1 - h2, bar_width, h2, color);
        }
    }

    // Window count + Uptime (below graph)
    int info_y = graph_y + graph_h + 6;
    ksprintf(buf, "Windows: %d", wm_window_count());
    font_draw_string(cx + 8, info_y, buf, NX_BLACK, NX_LTGRAY, FONT_SMALL);

    uint32_t secs = timer_get_ticks() / 100;
    ksprintf(buf, "Uptime: %d:%02d:%02d", secs / 3600, (secs / 60) % 60, secs % 60);
    font_draw_string(cx + 8, info_y + 14, buf, NX_BLACK, NX_LTGRAY, FONT_SMALL);
}

static void sysmon_close(Window* win) {
    if (win->userdata) kfree(win->userdata);
}

extern "C" void app_launch_sysmon() {
    Window* w = wm_create_window("System Monitor", 150, 80, 300, 260,
                                  WIN_CLOSEABLE | WIN_MINIATURIZE | WIN_RESIZABLE);
    if (!w) return;

    SysmonState* s = (SysmonState*)kmalloc(sizeof(SysmonState));
    memset(s, 0, sizeof(SysmonState));
    s->last_tick = timer_get_ticks();

    w->userdata = s;
    w->on_draw = sysmon_draw;
    w->on_close = sysmon_close;
}
