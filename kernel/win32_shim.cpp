#include "win32_shim.h"
#include "../lib/string.h"
#include "../lib/printf.h"
#include "../memory/heap.h"
#include "timer.h"

// ============================================================
// Win32 Shim State
// ============================================================

char w32_console_buf[W32_CONSOLE_MAX];
int  w32_console_len = 0;
bool w32_app_exited = false;
int  w32_exit_code = 0;

bool w32_msgbox_active = false;
char w32_msgbox_title[128];
char w32_msgbox_text[256];
uint32_t w32_msgbox_flags = 0;

void win32_shim_reset() {
    memset(w32_console_buf, 0, W32_CONSOLE_MAX);
    w32_console_len = 0;
    w32_app_exited = false;
    w32_exit_code = 0;
    w32_msgbox_active = false;
    w32_msgbox_title[0] = 0;
    w32_msgbox_text[0] = 0;
    w32_msgbox_flags = 0;
}

// ============================================================
// Win32 API Shim Implementations
// ============================================================

// --- kernel32.dll ---

static void WINAPI shim_ExitProcess(UINT code) {
    kprintf("[W32] ExitProcess(%d)\n", code);
    w32_app_exited = true;
    w32_exit_code = (int)code;
}

static HANDLE WINAPI shim_GetStdHandle(DWORD nStdHandle) {
    (void)nStdHandle;
    // Return dummy handles
    if (nStdHandle == STD_OUTPUT_HANDLE) return (HANDLE)1;
    if (nStdHandle == STD_ERROR_HANDLE) return (HANDLE)2;
    if (nStdHandle == STD_INPUT_HANDLE) return (HANDLE)3;
    return INVALID_HANDLE_VALUE;
}

static BOOL WINAPI shim_WriteConsoleA(HANDLE h, const void* buf, DWORD nChars,
                                       DWORD* written, void* reserved) {
    (void)h; (void)reserved;
    const char* text = (const char*)buf;
    for (DWORD i = 0; i < nChars && w32_console_len < W32_CONSOLE_MAX - 1; i++) {
        w32_console_buf[w32_console_len++] = text[i];
    }
    w32_console_buf[w32_console_len] = 0;
    if (written) *written = nChars;
    kprintf("[W32] WriteConsoleA: %d chars\n", nChars);
    return TRUE;
}

static BOOL WINAPI shim_WriteFile(HANDLE h, const void* buf, DWORD nBytes,
                                   DWORD* written, void* overlapped) {
    (void)overlapped;
    // Redirect stdout/stderr to console buffer
    if (h == (HANDLE)1 || h == (HANDLE)2) {
        return shim_WriteConsoleA(h, buf, nBytes, written, nullptr);
    }
    if (written) *written = nBytes;
    return TRUE;
}

static LPVOID WINAPI shim_VirtualAlloc(LPVOID addr, uint32_t size, DWORD type, DWORD protect) {
    (void)addr; (void)type; (void)protect;
    kprintf("[W32] VirtualAlloc(size=%d)\n", size);
    void* p = kmalloc_aligned(size, 4096);
    if (p) memset(p, 0, size);
    return p;
}

static BOOL WINAPI shim_VirtualFree(LPVOID addr, uint32_t size, DWORD type) {
    (void)size; (void)type;
    if (addr) kfree(addr);
    return TRUE;
}

static LPVOID WINAPI shim_HeapAlloc(HANDLE heap, DWORD flags, uint32_t bytes) {
    (void)heap; (void)flags;
    void* p = kmalloc(bytes);
    if (p && (flags & 0x08)) memset(p, 0, bytes); // HEAP_ZERO_MEMORY
    return p;
}

static BOOL WINAPI shim_HeapFree(HANDLE heap, DWORD flags, LPVOID mem) {
    (void)heap; (void)flags;
    if (mem) kfree(mem);
    return TRUE;
}

static HANDLE WINAPI shim_GetProcessHeap() {
    return (HANDLE)0xDEAD; // Dummy handle
}

static void WINAPI shim_GetSystemInfo(void* si) {
    // Minimal: just zero it
    memset(si, 0, 36); // sizeof(SYSTEM_INFO)
}

static DWORD WINAPI shim_GetLastError() {
    return 0; // No error
}

static void WINAPI shim_SetLastError(DWORD err) {
    (void)err;
}

static DWORD WINAPI shim_GetTickCount() {
    return timer_get_ticks() * 10; // ~10ms per tick
}

static BOOL WINAPI shim_QueryPerformanceCounter(int64_t* count) {
    if (count) *count = timer_get_ticks();
    return TRUE;
}

static BOOL WINAPI shim_QueryPerformanceFrequency(int64_t* freq) {
    if (freq) *freq = 100; // 100 Hz
    return TRUE;
}

static void WINAPI shim_Sleep(DWORD ms) {
    (void)ms;
    // Can't really sleep in single-tasking OS; just return
    kprintf("[W32] Sleep(%d) - no-op\n", ms);
}

static HMODULE WINAPI shim_GetModuleHandleA(LPCSTR name) {
    (void)name;
    return (HMODULE)0x400000; // Fake base
}

static int WINAPI shim_GetModuleFileNameA(HMODULE hmod, LPSTR buf, DWORD size) {
    (void)hmod;
    const char* name = "C:\\GWOS\\app.exe";
    int len = strlen(name);
    if ((DWORD)len >= size) len = size - 1;
    memcpy(buf, name, len);
    buf[len] = 0;
    return len;
}

static DWORD WINAPI shim_GetCurrentProcessId() {
    return 1; // Single process
}

static DWORD WINAPI shim_GetCurrentThreadId() {
    return 1;
}

static BOOL WINAPI shim_CloseHandle(HANDLE h) {
    (void)h;
    return TRUE;
}

static int WINAPI shim_MultiByteToWideChar(UINT cp, DWORD flags, LPCSTR str, int len,
                                             uint16_t* wstr, int wlen) {
    (void)cp; (void)flags;
    if (len == -1) len = strlen(str) + 1;
    int count = (wlen < len) ? wlen : len;
    for (int i = 0; i < count; i++) wstr[i] = (uint8_t)str[i];
    return count;
}

static int WINAPI shim_WideCharToMultiByte(UINT cp, DWORD flags, const uint16_t* wstr,
                                            int wlen, LPSTR str, int len,
                                            LPCSTR defchar, BOOL* used_default) {
    (void)cp; (void)flags; (void)defchar; (void)used_default;
    if (wlen == -1) {
        wlen = 0;
        while (wstr[wlen]) wlen++;
        wlen++;
    }
    int count = (len < wlen) ? len : wlen;
    for (int i = 0; i < count; i++) str[i] = (char)(wstr[i] & 0xFF);
    return count;
}

static LPCSTR WINAPI shim_GetCommandLineA() {
    return "app.exe";
}

static void* WINAPI shim_GetEnvironmentStringsA() {
    static char env[] = "GWOS=1\0\0";
    return env;
}

static BOOL WINAPI shim_FreeEnvironmentStringsA(void* p) {
    (void)p;
    return TRUE;
}

static DWORD WINAPI shim_GetFileType(HANDLE h) {
    (void)h;
    return 2; // FILE_TYPE_CHAR (console)
}

static BOOL WINAPI shim_SetHandleCount(DWORD count) {
    (void)count;
    return TRUE;
}

static DWORD WINAPI shim_GetACP() {
    return 1252; // Windows-1252
}

static BOOL WINAPI shim_GetCPInfo(UINT cp, void* info) {
    (void)cp;
    memset(info, 0, 20); // sizeof(CPINFO)
    *(UINT*)info = 1; // MaxCharSize = 1
    return TRUE;
}

static BOOL WINAPI shim_IsDebuggerPresent() {
    return FALSE;
}

static void WINAPI shim_OutputDebugStringA(LPCSTR str) {
    kprintf("[W32-DBG] %s\n", str);
}

// --- user32.dll ---

static int WINAPI shim_MessageBoxA(HWND hwnd, LPCSTR text, LPCSTR title, UINT type) {
    (void)hwnd;
    kprintf("[W32] MessageBoxA: '%s' - '%s'\n", title ? title : "(null)", text ? text : "(null)");

    // Store for display by the PE runner app
    w32_msgbox_active = true;
    strncpy(w32_msgbox_title, title ? title : "Message", 127);
    w32_msgbox_title[127] = 0;
    strncpy(w32_msgbox_text, text ? text : "", 255);
    w32_msgbox_text[255] = 0;
    w32_msgbox_flags = type;

    // Also append to console buffer
    int n = w32_console_len;
    if (n < W32_CONSOLE_MAX - 2) {
        const char* prefix = "[MessageBox] ";
        while (*prefix && n < W32_CONSOLE_MAX - 2) w32_console_buf[n++] = *prefix++;
        if (title) while (*title && n < W32_CONSOLE_MAX - 2) w32_console_buf[n++] = *title++;
        if (n < W32_CONSOLE_MAX - 2) w32_console_buf[n++] = ':';
        if (n < W32_CONSOLE_MAX - 2) w32_console_buf[n++] = ' ';
        if (text) while (*text && n < W32_CONSOLE_MAX - 2) w32_console_buf[n++] = *text++;
        if (n < W32_CONSOLE_MAX - 2) w32_console_buf[n++] = '\n';
        w32_console_buf[n] = 0;
        w32_console_len = n;
    }

    return IDOK;
}

static BOOL WINAPI shim_ShowWindow(HWND hwnd, int cmd) {
    (void)hwnd; (void)cmd;
    return TRUE;
}

static BOOL WINAPI shim_UpdateWindow(HWND hwnd) {
    (void)hwnd;
    return TRUE;
}

static BOOL WINAPI shim_DestroyWindow(HWND hwnd) {
    (void)hwnd;
    return TRUE;
}

static void WINAPI shim_PostQuitMessage(int code) {
    w32_app_exited = true;
    w32_exit_code = code;
}

static LRESULT WINAPI shim_DefWindowProcA(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    (void)hwnd; (void)msg; (void)wp; (void)lp;
    return 0;
}

static int CDECL shim_wsprintfA(LPSTR buf, LPCSTR fmt, ...) {
    // Simplified: just copy format string
    strcpy(buf, fmt);
    return strlen(buf);
}

static BOOL WINAPI shim_GetMessageA(void* msg, HWND hwnd, UINT min, UINT max) {
    (void)msg; (void)hwnd; (void)min; (void)max;
    return FALSE; // No messages = quit
}

static BOOL WINAPI shim_TranslateMessage(const void* msg) {
    (void)msg;
    return FALSE;
}

static LRESULT WINAPI shim_DispatchMessageA(const void* msg) {
    (void)msg;
    return 0;
}

static HWND WINAPI shim_CreateWindowExA(DWORD exStyle, LPCSTR className, LPCSTR windowName,
                                          DWORD style, int x, int y, int w, int h,
                                          HWND parent, HMENU menu, HINSTANCE inst, LPVOID param) {
    (void)exStyle; (void)className; (void)style; (void)x; (void)y;
    (void)w; (void)h; (void)parent; (void)menu; (void)inst; (void)param;
    kprintf("[W32] CreateWindowExA: '%s'\n", windowName ? windowName : "(null)");
    return (HWND)0xA001; // Fake window handle
}

static uint16_t WINAPI shim_RegisterClassExA(const void* wc) {
    (void)wc;
    return 1; // Success atom
}

static HCURSOR WINAPI shim_LoadCursorA(HINSTANCE inst, LPCSTR name) {
    (void)inst; (void)name;
    return (HCURSOR)1;
}

static HICON WINAPI shim_LoadIconA(HINSTANCE inst, LPCSTR name) {
    (void)inst; (void)name;
    return (HICON)1;
}

// --- msvcrt.dll / CRT (cdecl calling convention) ---

static void CDECL shim_puts(const char* str) {
    int n = w32_console_len;
    while (*str && n < W32_CONSOLE_MAX - 2) w32_console_buf[n++] = *str++;
    if (n < W32_CONSOLE_MAX - 1) w32_console_buf[n++] = '\n';
    w32_console_buf[n] = 0;
    w32_console_len = n;
}

static int CDECL shim_printf(const char* fmt, ...) {
    // Simplified: just output the format string
    shim_puts(fmt);
    return strlen(fmt);
}

static void* CDECL shim_malloc(uint32_t size) {
    return kmalloc(size);
}

static void CDECL shim_free(void* ptr) {
    if (ptr) kfree(ptr);
}

static void* CDECL shim_calloc(uint32_t n, uint32_t size) {
    uint32_t total = n * size;
    void* p = kmalloc(total);
    if (p) memset(p, 0, total);
    return p;
}

static void* CDECL shim_realloc(void* ptr, uint32_t size) {
    return krealloc(ptr, size);
}

static void* CDECL shim_memset_shim(void* dest, int val, uint32_t count) {
    return memset(dest, val, count);
}

static void* CDECL shim_memcpy_shim(void* dest, const void* src, uint32_t count) {
    return memcpy(dest, src, count);
}

static int CDECL shim_strlen_shim(const char* s) {
    return strlen(s);
}

static int CDECL shim_strcmp_shim(const char* a, const char* b) {
    return strcmp(a, b);
}

static char* CDECL shim_strcpy_shim(char* dst, const char* src) {
    return strcpy(dst, src);
}

static void CDECL shim_exit(int code) {
    w32_app_exited = true;
    w32_exit_code = code;
}

static void CDECL shim_abort() {
    w32_app_exited = true;
    w32_exit_code = -1;
}

// ============================================================
// Import Resolution Table
// ============================================================

struct DllShimEntry {
    const char* func_name;
    uint32_t    addr;
};

static bool stricmp_match(const char* a, const char* b) {
    while (*a && *b) {
        char ca = (*a >= 'A' && *a <= 'Z') ? *a + 32 : *a;
        char cb = (*b >= 'A' && *b <= 'Z') ? *b + 32 : *b;
        if (ca != cb) return false;
        a++; b++;
    }
    return *a == *b;
}

// Case-insensitive DLL name match helper
static bool dll_match(const char* dll_name, const char* match) {
    // Compare ignoring case and optional .dll extension
    const char* a = dll_name;
    const char* b = match;
    while (*a && *b) {
        char ca = (*a >= 'A' && *a <= 'Z') ? *a + 32 : *a;
        char cb = (*b >= 'A' && *b <= 'Z') ? *b + 32 : *b;
        if (ca != cb) return false;
        a++; b++;
    }
    // b is exhausted; a may have ".dll" remaining
    if (*b == 0 && *a == 0) return true;
    if (*b == 0) return stricmp_match(a, ".dll");
    return false;
}

#define SHIM(fn) {#fn, (uint32_t)(void*)shim_##fn}

static DllShimEntry kernel32_shims[] = {
    {"ExitProcess",                 (uint32_t)(void*)shim_ExitProcess},
    {"GetStdHandle",                (uint32_t)(void*)shim_GetStdHandle},
    {"WriteConsoleA",               (uint32_t)(void*)shim_WriteConsoleA},
    {"WriteFile",                   (uint32_t)(void*)shim_WriteFile},
    {"VirtualAlloc",                (uint32_t)(void*)shim_VirtualAlloc},
    {"VirtualFree",                 (uint32_t)(void*)shim_VirtualFree},
    {"HeapAlloc",                   (uint32_t)(void*)shim_HeapAlloc},
    {"HeapFree",                    (uint32_t)(void*)shim_HeapFree},
    {"GetProcessHeap",              (uint32_t)(void*)shim_GetProcessHeap},
    {"GetSystemInfo",               (uint32_t)(void*)shim_GetSystemInfo},
    {"GetLastError",                (uint32_t)(void*)shim_GetLastError},
    {"SetLastError",                (uint32_t)(void*)shim_SetLastError},
    {"GetTickCount",                (uint32_t)(void*)shim_GetTickCount},
    {"QueryPerformanceCounter",     (uint32_t)(void*)shim_QueryPerformanceCounter},
    {"QueryPerformanceFrequency",   (uint32_t)(void*)shim_QueryPerformanceFrequency},
    {"Sleep",                       (uint32_t)(void*)shim_Sleep},
    {"GetModuleHandleA",            (uint32_t)(void*)shim_GetModuleHandleA},
    {"GetModuleFileNameA",          (uint32_t)(void*)shim_GetModuleFileNameA},
    {"GetCurrentProcessId",         (uint32_t)(void*)shim_GetCurrentProcessId},
    {"GetCurrentThreadId",          (uint32_t)(void*)shim_GetCurrentThreadId},
    {"CloseHandle",                 (uint32_t)(void*)shim_CloseHandle},
    {"MultiByteToWideChar",         (uint32_t)(void*)shim_MultiByteToWideChar},
    {"WideCharToMultiByte",         (uint32_t)(void*)shim_WideCharToMultiByte},
    {"GetCommandLineA",             (uint32_t)(void*)shim_GetCommandLineA},
    {"GetEnvironmentStringsA",      (uint32_t)(void*)shim_GetEnvironmentStringsA},
    {"FreeEnvironmentStringsA",     (uint32_t)(void*)shim_FreeEnvironmentStringsA},
    {"GetFileType",                 (uint32_t)(void*)shim_GetFileType},
    {"SetHandleCount",              (uint32_t)(void*)shim_SetHandleCount},
    {"GetACP",                      (uint32_t)(void*)shim_GetACP},
    {"GetCPInfo",                   (uint32_t)(void*)shim_GetCPInfo},
    {"IsDebuggerPresent",           (uint32_t)(void*)shim_IsDebuggerPresent},
    {"OutputDebugStringA",          (uint32_t)(void*)shim_OutputDebugStringA},
    {nullptr, 0}
};

static DllShimEntry user32_shims[] = {
    {"MessageBoxA",                 (uint32_t)(void*)shim_MessageBoxA},
    {"ShowWindow",                  (uint32_t)(void*)shim_ShowWindow},
    {"UpdateWindow",                (uint32_t)(void*)shim_UpdateWindow},
    {"DestroyWindow",               (uint32_t)(void*)shim_DestroyWindow},
    {"PostQuitMessage",             (uint32_t)(void*)shim_PostQuitMessage},
    {"DefWindowProcA",              (uint32_t)(void*)shim_DefWindowProcA},
    {"wsprintfA",                   (uint32_t)(void*)shim_wsprintfA},
    {"GetMessageA",                 (uint32_t)(void*)shim_GetMessageA},
    {"TranslateMessage",            (uint32_t)(void*)shim_TranslateMessage},
    {"DispatchMessageA",            (uint32_t)(void*)shim_DispatchMessageA},
    {"CreateWindowExA",             (uint32_t)(void*)shim_CreateWindowExA},
    {"RegisterClassExA",            (uint32_t)(void*)shim_RegisterClassExA},
    {"LoadCursorA",                 (uint32_t)(void*)shim_LoadCursorA},
    {"LoadIconA",                   (uint32_t)(void*)shim_LoadIconA},
    {nullptr, 0}
};

static DllShimEntry msvcrt_shims[] = {
    {"puts",      (uint32_t)(void*)shim_puts},
    {"printf",    (uint32_t)(void*)shim_printf},
    {"malloc",    (uint32_t)(void*)shim_malloc},
    {"free",      (uint32_t)(void*)shim_free},
    {"calloc",    (uint32_t)(void*)shim_calloc},
    {"realloc",   (uint32_t)(void*)shim_realloc},
    {"memset",    (uint32_t)(void*)shim_memset_shim},
    {"memcpy",    (uint32_t)(void*)shim_memcpy_shim},
    {"strlen",    (uint32_t)(void*)shim_strlen_shim},
    {"strcmp",    (uint32_t)(void*)shim_strcmp_shim},
    {"strcpy",    (uint32_t)(void*)shim_strcpy_shim},
    {"exit",      (uint32_t)(void*)shim_exit},
    {"_exit",     (uint32_t)(void*)shim_exit},
    {"abort",     (uint32_t)(void*)shim_abort},
    {nullptr, 0}
};

uint32_t win32_resolve(const char* dll_name, const char* func_name) {
    DllShimEntry* table = nullptr;

    if (dll_match(dll_name, "kernel32") || dll_match(dll_name, "KERNEL32"))
        table = kernel32_shims;
    else if (dll_match(dll_name, "user32") || dll_match(dll_name, "USER32"))
        table = user32_shims;
    else if (dll_match(dll_name, "msvcrt") || dll_match(dll_name, "MSVCRT") ||
             dll_match(dll_name, "ucrtbase") || dll_match(dll_name, "api-ms-win-crt"))
        table = msvcrt_shims;

    if (!table) {
        kprintf("[W32] Unknown DLL: %s\n", dll_name);
        // Try all tables as fallback
        for (DllShimEntry* e = kernel32_shims; e->func_name; e++)
            if (strcmp(e->func_name, func_name) == 0) return e->addr;
        for (DllShimEntry* e = user32_shims; e->func_name; e++)
            if (strcmp(e->func_name, func_name) == 0) return e->addr;
        for (DllShimEntry* e = msvcrt_shims; e->func_name; e++)
            if (strcmp(e->func_name, func_name) == 0) return e->addr;
        return 0;
    }

    for (DllShimEntry* e = table; e->func_name; e++) {
        if (strcmp(e->func_name, func_name) == 0)
            return e->addr;
    }

    return 0;
}

void win32_shim_init() {
    win32_shim_reset();
    kprintf("[W32] Win32 shim layer initialized (%d kernel32, %d user32, %d msvcrt functions)\n",
                  (int)(sizeof(kernel32_shims)/sizeof(kernel32_shims[0]) - 1),
                  (int)(sizeof(user32_shims)/sizeof(user32_shims[0]) - 1),
                  (int)(sizeof(msvcrt_shims)/sizeof(msvcrt_shims[0]) - 1));
}
