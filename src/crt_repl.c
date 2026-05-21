/*
 * crt_repl.c — CRT 函数替换实现
 * 使用 Win32 API 和 x87 FPU 替代标准 CRT，消除对 ucrt/vcruntime 的依赖，
 * 从而大幅减小 exe 体积（减少导入表、DLL 加载开销）。
 */

/* 防止编译器使用内联形式，强制调用我们的实现 */
#pragma function(memset, memcpy)

#include <windows.h>

/* ── 内存分配 ─────────────────────────────────────────── */

void *calloc(size_t num, size_t size)
{
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, num * size);
}

void *malloc(size_t size)
{
    return HeapAlloc(GetProcessHeap(), 0, size);
}

void free(void *ptr)
{
    if (ptr) HeapFree(GetProcessHeap(), 0, ptr);
}

/* ── 内存操作 ─────────────────────────────────────────── */

void *memset(void *s, int c, size_t n)
{
    unsigned char *p = (unsigned char *)s;
    while (n--) *p++ = (unsigned char)c;
    return s;
}

void *memcpy(void *dest, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    while (n--) *d++ = *s++;
    return dest;
}

/* ── 字符串操作 ───────────────────────────────────────── */

size_t strlen(const char *s)
{
    size_t len = 0;
    while (*s++) len++;
    return len;
}

size_t wcslen(const wchar_t *s)
{
    size_t len = 0;
    while (*s++) len++;
    return len;
}

wchar_t *wcscpy(wchar_t *dest, const wchar_t *src)
{
    wchar_t *d = dest;
    while ((*d++ = *src++));
    return dest;
}

int wcscmp(const wchar_t *s1, const wchar_t *s2)
{
    while (*s1 && *s1 == *s2) { s1++; s2++; }
    return (int)(*s1 - *s2);
}

wchar_t *wcsrchr(const wchar_t *s, wchar_t c)
{
    wchar_t *last = NULL;
    while (*s) {
        if (*s == c) last = (wchar_t *)s;
        s++;
    }
    return last;
}

char *strstr(const char *haystack, const char *needle)
{
    if (!*needle) return (char *)haystack;
    while (*haystack) {
        const char *h = haystack, *n = needle;
        while (*h && *n && *h == *n) { h++; n++; }
        if (!*n) return (char *)haystack;
        haystack++;
    }
    return NULL;
}

/* ── 随机数（LCG，与 MSVC rand() 兼容） ────────────────── */

static unsigned long g_seed = 1;

void srand(unsigned int seed)
{
    g_seed = seed;
}

int rand(void)
{
    g_seed = g_seed * 214013UL + 2531011UL;
    return (int)((g_seed >> 16) & 0x7fff);
}

/* ── 时间（使用 GetTickCount 替代 time()） ──────────────── */

__int64 _time64(__int64 *t)
{
    /* 使用 32 位运算避免 __aulldiv 依赖 */
    unsigned long val = GetTickCount() / 1000;
    if (t) *t = (__int64)val;
    return (__int64)val;
}

/* ── 数学函数（x87 FPU 实现） ──────────────────────────── */

double __cdecl sin(double x)
{
    __asm {
        fld qword ptr [x]
        fsin
    }
}

double __cdecl cos(double x)
{
    __asm {
        fld qword ptr [x]
        fcos
    }
}

double __cdecl sqrt(double x)
{
    __asm {
        fld qword ptr [x]
        fsqrt
    }
}
