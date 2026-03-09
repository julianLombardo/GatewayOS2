#include "apps.h"
#include "../memory/heap.h"
#include "../net/net.h"
#include "../drivers/e1000.h"
#include "../drivers/serial.h"
// Mail sending uses the Python relay (mail_relay.py) via plain TCP
// No TLS needed - the relay handles Gmail's SSL on the host side

// ============================================================
// GATEWAY MAIL - Full email client with SMTP send capability
// Gmail-inspired UI with NeXTSTEP styling
// ============================================================

#define GMAIL_MAX_MSGS    16
#define GMAIL_BODY_LEN    512
#define GMAIL_ADDR_LEN    48
#define GMAIL_SUBJ_LEN    48
#define GMAIL_MAX_FOLDERS 5

// Email message
struct GmailMsg {
    char from[GMAIL_ADDR_LEN];
    char to[GMAIL_ADDR_LEN];
    char subject[GMAIL_SUBJ_LEN];
    char body[GMAIL_BODY_LEN];
    char date[16];      // "Mar 07"
    bool read;
    bool starred;
    bool deleted;
    int folder;         // 0=inbox, 1=sent, 2=drafts, 3=starred, 4=trash
};

// Views
enum GmailView {
    VIEW_LOGIN,
    VIEW_INBOX,
    VIEW_READ,
    VIEW_COMPOSE,
    VIEW_SENDING,
    VIEW_SETTINGS
};

// SMTP connection state
enum SmtpState {
    SMTP_IDLE,
    SMTP_CONNECTING,
    SMTP_HELO,
    SMTP_MAIL_FROM,
    SMTP_RCPT_TO,
    SMTP_DATA,
    SMTP_BODY,
    SMTP_DONE,
    SMTP_ERROR
};

struct GmailState {
    // Login
    char email[GMAIL_ADDR_LEN];
    char password[32];
    int email_len;
    int pass_len;
    int login_field;    // 0=email, 1=password
    bool logged_in;
    char display_name[24];

    // Messages
    GmailMsg msgs[GMAIL_MAX_MSGS];
    int msg_count;
    int selected;
    int scroll;
    int current_folder;  // 0=inbox, 1=sent, 2=drafts, 3=starred, 4=trash

    // View
    GmailView view;

    // Compose
    char compose_to[GMAIL_ADDR_LEN];
    char compose_subj[GMAIL_SUBJ_LEN];
    char compose_body[GMAIL_BODY_LEN];
    int to_len, subj_len, body_len;
    int compose_field;  // 0=to, 1=subj, 2=body
    bool is_reply;

    // SMTP
    SmtpState smtp_state;
    TcpSocket* smtp_sock;
    // (relay uses plain TCP, no TLS needed)
    char status_msg[48];
    int send_timer;
    int smtp_step; // sub-step within TLS SMTP
};

static const char* folder_names[] = {"Inbox", "Sent", "Drafts", "Starred", "Trash"};
static const int folder_icons[] = {0, 1, 2, 3, 4}; // icon type indices

// Saved credentials (persists across app closes within same boot)
static char saved_email[GMAIL_ADDR_LEN] = {0};
static char saved_password[32] = {0};
static int saved_email_len = 0;
static int saved_pass_len = 0;
static bool has_saved_creds = false;

// Pre-populate with sample emails
static void gmail_init_msgs(GmailState* g) {
    g->msg_count = 5;

    strcpy(g->msgs[0].from, "Gateway OS2 Team");
    strcpy(g->msgs[0].to, g->email);
    strcpy(g->msgs[0].subject, "Welcome to Gateway Mail!");
    strcpy(g->msgs[0].body,
        "Hello!\n\n"
        "Welcome to Gateway Mail, your email\n"
        "client on Gateway OS2.\n\n"
        "Features:\n"
        "- Send real emails via SMTP\n"
        "- Compose, reply, forward\n"
        "- Folder management\n"
        "- Star important messages\n\n"
        "Enjoy your experience!\n"
        "- The Gateway Team");
    strcpy(g->msgs[0].date, "Mar 07");
    g->msgs[0].read = false;
    g->msgs[0].starred = true;
    g->msgs[0].deleted = false;
    g->msgs[0].folder = 0;

    strcpy(g->msgs[1].from, "admin@gateway.os");
    strcpy(g->msgs[1].to, g->email);
    strcpy(g->msgs[1].subject, "System Configuration");
    strcpy(g->msgs[1].body,
        "Your system has been configured\n"
        "with the following settings:\n\n"
        "- Display: 1024x768 32-bit\n"
        "- Network: E1000 NIC (DHCP)\n"
        "- Mail: SMTP on port 25\n\n"
        "All 47+ applications are ready.");
    strcpy(g->msgs[1].date, "Mar 07");
    g->msgs[1].read = false;
    g->msgs[1].starred = false;
    g->msgs[1].deleted = false;
    g->msgs[1].folder = 0;

    strcpy(g->msgs[2].from, "security@gateway.os");
    strcpy(g->msgs[2].to, g->email);
    strcpy(g->msgs[2].subject, "Security Alert");
    strcpy(g->msgs[2].body,
        "New login detected:\n\n"
        "Device: Gateway OS2\n"
        "Location: Local Machine\n"
        "Time: Boot time\n\n"
        "If this was you, no action\n"
        "is needed.");
    strcpy(g->msgs[2].date, "Mar 06");
    g->msgs[2].read = true;
    g->msgs[2].starred = false;
    g->msgs[2].deleted = false;
    g->msgs[2].folder = 0;

    strcpy(g->msgs[3].from, "newsletter@gateway.os");
    strcpy(g->msgs[3].to, g->email);
    strcpy(g->msgs[3].subject, "Gateway OS2 Newsletter #1");
    strcpy(g->msgs[3].body,
        "Gateway OS2 Newsletter\n"
        "======================\n\n"
        "New in this release:\n"
        "- Full networking stack\n"
        "- E1000 NIC driver\n"
        "- TCP/IP + DHCP + DNS\n"
        "- SMTP email client\n"
        "- 47+ applications\n\n"
        "Stay tuned for more!");
    strcpy(g->msgs[3].date, "Mar 05");
    g->msgs[3].read = true;
    g->msgs[3].starred = false;
    g->msgs[3].deleted = false;
    g->msgs[3].folder = 0;

    strcpy(g->msgs[4].from, g->email);
    strcpy(g->msgs[4].to, "friend@example.com");
    strcpy(g->msgs[4].subject, "Hello from Gateway OS2!");
    strcpy(g->msgs[4].body,
        "Hey!\n\n"
        "I'm writing this from my own\n"
        "operating system. Gateway OS2\n"
        "has a built-in email client!\n\n"
        "Pretty cool, right?");
    strcpy(g->msgs[4].date, "Mar 07");
    g->msgs[4].read = true;
    g->msgs[4].starred = false;
    g->msgs[4].deleted = false;
    g->msgs[4].folder = 1; // Sent
}

// Count messages in a folder
static int gmail_folder_count(GmailState* g, int folder) {
    int c = 0;
    for (int i = 0; i < g->msg_count; i++) {
        if (!g->msgs[i].deleted && g->msgs[i].folder == folder) c++;
        if (folder == 3 && g->msgs[i].starred && !g->msgs[i].deleted) c++;
    }
    return c;
}

static int gmail_unread_count(GmailState* g) {
    int c = 0;
    for (int i = 0; i < g->msg_count; i++)
        if (!g->msgs[i].deleted && g->msgs[i].folder == 0 && !g->msgs[i].read) c++;
    return c;
}

// Draw a small icon for folders
static void draw_folder_icon(int x, int y, int type, uint32_t color) {
    switch (type) {
    case 0: // Inbox - envelope
        fb_rect(x, y + 2, 12, 8, color);
        fb_putpixel(x + 1, y + 3, color); fb_putpixel(x + 2, y + 4, color);
        fb_putpixel(x + 3, y + 5, color); fb_putpixel(x + 4, y + 6, color);
        fb_putpixel(x + 5, y + 7, color); fb_putpixel(x + 6, y + 7, color);
        fb_putpixel(x + 7, y + 6, color); fb_putpixel(x + 8, y + 5, color);
        fb_putpixel(x + 9, y + 4, color); fb_putpixel(x + 10, y + 3, color);
        break;
    case 1: // Sent - paper plane
        for (int i = 0; i < 10; i++) fb_putpixel(x + i, y + 5, color);
        for (int i = 0; i < 5; i++) { fb_putpixel(x + 5 + i, y + 5 - i, color); fb_putpixel(x + 5 + i, y + 5 + i, color); }
        break;
    case 2: // Drafts - pencil
        for (int i = 0; i < 10; i++) fb_putpixel(x + 2 + i, y + 8 - i, color);
        for (int i = 0; i < 10; i++) fb_putpixel(x + 3 + i, y + 8 - i, color);
        break;
    case 3: // Starred - star
        for (int i = 0; i < 5; i++) fb_putpixel(x + 5 + i, y + i, color);
        for (int i = 0; i < 5; i++) fb_putpixel(x + 5 - i, y + i, color);
        fb_hline(x + 1, y + 5, 10, color);
        for (int i = 0; i < 3; i++) fb_putpixel(x + 2 + i, y + 6 + i, color);
        for (int i = 0; i < 3; i++) fb_putpixel(x + 9 - i, y + 6 + i, color);
        break;
    case 4: // Trash - trash can
        fb_hline(x + 2, y + 1, 8, color);
        fb_hline(x + 4, y, 4, color);
        fb_rect(x + 3, y + 2, 6, 8, color);
        fb_vline(x + 5, y + 3, 6, color);
        fb_vline(x + 7, y + 3, 6, color);
        break;
    }
}

// Draw star icon
static void draw_star(int x, int y, bool filled) {
    uint32_t col = filled ? RGB(230, 180, 0) : NX_LTGRAY;
    fb_putpixel(x + 4, y, col);
    fb_hline(x + 3, y + 1, 3, col);
    fb_hline(x, y + 2, 9, col);
    fb_hline(x + 1, y + 3, 7, col);
    fb_hline(x + 2, y + 4, 5, col);
    fb_hline(x + 1, y + 5, 3, col);
    fb_hline(x + 5, y + 5, 3, col);
    fb_putpixel(x + 1, y + 6, col);
    fb_putpixel(x + 7, y + 6, col);
}

// ---- Mail Relay SMTP (plain TCP to host relay at 10.0.2.2:2525) ----
// The Python mail_relay.py on the host handles TLS/Gmail for us.
// Protocol: FROM\nTO\nSUBJECT\nUSER\nPASS\nBODY_LINES...\nEND
// Response: "OK\n" or "ERR:reason\n"

#define RELAY_IP    0x0202000A   // 10.0.2.2 in little-endian
#define RELAY_PORT  2525

static void gmail_smtp_send(GmailState* g) {
    g->smtp_state = SMTP_CONNECTING;
    g->send_timer = 0;
    g->smtp_step = 0;
    g->smtp_sock = nullptr;
    strcpy(g->status_msg, "Connecting to mail relay...");
}

static void gmail_smtp_poll(GmailState* g) {
    if (g->smtp_state == SMTP_IDLE || g->smtp_state == SMTP_DONE || g->smtp_state == SMTP_ERROR)
        return;

    g->send_timer++;

    switch (g->smtp_step) {
    case 0: {
        // Connect to relay via plain TCP
        serial_write("[GMAIL] Connecting to relay at 10.0.2.2:2525\n");
        strcpy(g->status_msg, "Connecting to mail relay...");

        NetConfig* nc = net_get_config();
        if (!nc || !nc->configured) {
            strcpy(g->status_msg, "Network not configured");
            g->smtp_state = SMTP_ERROR;
            return;
        }

        g->smtp_sock = net_tcp_connect(RELAY_IP, RELAY_PORT);
        if (!g->smtp_sock) {
            strcpy(g->status_msg, "Relay connect failed");
            serial_write("[GMAIL] TCP connect to relay failed\n");
            g->smtp_state = SMTP_ERROR;
            return;
        }

        // Wait for connection to establish
        uint32_t start = timer_get_ticks();
        while (g->smtp_sock->state != 2 /* ESTABLISHED */) {
            net_poll();
            if ((timer_get_ticks() - start) > 500) { // 5 sec timeout
                strcpy(g->status_msg, "Relay connect timeout");
                serial_write("[GMAIL] TCP connect timeout\n");
                net_tcp_close(g->smtp_sock);
                g->smtp_sock = nullptr;
                g->smtp_state = SMTP_ERROR;
                return;
            }
            for (volatile int i = 0; i < 1000; i++);
        }

        serial_write("[GMAIL] Connected to relay!\n");
        g->smtp_step = 1;
        strcpy(g->status_msg, "Sending to relay...");
        break;
    }

    case 1: {
        // Build and send the relay protocol message
        // Format: FROM\nTO\nSUBJECT\nUSER\nPASS\nBODY\nEND\n
        char msg[1024];
        int len = 0;

        // FROM (sender email)
        len += ksprintf(msg + len, "%s\n", g->email);
        // TO
        len += ksprintf(msg + len, "%s\n", g->compose_to);
        // SUBJECT
        len += ksprintf(msg + len, "%s\n", g->compose_subj);
        // Gmail USER (for SMTP auth)
        len += ksprintf(msg + len, "%s\n", g->email);
        // Gmail PASS (app password)
        len += ksprintf(msg + len, "%s\n", g->password);
        // BODY
        for (int i = 0; i < g->body_len && len < 900; i++) {
            msg[len++] = g->compose_body[i];
        }
        // END marker
        len += ksprintf(msg + len, "\nEND\n");

        net_tcp_send(g->smtp_sock, msg, len);

        char dbg[80];
        ksprintf(dbg, "[GMAIL] Sent %d bytes to relay\n", len);
        serial_write(dbg);

        g->smtp_step = 2;
        strcpy(g->status_msg, "Waiting for relay response...");
        break;
    }

    case 2: {
        // Wait for relay response: "OK\n" or "ERR:...\n"
        char resp[128] = {0};
        int total = 0;
        uint32_t start = timer_get_ticks();

        while (total < 120 && (timer_get_ticks() - start) < 3000) { // 30 sec timeout
            net_poll();
            uint16_t r = net_tcp_recv(g->smtp_sock, resp + total, 120 - total);
            if (r > 0) {
                total += r;
                // Check if we got a complete line
                for (int i = 0; i < total; i++) {
                    if (resp[i] == '\n') {
                        resp[i] = 0;
                        goto got_response;
                    }
                }
            }
            for (volatile int i = 0; i < 2000; i++);
        }

        // Timeout
        strcpy(g->status_msg, "Relay response timeout");
        serial_write("[GMAIL] Relay response timeout\n");
        net_tcp_close(g->smtp_sock);
        g->smtp_sock = nullptr;
        g->smtp_state = SMTP_ERROR;
        return;

    got_response:
        resp[total] = 0;
        char dbg[80];
        ksprintf(dbg, "[GMAIL] Relay response: %s\n", resp);
        serial_write(dbg);

        net_tcp_close(g->smtp_sock);
        g->smtp_sock = nullptr;

        if (resp[0] == 'O' && resp[1] == 'K') {
            serial_write("[GMAIL] Email sent successfully via relay!\n");
            g->smtp_state = SMTP_DONE;
            strcpy(g->status_msg, "Message sent via Gmail!");
        } else {
            // ERR:reason
            if (total > 4 && resp[0] == 'E' && resp[1] == 'R' && resp[2] == 'R' && resp[3] == ':') {
                ksprintf(g->status_msg, "Relay: %.40s", resp + 4);
            } else {
                strcpy(g->status_msg, "Relay error");
            }
            g->smtp_state = SMTP_ERROR;
        }
        break;
    }

    default:
        g->smtp_state = SMTP_ERROR;
        strcpy(g->status_msg, "Internal error");
        break;
    }
}

// ---- Drawing ----

static void gmail_draw_login(GmailState* g, int cx, int cy, int cw, int ch) {
    // Google-style login screen
    fb_fillrect(cx, cy, cw, ch, NX_WHITE);

    // Logo area
    int logo_x = cx + cw / 2;
    int ly = cy + 20;

    // Draw "G" logo - colored circle segments
    int gr = 20;
    int gcx = logo_x, gcy = ly + gr;
    for (int a = 0; a < 360; a++) {
        int px = gcx + gr * cos256(a) / 256;
        int py = gcy - gr * sin256(a) / 256;
        uint32_t col;
        if (a < 90) col = RGB(66, 133, 244);       // Blue
        else if (a < 180) col = RGB(234, 67, 53);   // Red
        else if (a < 270) col = RGB(251, 188, 4);   // Yellow
        else col = RGB(52, 168, 83);                 // Green
        fb_putpixel(px, py, col);
        fb_putpixel(px + 1, py, col);
        fb_putpixel(px, py + 1, col);
    }
    // Inner circle (white to make it a ring)
    for (int dy = -(gr-5); dy <= (gr-5); dy++) {
        for (int dx = -(gr-5); dx <= (gr-5); dx++) {
            if (dx*dx + dy*dy <= (gr-5)*(gr-5))
                fb_putpixel(gcx + dx, gcy + dy, NX_WHITE);
        }
    }
    // The "G" crossbar
    fb_fillrect(gcx, gcy - 3, gr + 2, 6, RGB(66, 133, 244));
    // Clear left half of crossbar
    fb_fillrect(gcx - gr + 5, gcy - 3, gr - 5, 6, NX_WHITE);

    ly += 50;
    const char* title = "Gateway Mail";
    int tw = strlen(title) * font_char_width(FONT_LARGE);
    font_draw_string(cx + (cw - tw) / 2, ly, title, NX_BLACK, NX_WHITE, FONT_LARGE);
    ly += 24;

    const char* sub = "Sign in to your account";
    tw = strlen(sub) * font_char_width(FONT_SMALL);
    font_draw_string(cx + (cw - tw) / 2, ly, sub, NX_DKGRAY, NX_WHITE, FONT_SMALL);
    ly += 25;

    // Email field
    int fw = cw - 80;
    int fx = cx + 40;

    font_draw_string(fx, ly, "Email", NX_DKGRAY, NX_WHITE, FONT_SMALL);
    ly += 14;
    uint32_t border0 = (g->login_field == 0) ? RGB(66, 133, 244) : NX_DKGRAY;
    fb_rect(fx, ly, fw, 22, border0);
    if (g->login_field == 0) fb_rect(fx - 1, ly - 1, fw + 2, 24, border0);
    fb_fillrect(fx + 1, ly + 1, fw - 2, 20, NX_WHITE);
    font_draw_string(fx + 6, ly + 5, g->email, NX_BLACK, NX_WHITE, FONT_SMALL);
    if (g->login_field == 0 && (timer_get_ticks() / 50) % 2 == 0) {
        int cx2 = fx + 6 + g->email_len * font_char_width(FONT_SMALL);
        fb_fillrect(cx2, ly + 3, 1, 14, NX_BLACK);
    }
    ly += 30;

    // Password field
    font_draw_string(fx, ly, "Password", NX_DKGRAY, NX_WHITE, FONT_SMALL);
    ly += 14;
    uint32_t border1 = (g->login_field == 1) ? RGB(66, 133, 244) : NX_DKGRAY;
    fb_rect(fx, ly, fw, 22, border1);
    if (g->login_field == 1) fb_rect(fx - 1, ly - 1, fw + 2, 24, border1);
    fb_fillrect(fx + 1, ly + 1, fw - 2, 20, NX_WHITE);
    // Show dots for password
    for (int i = 0; i < g->pass_len; i++) {
        int dx = fx + 6 + i * 10;
        for (int dy2 = -2; dy2 <= 2; dy2++)
            for (int dx2 = -2; dx2 <= 2; dx2++)
                if (dx2*dx2 + dy2*dy2 <= 4)
                    fb_putpixel(dx + 4 + dx2, ly + 11 + dy2, NX_BLACK);
    }
    if (g->login_field == 1 && (timer_get_ticks() / 50) % 2 == 0) {
        int cx2 = fx + 6 + g->pass_len * 10;
        fb_fillrect(cx2, ly + 3, 1, 14, NX_BLACK);
    }
    ly += 34;

    // Sign In button
    int bw = 100, bh = 28;
    int bx = cx + (cw - bw) / 2;
    fb_fillrect(bx, ly, bw, bh, RGB(66, 133, 244));
    fb_rect(bx, ly, bw, bh, RGB(50, 100, 200));
    const char* btn = "Sign In";
    tw = strlen(btn) * font_char_width(FONT_MEDIUM);
    font_draw_string(bx + (bw - tw) / 2, ly + 7, btn, NX_WHITE, RGB(66, 133, 244), FONT_MEDIUM);

    ly += 36;
    const char* n1 = "Use your Gmail address and";
    tw = strlen(n1) * font_char_width(FONT_SMALL);
    font_draw_string(cx + (cw - tw) / 2, ly, n1, NX_DKGRAY, NX_WHITE, FONT_SMALL);
    ly += 12;
    const char* n2 = "a Google App Password to send";
    tw = strlen(n2) * font_char_width(FONT_SMALL);
    font_draw_string(cx + (cw - tw) / 2, ly, n2, NX_DKGRAY, NX_WHITE, FONT_SMALL);
    ly += 12;
    const char* n3 = "real emails. (16-char code from";
    tw = strlen(n3) * font_char_width(FONT_SMALL);
    font_draw_string(cx + (cw - tw) / 2, ly, n3, NX_DKGRAY, NX_WHITE, FONT_SMALL);
    ly += 12;
    const char* n4 = "Google Account > Security)";
    tw = strlen(n4) * font_char_width(FONT_SMALL);
    font_draw_string(cx + (cw - tw) / 2, ly, n4, NX_DKGRAY, NX_WHITE, FONT_SMALL);
}

static void gmail_draw_sidebar(GmailState* g, int sx, int sy, int sw, int sh) {
    fb_fillrect(sx, sy, sw, sh, RGB(242, 242, 242));
    fb_vline(sx + sw - 1, sy, sh, NX_LTGRAY);

    // Compose button
    int by = sy + 8;
    int bw = sw - 16, bh = 28;
    int bx = sx + 8;
    fb_fillrect(bx, by, bw, bh, RGB(66, 133, 244));
    fb_rect(bx, by, bw, bh, RGB(50, 100, 200));
    // Rounded corners (approximate)
    fb_putpixel(bx, by, RGB(242, 242, 242));
    fb_putpixel(bx + bw - 1, by, RGB(242, 242, 242));
    fb_putpixel(bx, by + bh - 1, RGB(242, 242, 242));
    fb_putpixel(bx + bw - 1, by + bh - 1, RGB(242, 242, 242));

    const char* comp = "+ Compose";
    int tw = strlen(comp) * font_char_width(FONT_SMALL);
    font_draw_string(bx + (bw - tw) / 2, by + 9, comp, NX_WHITE, RGB(66, 133, 244), FONT_SMALL);

    by += 40;

    // Folder list
    for (int i = 0; i < GMAIL_MAX_FOLDERS; i++) {
        bool sel = (g->current_folder == i);
        uint32_t bg = sel ? RGB(210, 227, 252) : RGB(242, 242, 242);
        uint32_t fg = sel ? RGB(24, 90, 188) : NX_BLACK;

        fb_fillrect(sx, by, sw - 1, 24, bg);
        if (sel) fb_fillrect(sx, by, 3, 24, RGB(66, 133, 244));

        draw_folder_icon(sx + 10, by + 6, folder_icons[i], fg);
        font_draw_string(sx + 28, by + 7, folder_names[i], fg, bg, FONT_SMALL);

        // Unread count for inbox
        if (i == 0) {
            int unread = gmail_unread_count(g);
            if (unread > 0) {
                char ubuf[8];
                ksprintf(ubuf, "%d", unread);
                int uw = strlen(ubuf) * font_char_width(FONT_SMALL);
                font_draw_string(sx + sw - uw - 12, by + 7, ubuf, RGB(66, 133, 244), bg, FONT_SMALL);
            }
        }

        by += 24;
    }

    // User info at bottom
    int uby = sy + sh - 30;
    nx_draw_separator(sx + 4, uby, sw - 8);
    uby += 6;
    // Truncate email if too long
    char disp[20];
    int maxc = (sw - 16) / font_char_width(FONT_SMALL);
    if (maxc > 19) maxc = 19;
    strncpy(disp, g->email, maxc);
    disp[maxc] = 0;
    font_draw_string(sx + 8, uby, disp, NX_DKGRAY, RGB(242, 242, 242), FONT_SMALL);
}

static void gmail_draw_msglist(GmailState* g, int lx, int ly, int lw, int lh) {
    fb_fillrect(lx, ly, lw, lh, NX_WHITE);

    // Header bar
    fb_fillrect(lx, ly, lw, 26, RGB(248, 248, 248));
    fb_hline(lx, ly + 26, lw, NX_LTGRAY);

    char hdr[32];
    ksprintf(hdr, "%s", folder_names[g->current_folder]);
    font_draw_string(lx + 8, ly + 7, hdr, NX_BLACK, RGB(248, 248, 248), FONT_MEDIUM);

    // Refresh button
    nx_draw_button(lx + lw - 64, ly + 3, 56, 20, "Refresh", false, false);

    int row_h = 44;
    int msg_y = ly + 28;
    int visible = (lh - 28) / row_h;
    int vis = 0;

    for (int i = 0; i < g->msg_count && vis < visible; i++) {
        GmailMsg* m = &g->msgs[i];
        if (m->deleted) continue;

        // Filter by folder (starred shows all starred)
        if (g->current_folder == 3) {
            if (!m->starred) continue;
        } else if (g->current_folder == 4) {
            continue; // Trash shows deleted=true, but we skip those
        } else {
            if (m->folder != g->current_folder) continue;
        }

        if (vis < g->scroll) { vis++; continue; }

        int ry = msg_y + (vis - g->scroll) * row_h;
        if (ry + row_h > ly + lh) break;

        bool sel = (g->selected == i);
        uint32_t bg = sel ? RGB(210, 227, 252) : NX_WHITE;
        uint32_t from_col = m->read ? NX_DKGRAY : NX_BLACK;
        int from_font = m->read ? FONT_SMALL : FONT_MEDIUM;

        fb_fillrect(lx, ry, lw, row_h - 1, bg);
        fb_hline(lx + 8, ry + row_h - 1, lw - 16, RGB(238, 238, 238));

        // Star
        draw_star(lx + 8, ry + 8, m->starred);

        // Unread dot
        if (!m->read) {
            for (int dy = -2; dy <= 2; dy++)
                for (int dx = -2; dx <= 2; dx++)
                    if (dx*dx + dy*dy <= 4)
                        fb_putpixel(lx + 24 + dx, ry + 11 + dy, RGB(66, 133, 244));
        }

        // From / subject / date
        int text_x = lx + 34;
        int text_w = lw - 80;

        // Truncate from name
        char from_disp[24];
        int maxfrom = text_w / 2 / font_char_width(FONT_SMALL);
        if (maxfrom > 23) maxfrom = 23;
        strncpy(from_disp, m->from, maxfrom);
        from_disp[maxfrom] = 0;

        font_draw_string(text_x, ry + 5, from_disp, from_col, bg, from_font);

        // Date on right
        font_draw_string(lx + lw - 50, ry + 5, m->date, NX_DKGRAY, bg, FONT_SMALL);

        // Subject
        char subj_disp[40];
        int maxsubj = (lw - 50) / font_char_width(FONT_SMALL);
        if (maxsubj > 39) maxsubj = 39;
        strncpy(subj_disp, m->subject, maxsubj);
        subj_disp[maxsubj] = 0;
        font_draw_string(text_x, ry + 20, subj_disp, NX_DKGRAY, bg, FONT_SMALL);

        // Preview of body (first line)
        char preview[40];
        int pi = 0;
        int maxprev = (lw - 50) / font_char_width(FONT_SMALL);
        if (maxprev > 39) maxprev = 39;
        for (int c = 0; m->body[c] && pi < maxprev; c++) {
            if (m->body[c] == '\n') { preview[pi++] = ' '; continue; }
            preview[pi++] = m->body[c];
        }
        preview[pi] = 0;
        font_draw_string(text_x, ry + 32, preview, RGB(150, 150, 150), bg, FONT_SMALL);

        vis++;
    }
}

static void gmail_draw_read(GmailState* g, int rx, int ry, int rw, int rh) {
    if (g->selected < 0 || g->selected >= g->msg_count) return;
    GmailMsg* m = &g->msgs[g->selected];

    fb_fillrect(rx, ry, rw, rh, NX_WHITE);

    // Toolbar
    fb_fillrect(rx, ry, rw, 30, RGB(248, 248, 248));
    fb_hline(rx, ry + 30, rw, NX_LTGRAY);

    nx_draw_button(rx + 4, ry + 4, 50, 22, "Back", false, false);
    nx_draw_button(rx + 58, ry + 4, 50, 22, "Reply", false, false);
    nx_draw_button(rx + 112, ry + 4, 60, 22, "Forward", false, false);
    nx_draw_button(rx + 176, ry + 4, 50, 22, "Delete", false, false);

    // Star toggle
    draw_star(rx + rw - 24, ry + 10, m->starred);

    int y = ry + 38;

    // Subject as title
    font_draw_string(rx + 12, y, m->subject, NX_BLACK, NX_WHITE, FONT_LARGE);
    y += 22;

    // From / To
    char buf[80];
    ksprintf(buf, "From: %s", m->from);
    font_draw_string(rx + 12, y, buf, NX_DKGRAY, NX_WHITE, FONT_SMALL);
    y += 14;
    ksprintf(buf, "To: %s", m->to);
    font_draw_string(rx + 12, y, buf, NX_DKGRAY, NX_WHITE, FONT_SMALL);
    y += 14;
    ksprintf(buf, "Date: %s", m->date);
    font_draw_string(rx + 12, y, buf, NX_DKGRAY, NX_WHITE, FONT_SMALL);
    y += 8;

    nx_draw_separator(rx + 8, y, rw - 16);
    y += 8;

    // Body
    int bx = rx + 12;
    for (int c = 0; m->body[c] && y < ry + rh - 8; c++) {
        if (m->body[c] == '\n') {
            bx = rx + 12;
            y += 14;
            continue;
        }
        if (bx + font_char_width(FONT_SMALL) > rx + rw - 8) {
            bx = rx + 12;
            y += 14;
        }
        font_draw_char(bx, y, m->body[c], NX_BLACK, NX_WHITE, FONT_SMALL);
        bx += font_char_width(FONT_SMALL);
    }
}

static void gmail_draw_compose(GmailState* g, int cx, int cy, int cw, int ch) {
    fb_fillrect(cx, cy, cw, ch, NX_WHITE);

    // Toolbar
    fb_fillrect(cx, cy, cw, 30, RGB(248, 248, 248));
    fb_hline(cx, cy + 30, cw, NX_LTGRAY);

    // Send button (blue)
    fb_fillrect(cx + 4, cy + 4, 60, 22, RGB(66, 133, 244));
    fb_rect(cx + 4, cy + 4, 60, 22, RGB(50, 100, 200));
    const char* stxt = "Send";
    int stw = strlen(stxt) * font_char_width(FONT_SMALL);
    font_draw_string(cx + 4 + (60 - stw) / 2, cy + 10, stxt, NX_WHITE, RGB(66, 133, 244), FONT_SMALL);

    nx_draw_button(cx + 70, cy + 4, 60, 22, "Discard", false, false);

    // Save as draft
    nx_draw_button(cx + 136, cy + 4, 50, 22, "Draft", false, false);

    int fy = cy + 36;

    // To field
    uint32_t tb0 = (g->compose_field == 0) ? RGB(66, 133, 244) : NX_LTGRAY;
    font_draw_string(cx + 8, fy + 4, "To", NX_DKGRAY, NX_WHITE, FONT_SMALL);
    fb_hline(cx + 28, fy + 18, cw - 36, tb0);
    font_draw_string(cx + 30, fy + 4, g->compose_to, NX_BLACK, NX_WHITE, FONT_SMALL);
    if (g->compose_field == 0 && (timer_get_ticks() / 50) % 2 == 0) {
        int cx2 = cx + 30 + g->to_len * font_char_width(FONT_SMALL);
        fb_fillrect(cx2, fy + 2, 1, 14, NX_BLACK);
    }
    fy += 24;

    // Subject field
    uint32_t tb1 = (g->compose_field == 1) ? RGB(66, 133, 244) : NX_LTGRAY;
    font_draw_string(cx + 8, fy + 4, "Subj", NX_DKGRAY, NX_WHITE, FONT_SMALL);
    fb_hline(cx + 36, fy + 18, cw - 44, tb1);
    font_draw_string(cx + 38, fy + 4, g->compose_subj, NX_BLACK, NX_WHITE, FONT_SMALL);
    if (g->compose_field == 1 && (timer_get_ticks() / 50) % 2 == 0) {
        int cx2 = cx + 38 + g->subj_len * font_char_width(FONT_SMALL);
        fb_fillrect(cx2, fy + 2, 1, 14, NX_BLACK);
    }
    fy += 24;

    nx_draw_separator(cx + 4, fy, cw - 8);
    fy += 4;

    // Body area
    int body_h = ch - (fy - cy) - 20;
    fb_fillrect(cx + 4, fy, cw - 8, body_h, NX_WHITE);
    if (g->compose_field == 2) fb_rect(cx + 4, fy, cw - 8, body_h, RGB(66, 133, 244));

    int bx = cx + 10, by = fy + 4;
    int char_w = font_char_width(FONT_SMALL);
    int max_x = cx + cw - 12;
    for (int i = 0; i < g->body_len; i++) {
        if (g->compose_body[i] == '\n' || bx + char_w > max_x) {
            bx = cx + 10;
            by += 13;
            if (g->compose_body[i] == '\n') continue;
        }
        font_draw_char(bx, by, g->compose_body[i], NX_BLACK, NX_WHITE, FONT_SMALL);
        bx += char_w;
    }
    if (g->compose_field == 2 && (timer_get_ticks() / 50) % 2 == 0)
        fb_fillrect(bx, by, 1, 11, NX_BLACK);

    // Status bar
    int sb_y = cy + ch - 16;
    fb_fillrect(cx, sb_y, cw, 16, RGB(248, 248, 248));
    fb_hline(cx, sb_y, cw, NX_LTGRAY);
    font_draw_string(cx + 8, sb_y + 3, "Tab: next field | Enter: new line", NX_DKGRAY, RGB(248, 248, 248), FONT_SMALL);
}

static void gmail_draw_sending(GmailState* g, int cx, int cy, int cw, int ch) {
    fb_fillrect(cx, cy, cw, ch, NX_WHITE);

    int my = cy + ch / 2 - 30;

    const char* t = "Sending Message...";
    int tw = strlen(t) * font_char_width(FONT_LARGE);
    font_draw_string(cx + (cw - tw) / 2, my, t, NX_BLACK, NX_WHITE, FONT_LARGE);
    my += 30;

    // Progress bar
    int pw = cw - 100;
    int px = cx + 50;
    fb_rect(px, my, pw, 16, NX_DKGRAY);
    int progress = g->smtp_step * pw / 10;
    if (progress > pw - 2) progress = pw - 2;
    fb_fillrect(px + 1, my + 1, progress, 14, RGB(66, 133, 244));
    my += 26;

    // Status
    tw = strlen(g->status_msg) * font_char_width(FONT_SMALL);
    font_draw_string(cx + (cw - tw) / 2, my, g->status_msg, NX_DKGRAY, NX_WHITE, FONT_SMALL);

    if (g->smtp_state == SMTP_DONE || g->smtp_state == SMTP_ERROR) {
        my += 20;
        uint32_t col = (g->smtp_state == SMTP_DONE) ? NX_GREEN : NX_RED;
        const char* res = (g->smtp_state == SMTP_DONE) ? "Delivered!" : "Send failed";
        tw = strlen(res) * font_char_width(FONT_MEDIUM);
        font_draw_string(cx + (cw - tw) / 2, my, res, col, NX_WHITE, FONT_MEDIUM);
        my += 24;
        nx_draw_button(cx + cw / 2 - 30, my, 60, 22, "OK", false, true);
    }
}

// Main draw
static void gmail_draw(Window* win, int cx, int cy, int cw, int ch) {
    GmailState* g = (GmailState*)win->userdata;
    if (!g) return;

    if (g->view == VIEW_LOGIN) {
        gmail_draw_login(g, cx, cy, cw, ch);
        return;
    }

    if (g->view == VIEW_SENDING) {
        gmail_smtp_poll(g);
        gmail_draw_sending(g, cx, cy, cw, ch);
        return;
    }

    // Main layout: sidebar + content
    int sidebar_w = 120;
    gmail_draw_sidebar(g, cx, cy, sidebar_w, ch);

    int content_x = cx + sidebar_w;
    int content_w = cw - sidebar_w;

    switch (g->view) {
    case VIEW_INBOX:
        gmail_draw_msglist(g, content_x, cy, content_w, ch);
        break;
    case VIEW_READ:
        gmail_draw_read(g, content_x, cy, content_w, ch);
        break;
    case VIEW_COMPOSE:
        gmail_draw_compose(g, content_x, cy, content_w, ch);
        break;
    default:
        break;
    }

    // Network status indicator
    bool net_up = net_is_up();
    uint32_t net_col = net_up ? NX_GREEN : NX_RED;
    for (int dy = -3; dy <= 3; dy++)
        for (int dx = -3; dx <= 3; dx++)
            if (dx*dx + dy*dy <= 9)
                fb_putpixel(cx + cw - 10 + dx, cy + ch - 10 + dy, net_col);
}

// Mouse handler
static void gmail_mouse(Window* win, int mx, int my, bool left, bool right) {
    (void)right;
    if (!left) return;
    GmailState* g = (GmailState*)win->userdata;
    if (!g) return;

    int cx, cy, cw, ch;
    wm_get_content_area(win, &cx, &cy, &cw, &ch);

    if (g->view == VIEW_LOGIN) {
        int fx = cx + 40;
        int fw = cw - 80;
        int ly = cy + 103; // Email field y (after logo area)

        // Email field
        if (mx >= fx && mx < fx + fw && my >= ly && my < ly + 22) {
            g->login_field = 0;
            return;
        }
        ly += 30;

        // Password field
        if (mx >= fx && mx < fx + fw && my >= ly && my < ly + 22) {
            g->login_field = 1;
            return;
        }
        ly += 34;

        // Sign In button
        int bw = 100, bh = 28;
        int bx = cx + (cw - bw) / 2;
        if (mx >= bx && mx < bx + bw && my >= ly && my < ly + bh) {
            if (g->email_len > 0) {
                g->logged_in = true;
                g->view = VIEW_INBOX;
                gmail_init_msgs(g);
                // Save credentials immediately
                strcpy(saved_email, g->email);
                strcpy(saved_password, g->password);
                saved_email_len = g->email_len;
                saved_pass_len = g->pass_len;
                has_saved_creds = true;
            }
            return;
        }
        return;
    }

    // Sidebar clicks
    int sidebar_w = 120;
    if (mx < cx + sidebar_w) {
        // Compose button
        if (my >= cy + 8 && my < cy + 36) {
            g->view = VIEW_COMPOSE;
            g->compose_to[0] = 0; g->to_len = 0;
            g->compose_subj[0] = 0; g->subj_len = 0;
            g->compose_body[0] = 0; g->body_len = 0;
            g->compose_field = 0;
            g->is_reply = false;
            return;
        }
        // Folder clicks
        int fy = cy + 48;
        for (int i = 0; i < GMAIL_MAX_FOLDERS; i++) {
            if (my >= fy && my < fy + 24) {
                g->current_folder = i;
                g->selected = -1;
                g->scroll = 0;
                g->view = VIEW_INBOX;
                return;
            }
            fy += 24;
        }
        return;
    }

    int content_x = cx + sidebar_w;
    int content_w = cw - sidebar_w;

    if (g->view == VIEW_INBOX) {
        // Refresh button
        if (my >= cy + 3 && my < cy + 23 && mx >= content_x + content_w - 64 && mx < content_x + content_w - 8) {
            // Refresh - just redraw
            return;
        }

        // Message clicks
        int row_h = 44;
        int msg_y = cy + 28;
        int vis = 0;
        for (int i = 0; i < g->msg_count; i++) {
            GmailMsg* m = &g->msgs[i];
            if (m->deleted) continue;
            if (g->current_folder == 3) {
                if (!m->starred) continue;
            } else if (g->current_folder == 4) {
                continue;
            } else {
                if (m->folder != g->current_folder) continue;
            }

            if (vis < g->scroll) { vis++; continue; }

            int ry = msg_y + (vis - g->scroll) * row_h;

            if (my >= ry && my < ry + row_h && mx >= content_x && mx < content_x + content_w) {
                // Star click
                if (mx >= content_x + 4 && mx < content_x + 20) {
                    m->starred = !m->starred;
                    return;
                }
                // Select and open
                g->selected = i;
                m->read = true;
                g->view = VIEW_READ;
                return;
            }
            vis++;
        }
        return;
    }

    if (g->view == VIEW_READ) {
        int rx = content_x;
        // Toolbar buttons
        if (my >= cy + 4 && my < cy + 26) {
            if (mx >= rx + 4 && mx < rx + 54) {
                // Back
                g->view = VIEW_INBOX;
                return;
            }
            if (mx >= rx + 58 && mx < rx + 108) {
                // Reply
                GmailMsg* m = &g->msgs[g->selected];
                g->view = VIEW_COMPOSE;
                g->is_reply = true;
                strcpy(g->compose_to, m->from);
                g->to_len = strlen(g->compose_to);
                ksprintf(g->compose_subj, "Re: %s", m->subject);
                g->subj_len = strlen(g->compose_subj);
                g->compose_body[0] = 0;
                g->body_len = 0;
                g->compose_field = 2;
                return;
            }
            if (mx >= rx + 112 && mx < rx + 172) {
                // Forward
                GmailMsg* m = &g->msgs[g->selected];
                g->view = VIEW_COMPOSE;
                g->is_reply = false;
                g->compose_to[0] = 0; g->to_len = 0;
                ksprintf(g->compose_subj, "Fwd: %s", m->subject);
                g->subj_len = strlen(g->compose_subj);
                // Copy original body
                ksprintf(g->compose_body, "\n--- Forwarded ---\n%s", m->body);
                g->body_len = strlen(g->compose_body);
                g->compose_field = 0;
                return;
            }
            if (mx >= rx + 176 && mx < rx + 226) {
                // Delete
                g->msgs[g->selected].deleted = true;
                g->view = VIEW_INBOX;
                g->selected = -1;
                return;
            }
        }
        // Star toggle
        if (mx >= rx + content_w - 28 && mx < rx + content_w - 12 && my >= cy + 8 && my < cy + 22) {
            g->msgs[g->selected].starred = !g->msgs[g->selected].starred;
            return;
        }
        return;
    }

    if (g->view == VIEW_COMPOSE) {
        int rx = content_x;

        // Toolbar
        if (my >= cy + 4 && my < cy + 26) {
            if (mx >= rx + 4 && mx < rx + 64) {
                // Send
                if (g->to_len > 0 && g->body_len > 0) {
                    // Save to Sent folder
                    if (g->msg_count < GMAIL_MAX_MSGS) {
                        GmailMsg* m = &g->msgs[g->msg_count];
                        strcpy(m->from, g->email);
                        strcpy(m->to, g->compose_to);
                        strcpy(m->subject, g->compose_subj[0] ? g->compose_subj : "(no subject)");
                        strcpy(m->body, g->compose_body);
                        strcpy(m->date, "Now");
                        m->read = true;
                        m->starred = false;
                        m->deleted = false;
                        m->folder = 1; // Sent
                        g->msg_count++;
                    }
                    // Try SMTP send
                    g->view = VIEW_SENDING;
                    g->smtp_state = SMTP_IDLE;
                    strcpy(g->status_msg, "Connecting to server...");
                    g->send_timer = 0;
                    gmail_smtp_send(g);
                }
                return;
            }
            if (mx >= rx + 70 && mx < rx + 130) {
                // Discard
                g->view = VIEW_INBOX;
                return;
            }
            if (mx >= rx + 136 && mx < rx + 186) {
                // Save as draft
                if (g->msg_count < GMAIL_MAX_MSGS) {
                    GmailMsg* m = &g->msgs[g->msg_count];
                    strcpy(m->from, g->email);
                    strcpy(m->to, g->compose_to);
                    strcpy(m->subject, g->compose_subj[0] ? g->compose_subj : "(draft)");
                    strcpy(m->body, g->compose_body);
                    strcpy(m->date, "Draft");
                    m->read = true;
                    m->starred = false;
                    m->deleted = false;
                    m->folder = 2; // Drafts
                    g->msg_count++;
                }
                g->view = VIEW_INBOX;
                return;
            }
        }

        // Field clicks
        int fy = cy + 36;
        if (my >= fy && my < fy + 22) { g->compose_field = 0; return; }
        fy += 24;
        if (my >= fy && my < fy + 22) { g->compose_field = 1; return; }
        if (my > fy + 24) { g->compose_field = 2; return; }
        return;
    }

    if (g->view == VIEW_SENDING) {
        if (g->smtp_state == SMTP_DONE || g->smtp_state == SMTP_ERROR) {
            // OK button
            int by_btn = cy + ch / 2 + 40;
            if (mx >= cx + cw / 2 - 30 && mx < cx + cw / 2 + 30 &&
                my >= by_btn && my < by_btn + 22) {
                // Clean up connections before going back
                if (g->smtp_sock) { net_tcp_close(g->smtp_sock); g->smtp_sock = nullptr; }
                g->view = VIEW_INBOX;
                g->smtp_state = SMTP_IDLE;
            }
        }
    }
}

// Key handler
static void gmail_key(Window* win, uint8_t key) {
    GmailState* g = (GmailState*)win->userdata;
    if (!g) return;

    if (g->view == VIEW_LOGIN) {
        char* buf;
        int* len;
        int maxlen;
        if (g->login_field == 0) {
            buf = g->email; len = &g->email_len; maxlen = GMAIL_ADDR_LEN - 1;
        } else {
            buf = g->password; len = &g->pass_len; maxlen = 30;
        }

        if (key == KEY_TAB) {
            g->login_field = 1 - g->login_field;
        } else if (key == KEY_ENTER) {
            if (g->login_field == 0) {
                g->login_field = 1;
            } else if (g->email_len > 0) {
                g->logged_in = true;
                g->view = VIEW_INBOX;
                gmail_init_msgs(g);
                // Save credentials immediately
                strcpy(saved_email, g->email);
                strcpy(saved_password, g->password);
                saved_email_len = g->email_len;
                saved_pass_len = g->pass_len;
                has_saved_creds = true;
            }
        } else if (key == KEY_BACKSPACE) {
            if (*len > 0) { (*len)--; buf[*len] = 0; }
        } else if (key >= 0x20 && key < 0x7F && *len < maxlen) {
            buf[*len] = key;
            (*len)++;
            buf[*len] = 0;
        }
        return;
    }

    if (g->view == VIEW_INBOX) {
        if (key == KEY_UP) {
            // Select previous
            for (int i = g->selected - 1; i >= 0; i--) {
                if (!g->msgs[i].deleted && g->msgs[i].folder == g->current_folder) {
                    g->selected = i;
                    break;
                }
            }
        } else if (key == KEY_DOWN) {
            for (int i = g->selected + 1; i < g->msg_count; i++) {
                if (!g->msgs[i].deleted && g->msgs[i].folder == g->current_folder) {
                    g->selected = i;
                    break;
                }
            }
        } else if (key == KEY_ENTER && g->selected >= 0) {
            g->msgs[g->selected].read = true;
            g->view = VIEW_READ;
        }
        return;
    }

    if (g->view == VIEW_READ) {
        if (key == KEY_BACKSPACE || key == KEY_ESCAPE) {
            g->view = VIEW_INBOX;
        }
        return;
    }

    if (g->view == VIEW_COMPOSE) {
        if (key == KEY_TAB) {
            g->compose_field = (g->compose_field + 1) % 3;
            return;
        }
        if (key == KEY_ESCAPE) {
            g->view = VIEW_INBOX;
            return;
        }

        char* buf;
        int* len;
        int maxlen;
        if (g->compose_field == 0) {
            buf = g->compose_to; len = &g->to_len; maxlen = GMAIL_ADDR_LEN - 1;
        } else if (g->compose_field == 1) {
            buf = g->compose_subj; len = &g->subj_len; maxlen = GMAIL_SUBJ_LEN - 1;
        } else {
            buf = g->compose_body; len = &g->body_len; maxlen = GMAIL_BODY_LEN - 1;
        }

        if (key == KEY_BACKSPACE) {
            if (*len > 0) { (*len)--; buf[*len] = 0; }
        } else if (key == KEY_ENTER && g->compose_field == 2) {
            if (*len < maxlen) { buf[*len] = '\n'; (*len)++; buf[*len] = 0; }
        } else if (key >= 0x20 && key < 0x7F && *len < maxlen) {
            buf[*len] = key;
            (*len)++;
            buf[*len] = 0;
        }
        return;
    }

    if (g->view == VIEW_SENDING) {
        if ((g->smtp_state == SMTP_DONE || g->smtp_state == SMTP_ERROR) && key == KEY_ENTER) {
            if (g->smtp_sock) { net_tcp_close(g->smtp_sock); g->smtp_sock = nullptr; }
            g->view = VIEW_INBOX;
            g->smtp_state = SMTP_IDLE;
        }
    }
}

static void gmail_close(Window* win) {
    GmailState* g = (GmailState*)win->userdata;
    if (g) {
        // Save credentials for next open
        if (g->logged_in && g->email_len > 0) {
            strcpy(saved_email, g->email);
            strcpy(saved_password, g->password);
            saved_email_len = g->email_len;
            saved_pass_len = g->pass_len;
            has_saved_creds = true;
        }
        if (g->smtp_sock) net_tcp_close(g->smtp_sock);
        kfree(g);
    }
}

extern "C" void app_launch_gmail() {
    Window* w = wm_create_window("Gateway Mail", 80, 30, 560, 420,
                                  WIN_CLOSEABLE | WIN_MINIATURIZE | WIN_RESIZABLE);
    if (!w) return;
    GmailState* g = (GmailState*)kmalloc(sizeof(GmailState));
    memset(g, 0, sizeof(GmailState));
    g->selected = -1;
    g->current_folder = 0;
    g->smtp_state = SMTP_IDLE;

    // Restore saved credentials from previous session if available
    if (has_saved_creds && saved_email_len > 0) {
        strcpy(g->email, saved_email);
        g->email_len = saved_email_len;
        strcpy(g->password, saved_password);
        g->pass_len = saved_pass_len;
        g->logged_in = true;
        g->view = VIEW_INBOX;
        gmail_init_msgs(g);
    } else {
        // Start at login screen - user enters credentials manually
        g->logged_in = false;
        g->view = VIEW_INBOX;
    }

    w->userdata = g;
    w->on_draw = gmail_draw;
    w->on_mouse = gmail_mouse;
    w->on_key = gmail_key;
    w->on_close = gmail_close;
}
