#define INITGUID
#define COBJMACROS
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <wincodec.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ---------- constants ---------- */
#define MAX_PETS        64
#define MAX_INSTANCES   256
#define ATLAS_W         1536
#define ATLAS_H         1872
#define CELL_W          192
#define CELL_H          208
#define PET_W           48
#define PET_H           52
#define PREV_W          96
#define PREV_H          104
#define COLS            8
#define ROWS            9

#define IDC_LISTBOX     100
#define IDT_PREVIEW     1
#define IDT_PET         2

static const int g_frame_counts[ROWS] = {6, 8, 8, 4, 5, 8, 6, 6, 6};

/* per-frame durations (ms) from hatch-pet/scripts/render_animation_previews.py */
static const int g_frame_durations[ROWS][8] = {
    {280, 110, 110, 140, 140, 320,   0,   0}, /* idle          */
    {120, 120, 120, 120, 120, 120, 120, 220}, /* running-right */
    {120, 120, 120, 120, 120, 120, 120, 220}, /* running-left  */
    {140, 140, 140, 280,   0,   0,   0,   0}, /* waving        */
    {140, 140, 140, 140, 280,   0,   0,   0}, /* jumping       */
    {140, 140, 140, 140, 140, 140, 140, 240}, /* failed        */
    {150, 150, 150, 150, 150, 260,   0,   0}, /* waiting       */
    {120, 120, 120, 120, 120, 220,   0,   0}, /* running       */
    {150, 150, 150, 150, 150, 280,   0,   0}, /* review        */
};

/* ---------- data ---------- */
typedef struct Pet {
    wchar_t id[64];
    wchar_t name[128];
    wchar_t desc[256];
    wchar_t sprite_path[MAX_PATH];
    BYTE *pixels;
    int w, h;
} Pet;

typedef struct PetInst {
    HWND hwnd;
    Pet *pet;
    int state;
    int frame;
    int x, y;
    DWORD next_tick;
    int alive;
    HDC memdc;
    HBITMAP oldbmp;
    HBITMAP dib;
    BYTE *dib_pixels;
    /* drag state */
    int dragging;
    int drag_anchor_x;
    int drag_anchor_y;
    int last_drag_x;
    DWORD last_drag_tick;
    float drag_speed;
    int temp_anim;
    /* jump physics */
    int jump_active;
    int jump_origin_y;
    float jump_vy;
} PetInst;

typedef struct {
    HINSTANCE hinst;
    IWICImagingFactory *wic;
    Pet pets[MAX_PETS];
    int pet_count;
    int selected;
    HWND selector;
    HWND preview;
    HWND listbox;
    HWND desc_label;
    PetInst instances[MAX_INSTANCES];
    int instance_count;
    int preview_state;
    int preview_frame;
    DWORD preview_next;
    HDC prev_memdc;
    HBITMAP prev_dib;
    BYTE *prev_pixels;
} App;

static wchar_t g_base_dir[MAX_PATH];
static App g_app = {0};
static HWND g_focused_pet = NULL;

/* ---------- json helpers ---------- */
static const char *json_find_value(const char *json, const char *key)
{
    const char *p = strstr(json, key);
    if (!p) return NULL;
    p += strlen(key);
    while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == ':'))
        p++;
    if (*p != '"') return NULL;
    p++;
    return p;
}

static void json_copy_str(const char *src, char *dst, int max)
{
    int i = 0;
    while (src[i] && src[i] != '"' && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

/* ---------- directory scan ---------- */
static void wcat(wchar_t *dst, const wchar_t *src, int max)
{
    int d = (int)wcslen(dst);
    int s = 0;
    while (src[s] && d < max - 1)
        dst[d++] = src[s++];
    dst[d] = 0;
}

static void init_base_dir(void)
{
    GetModuleFileNameW(NULL, g_base_dir, MAX_PATH);
    wchar_t *p = wcsrchr(g_base_dir, L'\\');
    if (p) *(p + 1) = 0;
}

static void scan_pets(void)
{
    WIN32_FIND_DATAW fd;
    wchar_t search[MAX_PATH];
    wcscpy(search, g_base_dir);
    wcat(search, L"my-pet\\*", MAX_PATH);
    HANDLE h = FindFirstFileW(search, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
            wcscmp(fd.cFileName, L".") != 0 && wcscmp(fd.cFileName, L"..") != 0) {
            wchar_t json_path[MAX_PATH];
            wcscpy(json_path, g_base_dir);
            wcat(json_path, L"my-pet\\", MAX_PATH);
            wcat(json_path, fd.cFileName, MAX_PATH);
            wcat(json_path, L"\\pet.json", MAX_PATH);

            HANDLE fh = CreateFileW(json_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
            if (fh != INVALID_HANDLE_VALUE) {
                DWORD size = GetFileSize(fh, NULL);
                if (size > 0 && size < 65536) {
                    char *buf = (char *)malloc(size + 1);
                    DWORD rd = 0;
                    ReadFile(fh, buf, size, &rd, NULL);
                    CloseHandle(fh);
                    buf[rd] = 0;

                    Pet *p = &g_app.pets[g_app.pet_count];
                    const char *v;
                    char tmp[256];

                    v = json_find_value(buf, "\"id\"");
                    if (v) { json_copy_str(v, tmp, sizeof(tmp)); MultiByteToWideChar(CP_UTF8, 0, tmp, -1, p->id, 64); }
                    else { wcscpy(p->id, fd.cFileName); }

                    v = json_find_value(buf, "\"displayName\"");
                    if (v) { json_copy_str(v, tmp, sizeof(tmp)); MultiByteToWideChar(CP_UTF8, 0, tmp, -1, p->name, 128); }
                    else { wcscpy(p->name, p->id); }

                    v = json_find_value(buf, "\"description\"");
                    if (v) { json_copy_str(v, tmp, sizeof(tmp)); MultiByteToWideChar(CP_UTF8, 0, tmp, -1, p->desc, 256); }
                    else { p->desc[0] = 0; }

                    wchar_t sprite_name[MAX_PATH];
                    v = json_find_value(buf, "\"spritesheetPath\"");
                    if (v) { json_copy_str(v, tmp, sizeof(tmp)); MultiByteToWideChar(CP_UTF8, 0, tmp, -1, sprite_name, MAX_PATH); }
                    else { wcscpy(sprite_name, L"spritesheet.webp"); }

                    wcscpy(p->sprite_path, g_base_dir);
                    wcat(p->sprite_path, L"my-pet\\", MAX_PATH);
                    wcat(p->sprite_path, fd.cFileName, MAX_PATH);
                    wcat(p->sprite_path, L"\\", MAX_PATH);
                    wcat(p->sprite_path, sprite_name, MAX_PATH);

                    g_app.pet_count++;
                    free(buf);
                } else {
                    CloseHandle(fh);
                }
            }
        }
    } while (g_app.pet_count < MAX_PETS && FindNextFileW(h, &fd));
    FindClose(h);
}

/* ---------- WIC image loading ---------- */
static int load_pet(Pet *pet)
{
    if (pet->pixels) return 1;

    IWICBitmapDecoder *decoder = NULL;
    IWICBitmapFrameDecode *frame = NULL;
    IWICFormatConverter *converter = NULL;
    HRESULT hr;

    hr = IWICImagingFactory_CreateDecoderFromFilename(g_app.wic, pet->sprite_path, NULL,
        GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder);
    if (FAILED(hr)) return 0;

    hr = IWICBitmapDecoder_GetFrame(decoder, 0, &frame);
    if (FAILED(hr)) goto done;

    UINT w, h;
    IWICBitmapFrameDecode_GetSize(frame, &w, &h);
    pet->w = (int)w;
    pet->h = (int)h;

    hr = IWICImagingFactory_CreateFormatConverter(g_app.wic, &converter);
    if (FAILED(hr)) goto done;

    hr = IWICFormatConverter_Initialize(converter, (IWICBitmapSource *)frame,
        &GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, NULL, 0.0,
        WICBitmapPaletteTypeCustom);

    if (SUCCEEDED(hr)) {
        pet->pixels = (BYTE *)malloc(w * h * 4);
        if (pet->pixels) {
            hr = IWICFormatConverter_CopyPixels(converter, NULL, w * 4, w * h * 4, pet->pixels);
            if (SUCCEEDED(hr)) {
                for (UINT i = 0; i < w * h; i++) {
                    if (pet->pixels[i * 4 + 3] == 0) {
                        pet->pixels[i * 4 + 0] = 0;
                        pet->pixels[i * 4 + 1] = 0;
                        pet->pixels[i * 4 + 2] = 0;
                    }
                }
            }
        }
    }

    if (converter) IWICFormatConverter_Release(converter);
done:
    if (frame) IWICBitmapFrameDecode_Release(frame);
    IWICBitmapDecoder_Release(decoder);
    return pet->pixels != NULL;
}

static void free_pet(Pet *pet)
{
    if (pet->pixels) {
        free(pet->pixels);
        pet->pixels = NULL;
    }
}

/* ---------- render buffer ---------- */
static int ensure_buffer(PetInst *pi)
{
    if (pi->memdc) return 1;

    HDC screen = GetDC(NULL);
    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = PET_W;
    bmi.bmiHeader.biHeight = -PET_H;
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

static void free_buffer(PetInst *pi)
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

/* ---------- frame rendering ---------- */
static void render_frame_to_buffer(Pet *pet, int fx, int fy, BYTE *dst)
{
    if (!pet || !pet->pixels || !dst) return;
    int src_stride = pet->w * 4;
    int dst_stride = CELL_W * 4;
    for (int y = 0; y < CELL_H; y++) {
        int sy = fy + y;
        if (sy >= pet->h) break;
        BYTE *src = pet->pixels + sy * src_stride + fx * 4;
        memcpy(dst + y * dst_stride, src, dst_stride);
    }
}

static void render_scaled_frame_to(Pet *pet, int fx, int fy, BYTE *dst, int dw, int dh)
{
    BYTE src[CELL_W * CELL_H * 4];
    render_frame_to_buffer(pet, fx, fy, src);
    for (int dy = 0; dy < dh; dy++) {
        int sy = (dy * CELL_H) / dh;
        for (int dx = 0; dx < dw; dx++) {
            int sx = (dx * CELL_W) / dw;
            for (int c = 0; c < 4; c++) {
                dst[(dy * dw + dx) * 4 + c] = src[(sy * CELL_W + sx) * 4 + c];
            }
        }
    }
}

static void present_buffer(HWND hwnd, HDC memdc)
{
    POINT ptSrc = {0, 0};
    SIZE size = {PET_W, PET_H};
    BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    HDC screen = GetDC(NULL);
    UpdateLayeredWindow(hwnd, screen, NULL, &size, memdc, &ptSrc, 0, &bf, ULW_ALPHA);
    ReleaseDC(NULL, screen);
}

/* ---------- preview update ---------- */
static void update_preview(void)
{
    if (g_app.selected < 0 || g_app.selected >= g_app.pet_count) return;
    Pet *p = &g_app.pets[g_app.selected];
    if (!p->pixels || !g_app.prev_pixels) return;

    DWORD now = GetTickCount();
    if (now < g_app.preview_next) return;

    g_app.preview_frame++;
    if (g_app.preview_frame >= g_frame_counts[7]) g_app.preview_frame = 0;
    g_app.preview_next = now + g_frame_durations[7][g_app.preview_frame];

    int sx = g_app.preview_frame * CELL_W;
    int sy = 7 * CELL_H;
    render_scaled_frame_to(p, sx, sy, g_app.prev_pixels, PREV_W, PREV_H);
    present_buffer(g_app.preview, g_app.prev_memdc);
}

/* ---------- pet instance management ---------- */
static void pet_trigger_anim(HWND hwnd, int target)
{
    PetInst *pi = (PetInst *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (!pi || !pi->alive || !pi->pet || !pi->pet->pixels) return;
    if (pi->jump_active && target != 4) {
        pi->y = pi->jump_origin_y;
        SetWindowPos(hwnd, NULL, pi->x, pi->y, 0, 0,
            SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    pi->state = target;
    pi->frame = 0;
    pi->temp_anim = 1;
    if (target == 4) {
        pi->jump_active = 1;
        pi->jump_origin_y = pi->y;
        pi->jump_vy = -12.0f;
    } else {
        pi->jump_active = 0;
        pi->jump_vy = 0.0f;
    }
    pi->next_tick = GetTickCount() + g_frame_durations[target][0];
    render_scaled_frame_to(pi->pet, 0, target * CELL_H, pi->dib_pixels, PET_W, PET_H);
    present_buffer(hwnd, pi->memdc);
}

static void spawn_pet(void)
{
    if (g_app.selected < 0 || g_app.selected >= g_app.pet_count) return;
    Pet *p = &g_app.pets[g_app.selected];
    if (!load_pet(p)) return;
    if (g_app.instance_count >= MAX_INSTANCES) return;

    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    int x = rand() % (sw - PET_W);
    int y = rand() % (sh - PET_H);

    int idx = -1;
    for (int i = 0; i < MAX_INSTANCES; i++) {
        if (!g_app.instances[i].alive) { idx = i; break; }
    }
    if (idx < 0) return;

    PetInst *pi = &g_app.instances[idx];
    memset(pi, 0, sizeof(PetInst));
    pi->pet = p;
    pi->x = x;
    pi->y = y;
    pi->alive = 1;

    HWND hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
        L"PetWindow", p->name, WS_POPUP, x, y, PET_W, PET_H, NULL, NULL, g_app.hinst, pi);
    if (!hwnd) { pi->alive = 0; return; }

    g_app.instance_count++;
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);

    /* render first frame immediately */
    ensure_buffer(pi);
    render_scaled_frame_to(p, 0, 0, pi->dib_pixels, PET_W, PET_H);
    present_buffer(hwnd, pi->memdc);

    /* keep focus on listbox so arrow keys / Enter keep working */
    SetFocus(g_app.listbox);
}

static void force_set_focus(HWND hwnd)
{
    /* AttachThreadInput workaround: allows SetFocus to succeed even when
       our thread is not the foreground thread (e.g. after clicking desktop). */
    HWND fg = GetForegroundWindow();
    if (fg) {
        DWORD fgThread = GetWindowThreadProcessId(fg, NULL);
        DWORD curThread = GetCurrentThreadId();
        if (fgThread != curThread) {
            AttachThreadInput(fgThread, curThread, TRUE);
            SetFocus(hwnd);
            AttachThreadInput(fgThread, curThread, FALSE);
        } else {
            SetFocus(hwnd);
        }
    } else {
        SetFocus(hwnd);
    }
}

static void destroy_all_pets(void)
{
    for (int i = 0; i < MAX_INSTANCES; i++) {
        if (g_app.instances[i].alive && g_app.instances[i].hwnd)
            DestroyWindow(g_app.instances[i].hwnd);
    }
}

/* ---------- window procs ---------- */
static LRESULT CALLBACK PetWndProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l)
{
    PetInst *pi = (PetInst *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW *cs = (CREATESTRUCTW *)l;
        pi = (PetInst *)cs->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)pi);
        pi->hwnd = hwnd;
        ensure_buffer(pi);
        pi->temp_anim = 0;
        pi->jump_active = 0;
        pi->jump_vy = 0.0f;
        pi->next_tick = GetTickCount() + g_frame_durations[0][0];
        SetTimer(hwnd, IDT_PET, 16, NULL);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        if (!pi) return 0;
        force_set_focus(hwnd);
        g_focused_pet = hwnd;
        SetCapture(hwnd);
        pi->dragging = 1;
        POINT pt;
        GetCursorPos(&pt);
        pi->drag_anchor_x = pt.x - pi->x;
        pi->drag_anchor_y = pt.y - pi->y;
        pi->last_drag_x = pi->x;
        pi->last_drag_tick = GetTickCount();
        pi->drag_speed = 0.0f;
        return 0;
    }
    case WM_MOUSEMOVE: {
        if (!pi || !pi->dragging) return 0;
        POINT pt;
        GetCursorPos(&pt);
        int new_x = pt.x - pi->drag_anchor_x;
        int new_y = pt.y - pi->drag_anchor_y;

        int dx = new_x - pi->last_drag_x;
        DWORD now = GetTickCount();
        DWORD dt = now - pi->last_drag_tick;
        if (dt == 0) dt = 1;

        pi->x = new_x;
        pi->y = new_y;
        pi->last_drag_x = new_x;
        pi->last_drag_tick = now;

        /* direction: only horizontal component matters */
        if (dx < 0) {
            if (pi->state != 2) { pi->state = 2; pi->frame = 0; pi->temp_anim = 0; pi->jump_active = 0; pi->jump_vy = 0.0f; }
        } else if (dx > 0) {
            if (pi->state != 1) { pi->state = 1; pi->frame = 0; pi->temp_anim = 0; pi->jump_active = 0; pi->jump_vy = 0.0f; }
        }
        pi->drag_speed = (float)(dx < 0 ? -dx : dx) / (float)dt;

        SetWindowPos(hwnd, NULL, new_x, new_y, 0, 0,
            SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);

        /* real-time frame advance + render on every mouse move */
        if (now >= pi->next_tick) {
            pi->frame++;
            if (pi->frame >= g_frame_counts[pi->state]) pi->frame = 0;
            int base = g_frame_durations[pi->state][pi->frame];
            float eff = pi->drag_speed;
            if (now - pi->last_drag_tick > 50) eff = 0.0f;
            float factor = 1.0f + eff * 3.0f;
            int adj = (int)(base / factor);
            if (adj < 15) adj = 15;
            pi->next_tick = now + adj;
        }
        int sx = pi->frame * CELL_W;
        int sy = pi->state * CELL_H;
        render_scaled_frame_to(pi->pet, sx, sy, pi->dib_pixels, PET_W, PET_H);
        present_buffer(hwnd, pi->memdc);
        return 0;
    }
    case WM_LBUTTONUP: {
        if (!pi || !pi->dragging) return 0;
        pi->dragging = 0;
        ReleaseCapture();
        pi->state = 0;
        pi->frame = 0;
        pi->temp_anim = 0;
        pi->jump_active = 0;
        pi->jump_vy = 0.0f;
        pi->drag_speed = 0.0f;
        pi->next_tick = GetTickCount() + g_frame_durations[0][0];
        /* render idle frame immediately */
        render_scaled_frame_to(pi->pet, 0, 0, pi->dib_pixels, PET_W, PET_H);
        present_buffer(hwnd, pi->memdc);
        return 0;
    }

    case WM_TIMER: {
        if (!pi || !pi->alive || !pi->pet || !pi->pet->pixels) return 0;
        DWORD now = GetTickCount();
        int needs_render = 0;

        /* jump physics: update every tick for smooth motion */
        if (pi->jump_active) {
            pi->jump_vy += 0.6f;
            pi->y += (int)pi->jump_vy;
            if (pi->y >= pi->jump_origin_y) {
                pi->y = pi->jump_origin_y;
                pi->jump_active = 0;
                pi->jump_vy = 0.0f;
            }
            SetWindowPos(hwnd, NULL, pi->x, pi->y, 0, 0,
                SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
            needs_render = 1;
        }

        if (now >= pi->next_tick) {
            pi->frame++;
            if (pi->frame >= g_frame_counts[pi->state]) {
                if (pi->temp_anim) {
                    pi->state = 0;
                    pi->temp_anim = 0;
                    pi->frame = 0;
                } else {
                    pi->frame = 0;
                }
            }

            int base = g_frame_durations[pi->state][pi->frame];
            int adj = base;
            if (pi->dragging) {
                float eff = pi->drag_speed;
                if (now - pi->last_drag_tick > 50) eff = 0.0f;
                float factor = 1.0f + eff * 3.0f;
                adj = (int)(base / factor);
                if (adj < 15) adj = 15;
            }
            pi->next_tick = now + adj;
            needs_render = 1;
        }

        if (needs_render) {
            int sx = pi->frame * CELL_W;
            int sy = pi->state * CELL_H;
            render_scaled_frame_to(pi->pet, sx, sy, pi->dib_pixels, PET_W, PET_H);
            present_buffer(hwnd, pi->memdc);
        }
        return 0;
    }
    case WM_DESTROY:
        if (pi) {
            pi->alive = 0;
            free_buffer(pi);
            g_app.instance_count--;
        }
        if (g_focused_pet == hwnd) g_focused_pet = NULL;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, w, l);
}

/* preview is drawn inside selector via AlphaBlend in WM_PAINT */

static void on_sel_change(int idx)
{
    if (idx < 0 || idx >= g_app.pet_count) return;
    g_app.selected = idx;
    load_pet(&g_app.pets[idx]);
    SetWindowTextW(g_app.desc_label, g_app.pets[idx].desc);
    g_app.preview_state = 7;
    g_app.preview_frame = 0;
    g_app.preview_next = GetTickCount() + g_frame_durations[7][0];
    /* clear preview buffer to avoid cross-pet ghosting */
    if (g_app.prev_pixels)
        memset(g_app.prev_pixels, 0, PREV_W * PREV_H * 4);
    /* force redraw */
    if (g_app.selector) {
        RECT rc = {190, 10, 190 + PREV_W, 10 + PREV_H};
        InvalidateRect(g_app.selector, &rc, TRUE);
    }
}

static LRESULT CALLBACK SelWndProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l)
{
    switch (msg) {
    case WM_CREATE: {
        g_app.listbox = CreateWindowExW(0, L"LISTBOX", NULL,
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY,
            10, 10, 160, 380, hwnd, (HMENU)IDC_LISTBOX, g_app.hinst, NULL);
        for (int i = 0; i < g_app.pet_count; i++)
            SendMessageW(g_app.listbox, LB_ADDSTRING, 0, (LPARAM)g_app.pets[i].id);

        g_app.desc_label = CreateWindowExW(0, L"STATIC", L"Select a pet",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            190, 230, 380, 160, hwnd, NULL, g_app.hinst, NULL);

        CreateWindowExW(0, L"STATIC", L"ENTER: spawn pet   ESC: exit",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            10, 400, 560, 20, hwnd, NULL, g_app.hinst, NULL);

        if (g_app.pet_count > 0) {
            SendMessageW(g_app.listbox, LB_SETCURSEL, 0, 0);
            on_sel_change(0);
            /* force first preview draw */
            {
                RECT rc = {190, 10, 190 + PREV_W, 10 + PREV_H};
                InvalidateRect(hwnd, &rc, TRUE);
            }
        }
        SetFocus(g_app.listbox);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(w) == IDC_LISTBOX && HIWORD(w) == LBN_SELCHANGE)
            on_sel_change((int)SendMessageW(g_app.listbox, LB_GETCURSEL, 0, 0));
        return 0;
    case WM_TIMER:
        update_preview();
        {
            RECT rc = {190, 10, 190 + PREV_W, 10 + PREV_H};
            InvalidateRect(hwnd, &rc, FALSE);
        }
        return 0;
    case WM_ERASEBKGND:
        /* 默认背景绘制交给 DefWindowProc，但预览区域我们自行管理 */
        return DefWindowProcW(hwnd, msg, w, l);
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        /* 先清空预览区背景，防止旧帧残留 */
        RECT rc_preview = {190, 10, 190 + PREV_W, 10 + PREV_H};
        FillRect(hdc, &rc_preview, (HBRUSH)(COLOR_BTNFACE + 1));
        if (g_app.selected >= 0 && g_app.selected < g_app.pet_count && g_app.prev_memdc) {
            BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
            AlphaBlend(hdc, 190, 10, PREV_W, PREV_H, g_app.prev_memdc, 0, 0, PREV_W, PREV_H, bf);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_DESTROY:
        destroy_all_pets();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, w, l);
}

/* ---------- entry ---------- */
int WINAPI WinMain(HINSTANCE hinst, HINSTANCE, LPSTR, int)
{
    srand((unsigned)time(NULL));
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    HRESULT hr = CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
        &IID_IWICImagingFactory, (void **)&g_app.wic);
    if (FAILED(hr)) { CoUninitialize(); return 1; }

    init_base_dir();
    scan_pets();

    if (g_app.pet_count == 0) {
        MessageBoxW(NULL, L"No pets found in my-pet/ directory", L"Error", MB_OK);
        IWICImagingFactory_Release(g_app.wic);
        CoUninitialize();
        return 1;
    }

    g_app.hinst = hinst;
    g_app.selected = -1;

    /* preview render buffer */
    {
        HDC screen = GetDC(NULL);
        BITMAPINFO bmi = {0};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = PREV_W;
        bmi.bmiHeader.biHeight = -PREV_H;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        g_app.prev_dib = CreateDIBSection(screen, &bmi, DIB_RGB_COLORS, (void **)&g_app.prev_pixels, NULL, 0);
        g_app.prev_memdc = CreateCompatibleDC(screen);
        SelectObject(g_app.prev_memdc, g_app.prev_dib);
        ReleaseDC(NULL, screen);
    }

    WNDCLASSW wc_sel = {0};
    wc_sel.lpfnWndProc = SelWndProc;
    wc_sel.hInstance = hinst;
    wc_sel.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc_sel.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc_sel.lpszClassName = L"PetSelector";
    RegisterClassW(&wc_sel);

    WNDCLASSW wc_pet = {0};
    wc_pet.lpfnWndProc = PetWndProc;
    wc_pet.hInstance = hinst;
    wc_pet.lpszClassName = L"PetWindow";
    RegisterClassW(&wc_pet);

    g_app.selector = CreateWindowExW(0, L"PetSelector", L"Codex Pet",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 620, 520, NULL, NULL, hinst, NULL);

    ShowWindow(g_app.selector, SW_SHOW);
    UpdateWindow(g_app.selector);
    SetTimer(g_app.selector, IDT_PREVIEW, 30, NULL);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        if (msg.message == WM_KEYDOWN || msg.message == WM_SYSKEYDOWN) {
            if (msg.wParam == VK_RETURN) {
                spawn_pet();
                continue;
            }
            if (msg.wParam == VK_ESCAPE) {
                HWND focus = GetFocus();
                if (focus) {
                    wchar_t cn[64];
                    GetClassNameW(focus, cn, 64);
                    if (wcscmp(cn, L"PetWindow") == 0) {
                        DestroyWindow(focus);
                        continue;
                    }
                }
                destroy_all_pets();
                PostQuitMessage(0);
                break;
            }
            int target = -1;
            switch (msg.wParam) {
            case VK_SPACE: target = 4; break; /* jumping */
            case 'W':      target = 3; break; /* waving  */
            case 'E':      target = 5; break; /* failed  */
            case 'Q':      target = 6; break; /* waiting */
            case 'R':      target = 8; break; /* review  */
            }
            if (target >= 0 && g_focused_pet) {
                pet_trigger_anim(g_focused_pet, target);
                continue;
            }
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    destroy_all_pets();
    for (int i = 0; i < g_app.pet_count; i++) free_pet(&g_app.pets[i]);
    if (g_app.prev_memdc) { DeleteDC(g_app.prev_memdc); g_app.prev_memdc = NULL; }
    if (g_app.prev_dib) { DeleteObject(g_app.prev_dib); g_app.prev_dib = NULL; }
    IWICImagingFactory_Release(g_app.wic);
    CoUninitialize();
    return 0;
}
