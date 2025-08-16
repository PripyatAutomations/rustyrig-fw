//
// gtk-client/win32.c
// 	This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.

#include <stdbool.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#include "rrclient/gtk.core.h"
#include <gdk/gdkwin32.h>
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")

void enable_windows_dark_mode_for_gtk_window(GtkWidget *window) {
   if (!gtk_widget_get_realized(window)) {
      return;
   }

   HWND hwnd = GDK_WINDOW_HWND(gtk_widget_get_window(window));
   if (!hwnd) {
      return;
   }

   BOOL use_dark = TRUE;
   // DWMWA_USE_IMMERSIVE_DARK_MODE = 20 or 19 depending on build
   int attr = 20;
   HRESULT hr = DwmSetWindowAttribute(hwnd, attr, &use_dark, sizeof(use_dark));
   if (FAILED(hr)) {
      // Try fallback for older builds
      attr = 19;
      DwmSetWindowAttribute(hwnd, attr, &use_dark, sizeof(use_dark));
   }
}

void disable_console_quick_edit(void) {
   HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
   DWORD mode = 0;

   if (!GetConsoleMode(hStdin, &mode)) return;

   // Remove QuickEdit and Insert modes
   mode &= ~(ENABLE_QUICK_EDIT_MODE | ENABLE_INSERT_MODE);
   SetConsoleMode(hStdin, mode);
}

bool win32_init(void) {
   return false;
}

char *strndup(const char *s, size_t n) {
    size_t len = strnlen(s, n);
    char *result = (char *)malloc(len + 1);
    if (!result) {
       return NULL;
    }

    memcpy(result, s, len);
    result[len] = '\0';
    return result;
}

char *strcasestr(const char *haystack, const char *needle) {
    if (!*needle) {
	   return (char *)haystack;
	}

    for (; *haystack; haystack++) {
        const char *h = haystack;
        const char *n = needle;

        while (*h && *n && tolower((unsigned char)*h) == tolower((unsigned char)*n)) {
            h++;
            n++;
        }

        if (!*n) {
           return (char *)haystack;
	}
    }

    return NULL;
}

bool is_windows_dark_mode() {
    DWORD value = 1;
    DWORD size = sizeof(DWORD);
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
       if (RegQueryValueExA(hKey, "AppsUseLightTheme", NULL, NULL, (LPBYTE)&value, &size) != ERROR_SUCCESS) {
          value = 1;
       }
       RegCloseKey(hKey);
    }
    return value == 0;  // 0 means dark mode}
}
#endif	// _WIN32
