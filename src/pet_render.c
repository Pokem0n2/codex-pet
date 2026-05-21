#include "pet_common.h"
#include "pet_render.h"

int ensure_buffer(PetInst *pi)
{
    if (pi->memdc) return 1;

    HDC screen = GetDC(NULL);
    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = pi->w;
    bmi.bmiHeader.biHeight = -pi->h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    pi->dib = CreateDIBSection(screen, &bmi, DIB_RGB_COLORS, (void **)&pi->dib_pixels, NULL, 0);
    if (!pi->dib) { ReleaseDC(NULL, screen); return 0; }

    pi->memdc = CreateCompatibleDC(screen);
    if (!pi->memdc) { DeleteObject(pi->dib); pi->dib = NULL; ReleaseDC(NULL, screen); return 0; }

    pi->oldbmp = (HBITMAP)SelectObject(pi->memdc, pi->dib);
    ReleaseDC(NULL, screen);
    return 1;
}

void free_buffer(PetInst *pi)
{
    if (pi->memdc) {
        SelectObject(pi->memdc, pi->oldbmp);
        DeleteDC(pi->memdc);
        pi->memdc = NULL;
    }
    if (pi->dib) {
        DeleteObject(pi->dib);
        pi->dib = NULL;
    }
    pi->dib_pixels = NULL;
}

void render_frame_to_buffer(Pet *pet, int fx, int fy, BYTE *dst)
{
    if (!pet || !pet->pixels || !dst) return;
    int src_stride = pet->w * 4;
    int dst_stride = CELL_W * 4;
    for (int y = 0; y < CELL_H; y++) {
        int sy = fy + y;
        if (sy < 0 || sy >= pet->h || fx < 0 || fx + CELL_W > pet->w) {
            memset(dst + y * dst_stride, 0, dst_stride);
        } else {
            memcpy(dst + y * dst_stride, pet->pixels + sy * src_stride + fx * 4, dst_stride);
        }
    }
}

/* 缩放临时缓冲区指针，动态分配以避免占用 .data 段 */
static BYTE *g_scale_buf;

/* 确保缩放缓冲区已分配 */
static void ensure_scale_buf(void)
{
    if (!g_scale_buf)
        g_scale_buf = (BYTE *)malloc(CELL_W * CELL_H * 4);
}

void render_scaled_frame_to(Pet *pet, int fx, int fy, BYTE *dst, int dw, int dh)
{
    ensure_scale_buf();
    if (!g_scale_buf) return;
    render_frame_to_buffer(pet, fx, fy, g_scale_buf);
    for (int dy = 0; dy < dh; dy++) {
        int sy = (dy * CELL_H) / dh;
        for (int dx = 0; dx < dw; dx++) {
            int sx = (dx * CELL_W) / dw;
            *(DWORD *)&dst[(dy * dw + dx) * 4] = *(DWORD *)&g_scale_buf[(sy * CELL_W + sx) * 4];
        }
    }
}

void present_buffer(HWND hwnd, HDC memdc, int w, int h)
{
    POINT ptSrc = {0, 0};
    SIZE size = {w, h};
    BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    HDC screen = GetDC(NULL);
    UpdateLayeredWindow(hwnd, screen, NULL, &size, memdc, &ptSrc, 0, &bf, ULW_ALPHA);
    ReleaseDC(NULL, screen);
}

void update_preview(void)
{
    if (g_app.selected < 0 || g_app.selected >= g_app.pet_count) return;
    Pet *p = &g_app.pets[g_app.selected];
    if (!p->pixels || !g_app.prev_pixels) return;

    DWORD now = GetTickCount();
    if (now < g_app.preview_next) return;

    g_app.preview_frame++;
    if (g_app.preview_frame >= g_frame_counts[7]) g_app.preview_frame = 0;
    /* 预览统一使用 120ms 帧间隔，确保流畅循环 */
    g_app.preview_next = now + 120;

    render_preview_frame(p, g_app.preview_frame * CELL_W, 7 * CELL_H);
}

/* 将精灵帧居中渲染到固定大小的预览缓冲区（PREV_W × PREV_H） */
void render_preview_frame(Pet *pet, int fx, int fy)
{
    if (!pet || !pet->pixels || !g_app.prev_pixels) return;
    int pw = g_app.prev_w, ph = g_app.prev_h;
    int ox = (PREV_W - pw) / 2;
    int oy = (PREV_H - ph) / 2;
    memset(g_app.prev_pixels, 0, PREV_W * PREV_H * 4);
    ensure_scale_buf();
    if (!g_scale_buf) return;
    render_frame_to_buffer(pet, fx, fy, g_scale_buf);
    for (int dy = 0; dy < ph; dy++) {
        int sy = (dy * CELL_H) / ph;
        BYTE *row = g_app.prev_pixels + ((oy + dy) * PREV_W + ox) * 4;
        for (int dx = 0; dx < pw; dx++) {
            int sx = (dx * CELL_W) / pw;
            *(DWORD *)&row[dx * 4] = *(DWORD *)&g_scale_buf[(sy * CELL_W + sx) * 4];
        }
    }
}

void recreate_preview_buffer(void)
{
    HDC screen = GetDC(NULL);
    if (g_app.prev_memdc) {
        if (g_app.prev_oldbmp) SelectObject(g_app.prev_memdc, g_app.prev_oldbmp);
        DeleteDC(g_app.prev_memdc);
        g_app.prev_memdc = NULL;
    }
    if (g_app.prev_dib) { DeleteObject(g_app.prev_dib); g_app.prev_dib = NULL; }
    g_app.prev_pixels = NULL;

    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = g_app.prev_w;
    bmi.bmiHeader.biHeight = -g_app.prev_h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    g_app.prev_dib = CreateDIBSection(screen, &bmi, DIB_RGB_COLORS, (void **)&g_app.prev_pixels, NULL, 0);
    g_app.prev_memdc = CreateCompatibleDC(screen);
    g_app.prev_oldbmp = (HBITMAP)SelectObject(g_app.prev_memdc, g_app.prev_dib);
    ReleaseDC(NULL, screen);
}
