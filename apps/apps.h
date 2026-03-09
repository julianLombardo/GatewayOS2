#pragma once

#include "../gui/window.h"
#include "../gui/theme.h"
#include "../gui/font.h"
#include "../drivers/framebuffer.h"
#include "../drivers/keyboard.h"
#include "../lib/types.h"
#include "../lib/string.h"
#include "../lib/printf.h"
#include "../lib/math.h"
#include "../kernel/timer.h"

// App launch functions (extern "C" for menu linkage)
extern "C" {
    void app_launch_terminal();
    void app_launch_calculator();
    void app_launch_clock();
    void app_launch_edit();
    void app_launch_sysmon();
    void app_launch_hexview();
    void app_launch_about();
    void app_launch_preferences();
    void app_launch_help();
    void app_launch_grab();
    void app_launch_netinfo();
    void app_launch_mail();
    void app_launch_workspace();
    void app_launch_chess();
    void app_launch_snake();
    void app_launch_pong();
    void app_launch_mines();
    void app_launch_tetris();
    void app_launch_puzzle();
    void app_launch_billiards();
    void app_launch_paint();
    void app_launch_draw();
    void app_launch_decrypt();
    void app_launch_radar();
    void app_launch_neural();
    void app_launch_uplink();
    void app_launch_probe();
    void app_launch_starmap();
    void app_launch_comm();
    void app_launch_matrix();
    // Intelligence Tools
    void app_launch_cipher();
    void app_launch_fortress();
    void app_launch_sentinel();
    void app_launch_netscan();
    void app_launch_hashlab();
    // Extras
    void app_launch_calendar();
    void app_launch_notes();
    void app_launch_contacts();
    void app_launch_colorpick();
    void app_launch_fontview();
    void app_launch_taskmgr();
    void app_launch_diskuse();
    void app_launch_logview();
    void app_launch_screensaver();
    void app_launch_weather();
    void app_launch_music();
    void app_launch_fileview();
    void app_launch_gmail();
    void app_launch_javaide();
    void app_launch_perun();
}
