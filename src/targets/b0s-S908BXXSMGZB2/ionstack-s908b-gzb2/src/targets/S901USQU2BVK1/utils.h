#pragma once

/*
 * Target-specific utils for S901WVLS4DWL3.
 * Includes the base kernelsnitch utils then extends the print macros
 * with logfile output.  All static inline functions come from the base
 * header — no redefinitions needed.
 */
#include "kernelsnitch/utils.h"

#include <stdio.h>

extern FILE *g_logfile;

static inline void open_log(void) {
    g_logfile = fopen("/sdcard/cve-2026-43499.log", "a");
    if (g_logfile) {
        fprintf(g_logfile, "\n=== START ===\n");
        fflush(g_logfile);
    }
}

/*
 * Redefine the print macros to also write to g_logfile.
 * #undef first to suppress redefinition warnings.
 * ANDROID_APP_NO_LKM paths are left as-is (no logfile in app context).
 */
#ifndef ANDROID_APP_NO_LKM

#undef pr_error
#ifdef DEBUG
#define pr_error(fmt, ...) do { \
        printf(COLOR_RED "[!] %s:%d " COLOR_DEFAULT fmt, __FILE__, __LINE__, ##__VA_ARGS__); \
        if (g_logfile) { fprintf(g_logfile, "[!] %s:%d " fmt, __FILE__, __LINE__, ##__VA_ARGS__); fflush(g_logfile); } \
        exit(-1); \
    } while (0)
#else
#define pr_error(fmt, ...) do { \
        printf(COLOR_RED "[!] " COLOR_DEFAULT fmt, ##__VA_ARGS__); \
        if (g_logfile) { fprintf(g_logfile, "[!] " fmt, ##__VA_ARGS__); fflush(g_logfile); } \
        exit(-1); \
    } while (0)
#endif

#undef pr_warning
#ifdef DEBUG
#define pr_warning(fmt, ...) do { \
        printf(COLOR_RED "[-] %s:%d " COLOR_DEFAULT fmt, __FILE__, __LINE__, ##__VA_ARGS__); \
        if (g_logfile) { fprintf(g_logfile, "[-] %s:%d " fmt, __FILE__, __LINE__, ##__VA_ARGS__); fflush(g_logfile); } \
    } while (0)
#else
#define pr_warning(fmt, ...) do { \
        printf(COLOR_RED "[-] " COLOR_DEFAULT fmt, ##__VA_ARGS__); \
        if (g_logfile) { fprintf(g_logfile, "[-] " fmt, ##__VA_ARGS__); fflush(g_logfile); } \
    } while (0)
#endif

#undef pr_info
#ifdef DEBUG
#define pr_info(fmt, ...) do { \
        printf(COLOR_YELLOW "[*] %s:%d " COLOR_DEFAULT fmt, __FILE__, __LINE__, ##__VA_ARGS__); \
        if (g_logfile) { fprintf(g_logfile, "[*] %s:%d " fmt, __FILE__, __LINE__, ##__VA_ARGS__); fflush(g_logfile); } \
    } while (0)
#else
#define pr_info(fmt, ...) do { \
        printf(COLOR_YELLOW "[*] " COLOR_DEFAULT fmt, ##__VA_ARGS__); \
        if (g_logfile) { fprintf(g_logfile, "[*] " fmt, ##__VA_ARGS__); fflush(g_logfile); } \
    } while (0)
#endif

#undef pr_success
#ifdef DEBUG
#define pr_success(fmt, ...) do { \
        printf(COLOR_GREEN "[+] %s:%d " COLOR_DEFAULT fmt, __FILE__, __LINE__, ##__VA_ARGS__); \
        if (g_logfile) { fprintf(g_logfile, "[+] %s:%d " fmt, __FILE__, __LINE__, ##__VA_ARGS__); fflush(g_logfile); } \
    } while (0)
#else
#define pr_success(fmt, ...) do { \
        printf(COLOR_GREEN "[+] " COLOR_DEFAULT fmt, ##__VA_ARGS__); \
        if (g_logfile) { fprintf(g_logfile, "[+] " fmt, ##__VA_ARGS__); fflush(g_logfile); } \
    } while (0)
#endif

#endif /* !ANDROID_APP_NO_LKM */
