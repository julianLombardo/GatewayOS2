#pragma once

#include "../lib/types.h"

// ============================================================
// Win32 API Shim Layer for GatewayOS2
// Provides minimal Win32-compatible API surface for loaded PEs
// ============================================================

// Win32 type aliases
typedef uint32_t DWORD;
typedef int32_t  LONG;
typedef uint16_t WORD;
typedef uint8_t  BYTE;
typedef int      BOOL;
typedef void*    HANDLE;
typedef void*    HWND;
typedef void*    HINSTANCE;
typedef void*    HDC;
typedef void*    HBRUSH;
typedef void*    HMODULE;
typedef void*    HICON;
typedef void*    HCURSOR;
typedef void*    HMENU;
typedef void*    LPVOID;
typedef const char* LPCSTR;
typedef char*    LPSTR;
typedef uint32_t UINT;
typedef int32_t  LRESULT;
typedef uint32_t WPARAM;
typedef int32_t  LPARAM;
typedef LRESULT (*WNDPROC)(HWND, UINT, WPARAM, LPARAM);

#define WINAPI __attribute__((stdcall))
#define CDECL  __attribute__((cdecl))
#define TRUE  1
#define FALSE 0
#define INVALID_HANDLE_VALUE ((HANDLE)(int32_t)-1)

// Console handles
#define STD_INPUT_HANDLE  ((DWORD)-10)
#define STD_OUTPUT_HANDLE ((DWORD)-11)
#define STD_ERROR_HANDLE  ((DWORD)-12)

// MessageBox flags
#define MB_OK                0x00000000
#define MB_OKCANCEL          0x00000001
#define MB_YESNO             0x00000004
#define MB_ICONINFORMATION   0x00000040
#define MB_ICONWARNING       0x00000030
#define MB_ICONERROR         0x00000010
#define IDOK     1
#define IDCANCEL 2
#define IDYES    6
#define IDNO     7

// Window Messages
#define WM_DESTROY  0x0002
#define WM_PAINT    0x000F
#define WM_CLOSE    0x0010
#define WM_QUIT     0x0012
#define WM_KEYDOWN  0x0100
#define WM_COMMAND  0x0111

// ShowWindow
#define SW_SHOW 5

// Window styles
#define WS_OVERLAPPEDWINDOW 0x00CF0000

// Colors
#define COLOR_WINDOW 5

// ============================================================
// Win32 Shim Import Table
// ============================================================

struct Win32Import {
    const char* name;
    uint32_t    address;   // Function pointer cast to uint32_t
};

// Resolve a Win32 function name to its shim address
// Returns 0 if not found
uint32_t win32_resolve(const char* dll_name, const char* func_name);

// Initialize win32 shim (call before loading any PE)
void win32_shim_init();

// Console output buffer (for CUI apps)
#define W32_CONSOLE_MAX 2048
extern char w32_console_buf[W32_CONSOLE_MAX];
extern int  w32_console_len;
extern bool w32_app_exited;
extern int  w32_exit_code;

// Message box state (set by MessageBoxA)
extern bool w32_msgbox_active;
extern char w32_msgbox_title[128];
extern char w32_msgbox_text[256];
extern uint32_t w32_msgbox_flags;

// Reset state between runs
void win32_shim_reset();
