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
#include <math.h>

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

#define AI_TIMEOUT      10000
#define AI_MIN_ACTION   3000
#define AI_MAX_ACTION   10000

#define TRAJ_LINE       0
#define TRAJ_RECT       1
#define TRAJ_TRI        2
#define TRAJ_POLY       3
#define TRAJ_CIRCLE     4
#define TRAJ_ELLIPSE    5
#define TRAJ_FIGURE8    6
#define TRAJ_ARC        7
#define TRAJ_ZIGZAG     8
#define TRAJ_CYCLE      9

#define PI 3.14159265f

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
    int prev_x;
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
    int jump_count;
    int jump_origin_y;
    float jump_vy;
    /* directional run movement */
    int move_dx;
    int move_dy;
    float move_subx;
    float move_suby;
    /* continuous running loop */
    int run_loop_mode;
    /* auto-run direction for a full running cycle (1=right, -1=left, 0=none) */
    int run_dir_x;
    /* AI state */
    int ai_active;
    DWORD ai_last_interaction;
    DWORD ai_next_action;
    int ai_next_state;      /* pending transition target, -1 = none */
    int ai_traj_type;
    float ai_traj_t;
    float ai_traj_speed;
    float ai_subx;
    float ai_suby;
    int ai_p1;
    int ai_p2;
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
    /* Uniform 120ms for all preview frames ensures smooth looping;
       actual animation uses variable durations, but the small preview
       looks better with consistent timing. */
    g_app.preview_next = now + 120;

    int sx = g_app.preview_frame * CELL_W;
    int sy = 7 * CELL_H;
    render_scaled_frame_to(p, sx, sy, g_app.prev_pixels, PREV_W, PREV_H);
}

/* ---------- pet instance management ---------- */
static void pet_trigger_anim(HWND hwnd, int target)
{
    PetInst *pi = (PetInst *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (!pi || !pi->alive || !pi->pet || !pi->pet->pixels) return;
    if (target == 4 && pi->jump_active && pi->jump_count >= 2) {
        /* max double jump reached, ignore additional presses */
        return;
    }
    if (pi->jump_active && target != 4 && target != 1 && target != 2) {
        /* Non-running temp anims land immediately */
        pi->y = pi->jump_origin_y;
        SetWindowPos(hwnd, NULL, pi->x, pi->y, 0, 0,
            SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    pi->state = target;
    pi->frame = 0;
    pi->temp_anim = 1;
    pi->run_loop_mode = (target == 1 || target == 2) ? 1 : 0;
    if (target == 1 || target == 2) {
        pi->run_dir_x = (target == 1) ? 1 : -1;
    } else {
        pi->run_dir_x = 0;
    }
    if (target == 4) {
        if (pi->jump_active) {
            /* double jump boost */
            pi->jump_count = 2;
            pi->jump_vy = -12.3f;
        } else {
            /* first jump */
            pi->jump_active = 1;
            pi->jump_count = 1;
            pi->jump_origin_y = pi->y;
            pi->jump_vy = -10.1f;
        }
    } else {
        if (pi->jump_active) {
            if (target == 1 || target == 2) {
                /* Left/right interrupts jump: stop falling, keep current y */
                pi->jump_active = 0;
                pi->jump_count = 0;
                pi->jump_vy = 0.0f;
            } else {
                pi->y = pi->jump_origin_y;
                SetWindowPos(hwnd, NULL, pi->x, pi->y, 0, 0,
                    SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
            }
        }
        pi->jump_active = 0;
        pi->jump_count = 0;
        pi->jump_vy = 0.0f;
        pi->move_dx = 0;
        pi->move_dy = 0;
    }
    pi->next_tick = GetTickCount() + g_frame_durations[target][0];
    render_scaled_frame_to(pi->pet, 0, target * CELL_H, pi->dib_pixels, PET_W, PET_H);
    present_buffer(hwnd, pi->memdc);
}

static void ai_trajectory_point(PetInst *pi, float t, float *nx, float *ny)
{
    *nx = 0.5f; *ny = 0.5f;
    switch (pi->ai_traj_type) {
    case TRAJ_LINE:
        *nx = t;
        *ny = 0.3f + t * 0.4f;
        break;
    case TRAJ_RECT:
        {
            float lt = t * 4.0f;
            int side = (int)lt;
            float f = lt - side;
            if (side == 0) { *nx = f; *ny = 0.0f; }
            else if (side == 1) { *nx = 1.0f; *ny = f; }
            else if (side == 2) { *nx = 1.0f - f; *ny = 1.0f; }
            else { *nx = 0.0f; *ny = 1.0f - f; }
        }
        break;
    case TRAJ_TRI:
        {
            float lt = t * 3.0f;
            int side = (int)lt;
            float f = lt - side;
            if (side == 0) { *nx = 0.5f + f * 0.5f; *ny = f; }
            else if (side == 1) { *nx = 1.0f - f * 0.5f; *ny = 1.0f - f * 0.5f; }
            else { *nx = f * 0.5f; *ny = 0.5f + f * 0.5f; }
        }
        break;
    case TRAJ_POLY:
        {
            int n = 3 + (pi->ai_p1 % 5);
            float lt = t * n;
            int side = (int)lt;
            float f = lt - side;
            float a1 = side * 2.0f * PI / n;
            float a2 = (side + 1) * 2.0f * PI / n;
            float vx1 = 0.5f + 0.5f * cosf(a1);
            float vy1 = 0.5f + 0.5f * sinf(a1);
            float vx2 = 0.5f + 0.5f * cosf(a2);
            float vy2 = 0.5f + 0.5f * sinf(a2);
            *nx = vx1 + (vx2 - vx1) * f;
            *ny = vy1 + (vy2 - vy1) * f;
        }
        break;
    case TRAJ_CIRCLE:
        {
            float a = t * 2.0f * PI;
            *nx = 0.5f + 0.5f * cosf(a);
            *ny = 0.5f + 0.5f * sinf(a);
        }
        break;
    case TRAJ_ELLIPSE:
        {
            float a = t * 2.0f * PI;
            *nx = 0.5f + 0.5f * cosf(a);
            *ny = 0.5f + 0.3f * sinf(a);
        }
        break;
    case TRAJ_FIGURE8:
        {
            float a = t * 2.0f * PI;
            *nx = 0.5f + 0.4f * sinf(a);
            *ny = 0.5f + 0.4f * sinf(2.0f * a);
        }
        break;
    case TRAJ_ARC:
        {
            float a = t * PI;
            *nx = 0.5f + 0.5f * cosf(a);
            *ny = 0.5f + 0.5f * sinf(a);
        }
        break;
    case TRAJ_ZIGZAG:
        {
            int seg = (int)(t * 8.0f);
            float f = t * 8.0f - seg;
            *nx = (seg + f) / 8.0f;
            *ny = (seg % 2 == 0) ? f : 1.0f - f;
        }
        break;
    case TRAJ_CYCLE:
        {
            float theta = t * 4.0f * PI;
            *nx = (theta - sinf(theta)) / (4.0f * PI);
            *ny = (1.0f - cosf(theta)) / 2.0f;
        }
        break;
    }
}

static void ai_update_pos(PetInst *pi)
{
    /* Free movement is only allowed in running-left/right states */
    if (pi->state != 1 && pi->state != 2) return;

    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    int margin = 20;
    float scale_x = (float)(sw - PET_W - 2 * margin);
    float scale_y = (float)(sh - PET_H - 2 * margin);

    float t = pi->ai_traj_t;
    float nx, ny;
    ai_trajectory_point(pi, t, &nx, &ny);

    /* Compute local tangent direction in screen space */
    float dt = 0.0001f;
    float nx2, ny2;
    ai_trajectory_point(pi, t + dt, &nx2, &ny2);
    float dir_x = (nx2 - nx) * scale_x;
    float dir_y = (ny2 - ny) * scale_y;
    float dir_len = sqrtf(dir_x * dir_x + dir_y * dir_y);
    if (dir_len < 0.001f) dir_len = 0.001f;

    /* Advance t proportionally so arc-length speed is ~1 px/tick */
    pi->ai_traj_t += dt * 1.0f / dir_len;
    if (pi->ai_traj_t > 1.0f) pi->ai_traj_t -= 1.0f;

    /* Move exactly 1 pixel along the trajectory tangent.
       Sub-pixel accumulators ensure smooth motion at any angle. */
    pi->ai_subx += dir_x / dir_len;
    pi->ai_suby += dir_y / dir_len;

    int ix = (int)pi->ai_subx;
    int iy = (int)pi->ai_suby;
    pi->ai_subx -= (float)ix;
    pi->ai_suby -= (float)iy;

    pi->x += ix;
    pi->y += iy;

    /* Boundary clamp */
    if (pi->x < 0) pi->x = 0;
    if (pi->x > sw - PET_W) pi->x = sw - PET_W;
    if (pi->y < 0) pi->y = 0;
    if (pi->y > sh - PET_H) pi->y = sh - PET_H;

    /* If the boundary blocked movement, clear sub-pixel accumulators
       so the pet doesn't jitter from repeated outward pushes. */
    if ((pi->x == 0 && ix < 0) || (pi->x == sw - PET_W && ix > 0) ||
        (pi->y == 0 && iy < 0) || (pi->y == sh - PET_H && iy > 0)) {
        pi->ai_subx = 0.0f;
        pi->ai_suby = 0.0f;
    }

    SetWindowPos(pi->hwnd, NULL, pi->x, pi->y, 0, 0,
        SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
}

/* Weighted state transition based on current state and pet personality.
   activity: 0=lazy, 1=normal, 2=hyper  |  run_bias: 0=left-biased, 1=balanced, 2=right-biased */
static int ai_weighted_state(PetInst *pi)
{
    int activity = (pi->ai_p1 % 3);      /* 0,1,2 */
    int run_bias = (pi->ai_p2 % 3);      /* 0,1,2 */
    int r = rand() % 100;

    switch (pi->state) {
    case 0: /* idle */
        if (r < 30 - activity * 5) return 0;
        if (r < 50 + activity * 5) return (run_bias == 0) ? 2 : 1;
        if (r < 70 + activity * 5) return (run_bias == 0) ? 1 : 2;
        if (r < 82) return 3;
        if (r < 88) return 6;
        if (r < 93) return 4;
        if (r < 97) return 8;
        return 5;
    case 1: /* running-right */
        if (r < 20) return 0;
        if (r < 50 + activity * 5) return 1;
        if (r < 65 + activity * 5) return 2;
        if (r < 80) return 8;
        if (r < 88) return 4;
        if (r < 93) return 3;
        if (r < 97) return 6;
        return 5;
    case 2: /* running-left */
        if (r < 20) return 0;
        if (r < 50 + activity * 5) return 2;
        if (r < 65 + activity * 5) return 1;
        if (r < 80) return 8;
        if (r < 88) return 4;
        if (r < 93) return 3;
        if (r < 97) return 6;
        return 5;
    case 3: /* waving */
        if (r < 55) return 0;
        if (r < 70) return 1;
        if (r < 85) return 2;
        if (r < 90) return 8;
        if (r < 94) return 6;
        if (r < 97) return 4;
        return 5;
    case 4: /* jumping */
        if (r < 45) return 0;
        if (r < 65) return 1;
        if (r < 85) return 2;
        if (r < 90) return 8;
        if (r < 95) return 3;
        if (r < 98) return 6;
        return 5;
    case 5: /* failed */
        if (r < 75) return 0;
        if (r < 88) return 1;
        if (r < 98) return 2;
        return 8;
    case 6: /* waiting */
        if (r < 45) return 0;
        if (r < 62) return 1;
        if (r < 79) return 2;
        if (r < 88) return 3;
        if (r < 94) return 8;
        if (r < 97) return 4;
        return 5;
    case 8: /* review */
        if (r < 35) return 0;
        if (r < 55) return 1;
        if (r < 75) return 2;
        if (r < 85) return 3;
        if (r < 92) return 8;
        if (r < 97) return 6;
        return 4;
    }
    return 0;
}

static int ai_state_duration(int state, int activity)
{
    int base_min, base_max;
    switch (state) {
    case 0:  base_min = 3000; base_max = 7000; break; /* idle   */
    case 1:
    case 2:  base_min = 4000; base_max = 9000; break; /* running */
    case 3:  base_min = 2000; base_max = 4000; break; /* waving  */
    case 4:  base_min = 1500; base_max = 3000; break; /* jumping */
    case 5:  base_min = 2000; base_max = 4000; break; /* failed  */
    case 6:  base_min = 2000; base_max = 5000; break; /* waiting */
    case 8:  base_min = 2000; base_max = 4000; break; /* review  */
    default: base_min = 2000; base_max = 5000; break;
    }
    /* Hyper pets act faster/shorter; lazy pets linger longer */
    int adj = (activity - 1) * 800;
    base_min -= adj;
    base_max -= adj;
    if (base_min < 800)  base_min = 800;
    if (base_max < base_min + 500) base_max = base_min + 500;
    return base_min + rand() % (base_max - base_min);
}

static void ai_apply_state(PetInst *pi, int state, DWORD now)
{
    pi->state = state;
    pi->frame = 0;
    pi->temp_anim = (state >= 3 || state == 1 || state == 2) ? 1 : 0;
    pi->run_loop_mode = (state == 1 || state == 2) ? 1 : 0;
    pi->run_dir_x = (state == 1) ? 1 : (state == 2) ? -1 : 0;
    pi->next_tick = now + g_frame_durations[state][0];

    if (state == 4) {
        pi->jump_active = 1;
        pi->jump_count = 1;
        pi->jump_origin_y = pi->y;
        pi->jump_vy = -10.1f;
    }

    /* Reset sub-pixel accumulators on state change to prevent drift */
    pi->ai_subx = 0.0f;
    pi->ai_suby = 0.0f;

    render_scaled_frame_to(pi->pet, 0, state * CELL_H, pi->dib_pixels, PET_W, PET_H);
    present_buffer(pi->hwnd, pi->memdc);
}

static void ai_pick_action(PetInst *pi, DWORD now)
{
    int next = ai_weighted_state(pi);
    int activity = (pi->ai_p1 % 3);

    /* Trajectory: 80% keep current, 20% switch */
    if ((rand() % 100) < 20) {
        pi->ai_traj_type = rand() % 10;
        pi->ai_traj_t = (float)rand() / (float)RAND_MAX;
    }
    /* If switching from non-running to running, seed t from current position
       to minimise position jump (approximate by using x coordinate ratio) */
    if ((pi->state != 1 && pi->state != 2) && (next == 1 || next == 2)) {
        int sw = GetSystemMetrics(SM_CXSCREEN);
        int margin = 20;
        float scale_x = (float)(sw - PET_W - 2 * margin);
        if (scale_x > 0.0f) {
            pi->ai_traj_t = (float)(pi->x - margin) / scale_x;
            if (pi->ai_traj_t < 0.0f) pi->ai_traj_t = 0.0f;
            if (pi->ai_traj_t > 1.0f) pi->ai_traj_t = 1.0f;
        }
    }

    pi->ai_next_action = now + ai_state_duration(next, activity);

    /* If currently running, enter graceful stop and defer state change
       until the running cycle naturally finishes. */
    if (pi->state == 1 || pi->state == 2) {
        pi->run_loop_mode = 0;
        pi->ai_next_state = next;
    } else {
        pi->ai_next_state = -1;
        ai_apply_state(pi, next, now);
    }
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
    pi->prev_x = x;
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
        pi->jump_count = 0;
        pi->jump_vy = 0.0f;
        pi->move_dx = 0;
        pi->move_dy = 0;
        pi->move_subx = 0.0f;
        pi->move_suby = 0.0f;
        pi->run_loop_mode = 0;
        pi->run_dir_x = 0;
        pi->ai_active = 0;
        pi->ai_next_state = -1;
        pi->ai_subx = 0.0f;
        pi->ai_suby = 0.0f;
        pi->ai_last_interaction = GetTickCount();
        pi->prev_x = pi->x;
        pi->next_tick = GetTickCount() + g_frame_durations[0][0];
        SetTimer(hwnd, IDT_PET, 20, NULL);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        if (!pi) return 0;
        force_set_focus(hwnd);
        g_focused_pet = hwnd;
        SetCapture(hwnd);
        pi->dragging = 1;
        pi->run_loop_mode = 0;
        pi->ai_active = 0;
        pi->ai_next_state = -1;
        pi->ai_subx = 0.0f;
        pi->ai_suby = 0.0f;
        pi->ai_last_interaction = GetTickCount();
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
        if (pi->y < 0) pi->y = 0;
        pi->last_drag_x = new_x;
        pi->last_drag_tick = now;

        /* direction: only horizontal component matters */
        if (dx < 0) {
            if (pi->state != 2) { pi->state = 2; pi->frame = 0; pi->temp_anim = 0; pi->run_loop_mode = 0; pi->run_dir_x = 0; pi->jump_active = 0; pi->jump_count = 0; pi->jump_vy = 0.0f; pi->move_dx = 0; pi->move_dy = 0; }
        } else if (dx > 0) {
            if (pi->state != 1) { pi->state = 1; pi->frame = 0; pi->temp_anim = 0; pi->run_loop_mode = 0; pi->run_dir_x = 0; pi->jump_active = 0; pi->jump_count = 0; pi->jump_vy = 0.0f; pi->move_dx = 0; pi->move_dy = 0; }
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
        pi->run_loop_mode = 0;
        pi->run_dir_x = 0;
        pi->jump_active = 0;
        pi->jump_count = 0;
        pi->jump_vy = 0.0f;
        pi->move_dx = 0;
        pi->move_dy = 0;
        pi->move_subx = 0.0f;
        pi->move_suby = 0.0f;
        pi->drag_speed = 0.0f;
        pi->ai_subx = 0.0f;
        pi->ai_suby = 0.0f;
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
            if (pi->y < 0) pi->y = 0;
            if (pi->y >= pi->jump_origin_y) {
                pi->y = pi->jump_origin_y;
                pi->jump_active = 0;
                pi->jump_count = 0;
                pi->jump_vy = 0.0f;
            }
            SetWindowPos(hwnd, NULL, pi->x, pi->y, 0, 0,
                SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
            needs_render = 1;
        }

        /* temp running movement (horizontal or vertical) */
        if (pi->temp_anim && (pi->state == 1 || pi->state == 2)) {
            int moved = 0;
            int dx = pi->move_dx;
            int dy = pi->move_dy;
            /* auto-run for a full cycle even after key release */
            if (dx == 0 && dy == 0 && pi->run_dir_x != 0) {
                dx = pi->run_dir_x * 1;
            }
            /* Enforce uniform 1 px/tick speed even on diagonals */
            if (dx != 0 && dy != 0) {
                float len = sqrtf((float)(dx * dx) + (float)(dy * dy));
                float ratio = 1.0f / len;
                pi->move_subx += dx * ratio;
                pi->move_suby += dy * ratio;
                dx = (int)pi->move_subx;
                dy = (int)pi->move_suby;
                pi->move_subx -= dx;
                pi->move_suby -= dy;
            } else {
                pi->move_subx = 0.0f;
                pi->move_suby = 0.0f;
            }
            if (dx != 0) {
                pi->x += dx;
                int sw = GetSystemMetrics(SM_CXSCREEN);
                if (pi->x < 0) pi->x = 0;
                if (pi->x > sw - PET_W) pi->x = sw - PET_W;
                moved = 1;
            }
            if (dy != 0) {
                pi->y += dy;
                int sh = GetSystemMetrics(SM_CYSCREEN);
                if (pi->y < 0) pi->y = 0;
                if (pi->y > sh - PET_H) pi->y = sh - PET_H;
                moved = 1;
            }
            if (moved) {
                SetWindowPos(hwnd, NULL, pi->x, pi->y, 0, 0,
                    SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
                needs_render = 1;
            }
        }

        /* AI: activate after timeout of no interaction */
        if (!pi->dragging && !pi->ai_active) {
            if (now - pi->ai_last_interaction > AI_TIMEOUT) {
                pi->ai_active = 1;
                ai_pick_action(pi, now);
            }
        } else if (pi->ai_active) {
            if (now >= pi->ai_next_action) {
                ai_pick_action(pi, now);
                needs_render = 1;
            }
            ai_update_pos(pi);
            needs_render = 1;
        }

        if (now >= pi->next_tick) {
            pi->frame++;
            if (pi->frame >= g_frame_counts[pi->state]) {
                if (pi->temp_anim) {
                    if (pi->state == 1 || pi->state == 2) {
                        if (pi->run_loop_mode) {
                            /* continuous loop: uniform timing, keep looping */
                            pi->frame = 0;
                        } else {
                            /* graceful stop: finish cycle, then return to idle */
                            pi->state = 0;
                            pi->temp_anim = 0;
                            pi->frame = 0;
                            pi->run_dir_x = 0;
                            pi->move_dx = 0;
                            pi->move_dy = 0;
                            if (pi->ai_active && pi->ai_next_state >= 0) {
                                ai_apply_state(pi, pi->ai_next_state, now);
                                pi->ai_next_state = -1;
                            }
                        }
                    } else {
                        pi->state = 0;
                        pi->temp_anim = 0;
                        pi->frame = 0;
                        pi->run_dir_x = 0;
                        pi->move_dx = 0;
                        pi->move_dy = 0;
                        if (pi->ai_active && pi->ai_next_state >= 0) {
                            ai_apply_state(pi, pi->ai_next_state, now);
                            pi->ai_next_state = -1;
                        }
                    }
                } else {
                    pi->frame = 0;
                }
            }

            int base;
            if ((pi->state == 1 || pi->state == 2) && pi->run_loop_mode) {
                base = 120; /* uniform speed during continuous running loop */
            } else {
                base = g_frame_durations[pi->state][pi->frame];
            }
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

        /* real-time running direction switch based on x delta */
        if ((pi->state == 1 || pi->state == 2) && pi->prev_x != pi->x) {
            if (pi->x > pi->prev_x && pi->state != 1) {
                pi->state = 1; pi->frame = 0;
            } else if (pi->x < pi->prev_x && pi->state != 2) {
                pi->state = 2; pi->frame = 0;
            }
        }
        pi->prev_x = pi->x;

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
    g_app.preview_next = GetTickCount() + 120;
    /* render first frame immediately to avoid blank period */
    if (g_app.prev_pixels) {
        Pet *p = &g_app.pets[idx];
        if (p && p->pixels) {
            render_scaled_frame_to(p, 0, 7 * CELL_H, g_app.prev_pixels, PREV_W, PREV_H);
        }
    }
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
        if (msg.message == WM_KEYUP) {
            if ((msg.wParam == VK_LEFT || msg.wParam == VK_RIGHT ||
                 msg.wParam == VK_UP   || msg.wParam == VK_DOWN) && g_focused_pet) {
                PetInst *pi = (PetInst *)GetWindowLongPtrW(g_focused_pet, GWLP_USERDATA);
                if (pi && pi->alive && pi->temp_anim && (pi->state == 1 || pi->state == 2)) {
                    /* graceful stop: let running animation finish with original durations */
                    pi->run_loop_mode = 0;
                    pi->move_dx = 0;
                    pi->move_dy = 0;
                    continue;
                }
            }
        }
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
            case VK_LEFT:  target = 2; break; /* running-left  */
            case VK_RIGHT: target = 1; break; /* running-right */
            case VK_UP:
            case VK_DOWN: {
                if (g_focused_pet) {
                    PetInst *pi = (PetInst *)GetWindowLongPtrW(g_focused_pet, GWLP_USERDATA);
                    if (pi) {
                        int sw = GetSystemMetrics(SM_CXSCREEN);
                        target = (pi->x <= sw / 2) ? 1 : 2;
                    }
                }
                break;
            }
            }
            if (target >= 0 && g_focused_pet) {
                /* Arrow keys only control pet when focus is actually on a pet window */
                if (msg.wParam == VK_LEFT || msg.wParam == VK_RIGHT ||
                    msg.wParam == VK_UP   || msg.wParam == VK_DOWN) {
                    HWND focus = GetFocus();
                    int on_pet = 0;
                    if (focus) {
                        wchar_t cn[64];
                        GetClassNameW(focus, cn, 64);
                        on_pet = (wcscmp(cn, L"PetWindow") == 0);
                    }
                    if (!on_pet) {
                        TranslateMessage(&msg);
                        DispatchMessage(&msg);
                        continue;
                    }
                }
                PetInst *pi = (PetInst *)GetWindowLongPtrW(g_focused_pet, GWLP_USERDATA);
                if (pi) {
                    pi->ai_active = 0;
                    pi->ai_next_state = -1;
                    pi->ai_subx = 0.0f;
                    pi->ai_suby = 0.0f;
                    pi->move_subx = 0.0f;
                    pi->move_suby = 0.0f;
                    pi->ai_last_interaction = GetTickCount();
                }
                /* During jumping, block all state-switching keys except left/right arrows and space (double-jump) */
                if (pi && pi->jump_active) {
                    if (msg.wParam != VK_LEFT && msg.wParam != VK_RIGHT && msg.wParam != VK_SPACE) {
                        continue;
                    }
                }
                /* Jumping (target==4) always calls through to allow double-jump.
                   Other temp anims skip re-trigger if already playing same state. */
                if (!pi || !pi->alive || !pi->temp_anim || pi->state != target || target == 4) {
                    pet_trigger_anim(g_focused_pet, target);
                }
                /* Set movement direction for running-left/right */
                if (target == 1 || target == 2) {
                    pi = (PetInst *)GetWindowLongPtrW(g_focused_pet, GWLP_USERDATA);
                    if (pi) {
                        if (msg.wParam == VK_LEFT || msg.wParam == VK_RIGHT) {
                            pi->move_dx = (target == 1) ? 1 : -1;
                            pi->move_dy = 0;
                        } else if (msg.wParam == VK_UP || msg.wParam == VK_DOWN) {
                            pi->move_dx = 0;
                            pi->move_dy = (msg.wParam == VK_UP) ? -1 : 1;
                        }
                    }
                }
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
