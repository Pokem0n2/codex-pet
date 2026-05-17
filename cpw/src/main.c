#define COBJMACROS
#include <windows.h>
#include <windowsx.h>
#include <wincodec.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define APP_NAME L"cpw_rebuilt"
#define PET_WINDOW_CLASS L"CpwRebuiltPetWindow"
#define SELECTOR_WINDOW_CLASS L"CpwRebuiltSelectorWindow"
#define HOTKEY_EXIT_ID 1001
#define SELECTOR_PREVIEW_TIMER_ID 2001

#define FRAME_W 192
#define FRAME_H 208
#define STATE_COUNT 9
#define PET_ID_CAP 64
#define PET_NAME_CAP 128
#define PET_DESC_CAP 512
#define MAX_PETS 512

typedef enum PetState {
    PET_STATE_IDLE = 0,
    PET_STATE_RUN_RIGHT = 1,
    PET_STATE_RUN_LEFT = 2,
    PET_STATE_WAVING = 3,
    PET_STATE_JUMPING = 4,
    PET_STATE_FAILED = 5,
    PET_STATE_WAITING = 6,
    PET_STATE_RUNNING = 7,
    PET_STATE_REVIEW = 8
} PetState;

static const int k_frame_counts[STATE_COUNT] = {6, 8, 8, 4, 5, 8, 6, 6, 6};
static const int k_frame_durations_ms[STATE_COUNT] = {183, 133, 133, 175, 168, 153, 168, 137, 172};

typedef struct PetMetadata {
    wchar_t id[PET_ID_CAP];
    wchar_t display_name[PET_NAME_CAP];
    wchar_t description[PET_DESC_CAP];
    wchar_t sprite_path[MAX_PATH];
} PetMetadata;

typedef struct PetList {
    PetMetadata *items;
    size_t count;
} PetList;

typedef struct SpriteSheet {
    int width;
    int height;
    uint32_t *pixels;
} SpriteSheet;

typedef struct PetWindow {
    HWND hwnd;
    HDC mem_dc;
    HBITMAP dib;
    void *dib_pixels;
    POINT drag_cursor_start;
    POINT drag_window_start;
    int active;
    int dragging;
    int current_pet_index;
    int current_state;
    int current_frame;
    int idle_loops;
    int last_drag_delta_x;
} PetWindow;

typedef struct App {
    HINSTANCE instance;
    IWICImagingFactory *wic;
    wchar_t pet_root[MAX_PATH];
    PetList pets;
    SpriteSheet sheet;
    PetWindow pet_window;
    int hotkey_registered;
} App;

typedef struct SelectorState {
    App *app;
    HWND hwnd;
    HWND listbox;
    HWND start_button;
    HWND name_label;
    HWND description_edit;
    RECT preview_rect;
    SpriteSheet preview_sheet;
    HDC preview_dc;
    HBITMAP preview_dib;
    void *preview_pixels;
    int preview_pet_index;
    int preview_state;
    int preview_frame;
    int preview_idle_loops;
    int result;
    int done;
} SelectorState;

static App g_app;

static void safe_free(void *ptr) {
    free(ptr);
}

static int wide_from_utf8(const char *src, wchar_t *dst, size_t dst_cap) {
    int written;
    if (!src || !dst || dst_cap == 0) {
        return 0;
    }
    written = MultiByteToWideChar(CP_UTF8, 0, src, -1, dst, (int) dst_cap);
    if (written <= 0) {
        dst[0] = L'\0';
        return 0;
    }
    dst[dst_cap - 1] = L'\0';
    return 1;
}

static int build_path2(wchar_t *out, size_t out_cap, const wchar_t *left, const wchar_t *right) {
    int count;
    if (!out || out_cap == 0 || !left || !right) {
        return 0;
    }
    count = _snwprintf(out, out_cap, L"%ls\\%ls", left, right);
    if (count < 0 || (size_t) count >= out_cap) {
        out[out_cap - 1] = L'\0';
        return 0;
    }
    return 1;
}

static int build_path3(wchar_t *out, size_t out_cap, const wchar_t *a, const wchar_t *b, const wchar_t *c) {
    wchar_t temp[MAX_PATH];
    return build_path2(temp, MAX_PATH, a, b) && build_path2(out, out_cap, temp, c);
}

static int file_exists_w(const wchar_t *path) {
    DWORD attrs = GetFileAttributesW(path);
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

static int dir_exists_w(const wchar_t *path) {
    DWORD attrs = GetFileAttributesW(path);
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

static int read_file_utf8(const wchar_t *path, char **buffer_out, size_t *size_out) {
    FILE *fp = NULL;
    char *buffer = NULL;
    long size = 0;
    size_t read_bytes = 0;

    *buffer_out = NULL;
    if (size_out) {
        *size_out = 0;
    }

    fp = _wfopen(path, L"rb");
    if (!fp) {
        return 0;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return 0;
    }
    size = ftell(fp);
    if (size < 0 || fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return 0;
    }

    buffer = (char *) malloc((size_t) size + 1);
    if (!buffer) {
        fclose(fp);
        return 0;
    }

    read_bytes = fread(buffer, 1, (size_t) size, fp);
    fclose(fp);
    if (read_bytes != (size_t) size) {
        safe_free(buffer);
        return 0;
    }

    buffer[size] = '\0';
    *buffer_out = buffer;
    if (size_out) {
        *size_out = (size_t) size;
    }
    return 1;
}

static const char *skip_json_ws(const char *p) {
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
        ++p;
    }
    return p;
}

static int json_extract_string(const char *json, const char *key, char *out, size_t out_cap) {
    char needle[64];
    const char *match;
    const char *cursor;
    size_t len = 0;

    if (!json || !key || !out || out_cap == 0) {
        return 0;
    }

    _snprintf(needle, sizeof(needle), "\"%s\"", key);
    match = strstr(json, needle);
    if (!match) {
        out[0] = '\0';
        return 0;
    }

    cursor = skip_json_ws(match + strlen(needle));
    if (*cursor != ':') {
        out[0] = '\0';
        return 0;
    }
    cursor = skip_json_ws(cursor + 1);
    if (*cursor != '"') {
        out[0] = '\0';
        return 0;
    }

    ++cursor;
    while (*cursor && len + 1 < out_cap) {
        if (*cursor == '\\') {
            ++cursor;
            if (*cursor == '\0') {
                break;
            }
            switch (*cursor) {
                case '"': out[len++] = '"'; break;
                case '\\': out[len++] = '\\'; break;
                case '/': out[len++] = '/'; break;
                case 'b': out[len++] = '\b'; break;
                case 'f': out[len++] = '\f'; break;
                case 'n': out[len++] = '\n'; break;
                case 'r': out[len++] = '\r'; break;
                case 't': out[len++] = '\t'; break;
                default: out[len++] = *cursor; break;
            }
            ++cursor;
            continue;
        }
        if (*cursor == '"') {
            break;
        }
        out[len++] = *cursor++;
    }

    out[len] = '\0';
    return len > 0;
}

static int resolve_pet_root(App *app) {
    wchar_t env_path[MAX_PATH];
    wchar_t cwd[MAX_PATH];
    wchar_t exe_path[MAX_PATH];
    wchar_t parent_dir[MAX_PATH];
    wchar_t *last_slash;

    if (GetEnvironmentVariableW(L"DIGIT_PET_PATH", env_path, MAX_PATH) > 0 && dir_exists_w(env_path)) {
        wcsncpy(app->pet_root, env_path, MAX_PATH - 1);
        app->pet_root[MAX_PATH - 1] = L'\0';
        return 1;
    }

    if (GetCurrentDirectoryW(MAX_PATH, cwd) > 0 && build_path2(app->pet_root, MAX_PATH, cwd, L"my") && dir_exists_w(app->pet_root)) {
        return 1;
    }

    if (GetModuleFileNameW(NULL, exe_path, MAX_PATH) > 0) {
        last_slash = wcsrchr(exe_path, L'\\');
        if (last_slash) {
            *last_slash = L'\0';
            if (build_path2(app->pet_root, MAX_PATH, exe_path, L"my") && dir_exists_w(app->pet_root)) {
                return 1;
            }
            wcsncpy(parent_dir, exe_path, MAX_PATH - 1);
            parent_dir[MAX_PATH - 1] = L'\0';
            last_slash = wcsrchr(parent_dir, L'\\');
            if (last_slash) {
                *last_slash = L'\0';
                if (build_path2(app->pet_root, MAX_PATH, parent_dir, L"my") && dir_exists_w(app->pet_root)) {
                    return 1;
                }
            }
        }
    }

    return 0;
}

static void free_pet_list(PetList *list) {
    safe_free(list->items);
    list->items = NULL;
    list->count = 0;
}

static int load_pet_metadata(App *app, const wchar_t *folder_name, PetMetadata *pet) {
    wchar_t json_path[MAX_PATH];
    wchar_t webp_path[MAX_PATH];
    wchar_t png_path[MAX_PATH];
    char *json = NULL;
    char display_name_utf8[PET_NAME_CAP * 3];
    char description_utf8[PET_DESC_CAP * 3];
    char sprite_name_utf8[MAX_PATH];
    wchar_t sprite_name_w[MAX_PATH];

    memset(pet, 0, sizeof(*pet));
    wcsncpy(pet->id, folder_name, PET_ID_CAP - 1);

    if (!build_path3(json_path, MAX_PATH, app->pet_root, folder_name, L"pet.json")) {
        return 0;
    }
    if (!read_file_utf8(json_path, &json, NULL)) {
        return 0;
    }

    if (!json_extract_string(json, "displayName", display_name_utf8, sizeof(display_name_utf8))) {
        safe_free(json);
        return 0;
    }
    if (!wide_from_utf8(display_name_utf8, pet->display_name, PET_NAME_CAP)) {
        safe_free(json);
        return 0;
    }
    if (json_extract_string(json, "description", description_utf8, sizeof(description_utf8))) {
        wide_from_utf8(description_utf8, pet->description, PET_DESC_CAP);
    }

    if (json_extract_string(json, "spritesheetPath", sprite_name_utf8, sizeof(sprite_name_utf8)) &&
        wide_from_utf8(sprite_name_utf8, sprite_name_w, MAX_PATH) &&
        build_path3(pet->sprite_path, MAX_PATH, app->pet_root, folder_name, sprite_name_w) &&
        file_exists_w(pet->sprite_path)) {
        safe_free(json);
        return 1;
    }

    safe_free(json);

    if (build_path3(webp_path, MAX_PATH, app->pet_root, folder_name, L"spritesheet.webp") && file_exists_w(webp_path)) {
        wcsncpy(pet->sprite_path, webp_path, MAX_PATH - 1);
        return 1;
    }
    if (build_path3(png_path, MAX_PATH, app->pet_root, folder_name, L"spritesheet.png") && file_exists_w(png_path)) {
        wcsncpy(pet->sprite_path, png_path, MAX_PATH - 1);
        return 1;
    }
    return 0;
}

static int scan_pets(App *app) {
    WIN32_FIND_DATAW entry;
    HANDLE handle;
    wchar_t pattern[MAX_PATH];
    PetMetadata *items;
    size_t count = 0;

    free_pet_list(&app->pets);

    if (!build_path2(pattern, MAX_PATH, app->pet_root, L"*")) {
        return 0;
    }

    items = (PetMetadata *) calloc(MAX_PETS, sizeof(PetMetadata));
    if (!items) {
        return 0;
    }

    handle = FindFirstFileW(pattern, &entry);
    if (handle == INVALID_HANDLE_VALUE) {
        safe_free(items);
        return 0;
    }

    do {
        if ((entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
            continue;
        }
        if (wcscmp(entry.cFileName, L".") == 0 || wcscmp(entry.cFileName, L"..") == 0) {
            continue;
        }
        if (count >= MAX_PETS) {
            break;
        }
        if (load_pet_metadata(app, entry.cFileName, &items[count])) {
            ++count;
        }
    } while (FindNextFileW(handle, &entry));

    FindClose(handle);

    if (count == 0) {
        safe_free(items);
        return 0;
    }

    app->pets.items = items;
    app->pets.count = count;
    return 1;
}

static void free_sprite_sheet(SpriteSheet *sheet) {
    safe_free(sheet->pixels);
    sheet->pixels = NULL;
    sheet->width = 0;
    sheet->height = 0;
}

static int ensure_wic_factory(App *app) {
    HRESULT hr;
    if (app->wic) {
        return 1;
    }
    hr = CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory, (void **) &app->wic);
    return SUCCEEDED(hr) && app->wic != NULL;
}

static int load_sprite_sheet(App *app, const wchar_t *path, SpriteSheet *sheet) {
    IWICBitmapDecoder *decoder = NULL;
    IWICBitmapFrameDecode *frame = NULL;
    IWICFormatConverter *converter = NULL;
    UINT width = 0;
    UINT height = 0;
    size_t bytes;
    HRESULT hr;

    free_sprite_sheet(sheet);

    if (!ensure_wic_factory(app)) {
        return 0;
    }

    hr = IWICImagingFactory_CreateDecoderFromFilename(app->wic, path, NULL, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder);
    if (FAILED(hr)) {
        return 0;
    }

    hr = IWICBitmapDecoder_GetFrame(decoder, 0, &frame);
    if (FAILED(hr)) {
        IWICBitmapDecoder_Release(decoder);
        return 0;
    }

    hr = IWICBitmapFrameDecode_GetSize(frame, &width, &height);
    if (FAILED(hr) || width < FRAME_W * 8 || height < FRAME_H * STATE_COUNT) {
        IWICBitmapFrameDecode_Release(frame);
        IWICBitmapDecoder_Release(decoder);
        return 0;
    }

    hr = IWICImagingFactory_CreateFormatConverter(app->wic, &converter);
    if (FAILED(hr)) {
        IWICBitmapFrameDecode_Release(frame);
        IWICBitmapDecoder_Release(decoder);
        return 0;
    }

    hr = IWICFormatConverter_Initialize(
        converter,
        (IWICBitmapSource *) frame,
        &GUID_WICPixelFormat32bppPBGRA,
        WICBitmapDitherTypeNone,
        NULL,
        0.0,
        WICBitmapPaletteTypeCustom
    );
    if (FAILED(hr)) {
        IWICFormatConverter_Release(converter);
        IWICBitmapFrameDecode_Release(frame);
        IWICBitmapDecoder_Release(decoder);
        return 0;
    }

    bytes = (size_t) width * (size_t) height * 4;
    sheet->pixels = (uint32_t *) malloc(bytes);
    if (!sheet->pixels) {
        IWICFormatConverter_Release(converter);
        IWICBitmapFrameDecode_Release(frame);
        IWICBitmapDecoder_Release(decoder);
        return 0;
    }

    hr = IWICFormatConverter_CopyPixels(converter, NULL, width * 4, (UINT) bytes, (BYTE *) sheet->pixels);
    if (FAILED(hr)) {
        free_sprite_sheet(sheet);
        IWICFormatConverter_Release(converter);
        IWICBitmapFrameDecode_Release(frame);
        IWICBitmapDecoder_Release(decoder);
        return 0;
    }

    sheet->width = (int) width;
    sheet->height = (int) height;

    IWICFormatConverter_Release(converter);
    IWICBitmapFrameDecode_Release(frame);
    IWICBitmapDecoder_Release(decoder);
    return 1;
}

static void pet_set_state(PetWindow *pet, int state) {
    if (state < 0 || state >= STATE_COUNT) {
        state = PET_STATE_IDLE;
    }
    pet->current_state = state;
    pet->current_frame = 0;
}

static int pick_idle_variation(void) {
    switch (rand() % 5) {
        case 0: return PET_STATE_WAVING;
        case 1: return PET_STATE_JUMPING;
        case 2: return PET_STATE_WAITING;
        case 3: return PET_STATE_REVIEW;
        default: return PET_STATE_IDLE;
    }
}

static void pet_tick_state(PetWindow *pet) {
    int frame_count = k_frame_counts[pet->current_state];
    if (frame_count <= 0) {
        frame_count = 1;
    }

    pet->current_frame++;
    if (pet->current_frame < frame_count) {
        return;
    }

    pet->current_frame = 0;
    if (pet->dragging) {
        return;
    }

    if (pet->current_state == PET_STATE_IDLE) {
        pet->idle_loops++;
        if (pet->idle_loops >= 2 && (rand() % 3) == 0) {
            pet->idle_loops = 0;
            pet_set_state(pet, pick_idle_variation());
        }
    } else {
        pet_set_state(pet, PET_STATE_IDLE);
    }
}

static void destroy_render_buffer(PetWindow *pet) {
    if (pet->mem_dc) {
        DeleteDC(pet->mem_dc);
        pet->mem_dc = NULL;
    }
    if (pet->dib) {
        DeleteObject(pet->dib);
        pet->dib = NULL;
    }
    pet->dib_pixels = NULL;
}

static int ensure_render_buffer(PetWindow *pet) {
    HDC screen_dc;
    BITMAPINFO bmi;

    if (pet->mem_dc && pet->dib && pet->dib_pixels) {
        return 1;
    }

    screen_dc = GetDC(NULL);
    if (!screen_dc) {
        return 0;
    }

    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = FRAME_W;
    bmi.bmiHeader.biHeight = -FRAME_H;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    pet->mem_dc = CreateCompatibleDC(screen_dc);
    pet->dib = CreateDIBSection(screen_dc, &bmi, DIB_RGB_COLORS, &pet->dib_pixels, NULL, 0);
    ReleaseDC(NULL, screen_dc);

    if (!pet->mem_dc || !pet->dib || !pet->dib_pixels) {
        destroy_render_buffer(pet);
        return 0;
    }

    SelectObject(pet->mem_dc, pet->dib);
    return 1;
}

static void clear_render_buffer(PetWindow *pet) {
    if (pet->dib_pixels) {
        memset(pet->dib_pixels, 0, FRAME_W * FRAME_H * 4);
    }
}

static void present_pet_frame(App *app) {
    PetWindow *pet = &app->pet_window;
    POINT dst;
    SIZE size;
    POINT src = {0, 0};
    BLENDFUNCTION blend;
    HDC screen_dc;
    uint8_t *dst_pixels;
    const uint8_t *src_pixels;
    int y;
    int src_x;
    int src_y;

    if (!pet->hwnd || !app->sheet.pixels) {
        return;
    }
    if (!ensure_render_buffer(pet)) {
        return;
    }

    src_x = pet->current_frame * FRAME_W;
    src_y = pet->current_state * FRAME_H;
    if (src_x + FRAME_W > app->sheet.width || src_y + FRAME_H > app->sheet.height) {
        pet_set_state(pet, PET_STATE_IDLE);
        src_x = 0;
        src_y = 0;
    }

    dst_pixels = (uint8_t *) pet->dib_pixels;
    src_pixels = (const uint8_t *) app->sheet.pixels + (((size_t) src_y * (size_t) app->sheet.width) + (size_t) src_x) * 4;
    for (y = 0; y < FRAME_H; ++y) {
        memcpy(dst_pixels + (size_t) y * FRAME_W * 4, src_pixels + (size_t) y * app->sheet.width * 4, FRAME_W * 4);
    }

    RECT rect;
    GetWindowRect(pet->hwnd, &rect);
    dst.x = rect.left;
    dst.y = rect.top;
    size.cx = FRAME_W;
    size.cy = FRAME_H;

    blend.BlendOp = AC_SRC_OVER;
    blend.BlendFlags = 0;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;

    screen_dc = GetDC(NULL);
    if (!screen_dc) {
        return;
    }
    UpdateLayeredWindow(pet->hwnd, screen_dc, &dst, &size, pet->mem_dc, &src, 0, &blend, ULW_ALPHA);
    ReleaseDC(NULL, screen_dc);
}

static void reset_pet_timer(PetWindow *pet) {
    if (pet->hwnd) {
        SetTimer(pet->hwnd, 1, (UINT) k_frame_durations_ms[pet->current_state], NULL);
    }
}

static void unload_pet(App *app) {
    PetWindow *pet = &app->pet_window;
    if (pet->hwnd) {
        KillTimer(pet->hwnd, 1);
    }
    destroy_render_buffer(pet);
    free_sprite_sheet(&app->sheet);
    pet->current_pet_index = -1;
    pet->current_frame = 0;
    pet->current_state = PET_STATE_IDLE;
    pet->idle_loops = 0;
}

static int load_pet_by_index(App *app, int index) {
    if (index < 0 || (size_t) index >= app->pets.count) {
        return 0;
    }

    if (!load_sprite_sheet(app, app->pets.items[index].sprite_path, &app->sheet)) {
        return 0;
    }

    app->pet_window.current_pet_index = index;
    pet_set_state(&app->pet_window, PET_STATE_IDLE);
    app->pet_window.idle_loops = 0;
    clear_render_buffer(&app->pet_window);
    reset_pet_timer(&app->pet_window);
    return 1;
}

static int run_selector(App *app);

static void reload_pet_with_selector(App *app) {
    int choice = run_selector(app);
    if (choice < 0) {
        return;
    }
    unload_pet(app);
    if (load_pet_by_index(app, choice)) {
        present_pet_frame(app);
    } else {
        MessageBoxW(NULL, L"Failed to load the selected pet spritesheet.", APP_NAME, MB_ICONERROR);
    }
}

static LRESULT CALLBACK pet_window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    App *app = &g_app;
    PetWindow *pet = &app->pet_window;

    switch (msg) {
        case WM_CREATE:
            pet->hwnd = hwnd;
            reset_pet_timer(pet);
            return 0;
        case WM_TIMER:
            pet_tick_state(pet);
            reset_pet_timer(pet);
            present_pet_frame(app);
            return 0;
        case WM_LBUTTONDOWN: {
            POINT cursor = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            RECT rect;
            GetCursorPos(&pet->drag_cursor_start);
            GetWindowRect(hwnd, &rect);
            pet->drag_window_start.x = rect.left;
            pet->drag_window_start.y = rect.top;
            pet->dragging = 1;
            pet->last_drag_delta_x = 0;
            SetCapture(hwnd);
            pet_set_state(pet, PET_STATE_IDLE);
            present_pet_frame(app);
            (void) cursor;
            return 0;
        }
        case WM_MOUSEMOVE:
            if (pet->dragging) {
                POINT screen;
                int dx;
                int dy;
                GetCursorPos(&screen);
                dx = screen.x - pet->drag_cursor_start.x;
                dy = screen.y - pet->drag_cursor_start.y;
                pet->last_drag_delta_x = dx;
                if (dx > 1) {
                    pet_set_state(pet, PET_STATE_RUN_RIGHT);
                } else if (dx < -1) {
                    pet_set_state(pet, PET_STATE_RUN_LEFT);
                }
                SetWindowPos(hwnd, NULL, pet->drag_window_start.x + dx, pet->drag_window_start.y + dy, 0, 0,
                    SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
                reset_pet_timer(pet);
                present_pet_frame(app);
            }
            return 0;
        case WM_LBUTTONUP:
            if (pet->dragging) {
                pet->dragging = 0;
                ReleaseCapture();
                pet_set_state(pet, PET_STATE_IDLE);
                reset_pet_timer(pet);
                present_pet_frame(app);
            }
            return 0;
        case WM_LBUTTONDBLCLK:
            reload_pet_with_selector(app);
            return 0;
        case WM_RBUTTONUP:
            reload_pet_with_selector(app);
            return 0;
        case WM_DESTROY:
            unload_pet(app);
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
}

static int ensure_pet_window_class(HINSTANCE instance) {
    static int registered = 0;
    WNDCLASSEXW wc;
    if (registered) {
        return 1;
    }
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.hInstance = instance;
    wc.lpfnWndProc = pet_window_proc;
    wc.lpszClassName = PET_WINDOW_CLASS;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.style = CS_DBLCLKS;
    if (!RegisterClassExW(&wc)) {
        return 0;
    }
    registered = 1;
    return 1;
}

static int create_pet_window(App *app) {
    int screen_w;
    int screen_h;
    int x;
    int y;
    HWND hwnd;

    if (!ensure_pet_window_class(app->instance)) {
        return 0;
    }

    screen_w = GetSystemMetrics(SM_CXSCREEN);
    screen_h = GetSystemMetrics(SM_CYSCREEN);
    x = screen_w - FRAME_W - 40;
    y = screen_h - FRAME_H - 80;

    hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
        PET_WINDOW_CLASS,
        APP_NAME,
        WS_POPUP,
        x, y, FRAME_W, FRAME_H,
        NULL,
        NULL,
        app->instance,
        NULL
    );

    if (!hwnd) {
        return 0;
    }

    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    UpdateWindow(hwnd);
    return 1;
}

static void selector_start_or_close(SelectorState *state, int accept) {
    int index;
    if (!accept) {
        state->result = -1;
        DestroyWindow(state->hwnd);
        return;
    }
    index = (int) SendMessageW(state->listbox, LB_GETCURSEL, 0, 0);
    if (index < 0) {
        MessageBoxW(state->hwnd, L"Please select a pet first.", APP_NAME, MB_ICONINFORMATION);
        return;
    }
    state->result = index;
    DestroyWindow(state->hwnd);
}

static void selector_free_preview(SelectorState *state) {
    if (state->preview_dc) {
        DeleteDC(state->preview_dc);
        state->preview_dc = NULL;
    }
    if (state->preview_dib) {
        DeleteObject(state->preview_dib);
        state->preview_dib = NULL;
    }
    state->preview_pixels = NULL;
    free_sprite_sheet(&state->preview_sheet);
    state->preview_pet_index = -1;
    state->preview_frame = 0;
    state->preview_state = PET_STATE_IDLE;
    state->preview_idle_loops = 0;
}

static int selector_ensure_preview_surface(SelectorState *state) {
    HDC screen_dc;
    BITMAPINFO bmi;

    if (state->preview_dc && state->preview_dib && state->preview_pixels) {
        return 1;
    }

    screen_dc = GetDC(state->hwnd);
    if (!screen_dc) {
        return 0;
    }

    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = FRAME_W;
    bmi.bmiHeader.biHeight = -FRAME_H;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    state->preview_dc = CreateCompatibleDC(screen_dc);
    state->preview_dib = CreateDIBSection(screen_dc, &bmi, DIB_RGB_COLORS, &state->preview_pixels, NULL, 0);
    ReleaseDC(state->hwnd, screen_dc);

    if (!state->preview_dc || !state->preview_dib || !state->preview_pixels) {
        selector_free_preview(state);
        return 0;
    }

    SelectObject(state->preview_dc, state->preview_dib);
    return 1;
}

static void selector_set_preview_state(SelectorState *state, int preview_state) {
    if (preview_state < 0 || preview_state >= STATE_COUNT) {
        preview_state = PET_STATE_IDLE;
    }
    state->preview_state = preview_state;
    state->preview_frame = 0;
}

static void selector_render_preview(SelectorState *state) {
    uint8_t *dst_pixels;
    const uint8_t *src_pixels;
    int src_x;
    int src_y;
    int y;

    if (!state->preview_sheet.pixels) {
        return;
    }
    if (!selector_ensure_preview_surface(state)) {
        return;
    }

    src_x = state->preview_frame * FRAME_W;
    src_y = state->preview_state * FRAME_H;
    if (src_x + FRAME_W > state->preview_sheet.width || src_y + FRAME_H > state->preview_sheet.height) {
        selector_set_preview_state(state, PET_STATE_IDLE);
        src_x = 0;
        src_y = 0;
    }

    dst_pixels = (uint8_t *) state->preview_pixels;
    src_pixels = (const uint8_t *) state->preview_sheet.pixels + (((size_t) src_y * (size_t) state->preview_sheet.width) + (size_t) src_x) * 4;
    for (y = 0; y < FRAME_H; ++y) {
        memcpy(dst_pixels + (size_t) y * FRAME_W * 4, src_pixels + (size_t) y * state->preview_sheet.width * 4, FRAME_W * 4);
    }

    InvalidateRect(state->hwnd, &state->preview_rect, TRUE);
}

static void selector_advance_preview(SelectorState *state) {
    int frame_count = k_frame_counts[state->preview_state];
    if (!state->preview_sheet.pixels) {
        return;
    }
    if (frame_count <= 0) {
        frame_count = 1;
    }

    state->preview_frame++;
    if (state->preview_frame < frame_count) {
        selector_render_preview(state);
        return;
    }

    state->preview_frame = 0;
    if (state->preview_state == PET_STATE_IDLE) {
        state->preview_idle_loops++;
        if (state->preview_idle_loops >= 2) {
            state->preview_idle_loops = 0;
            switch (rand() % 4) {
                case 0: selector_set_preview_state(state, PET_STATE_WAVING); break;
                case 1: selector_set_preview_state(state, PET_STATE_JUMPING); break;
                case 2: selector_set_preview_state(state, PET_STATE_WAITING); break;
                default: selector_set_preview_state(state, PET_STATE_IDLE); break;
            }
        }
    } else {
        selector_set_preview_state(state, PET_STATE_IDLE);
    }
    selector_render_preview(state);
}

static void selector_update_details(SelectorState *state, int index) {
    const PetMetadata *pet;

    if (index < 0 || (size_t) index >= state->app->pets.count) {
        SetWindowTextW(state->name_label, L"");
        SetWindowTextW(state->description_edit, L"");
        selector_free_preview(state);
        InvalidateRect(state->hwnd, &state->preview_rect, TRUE);
        return;
    }

    pet = &state->app->pets.items[index];
    SetWindowTextW(state->name_label, pet->display_name);
    SetWindowTextW(state->description_edit, pet->description[0] ? pet->description : L"No description available.");

    if (state->preview_pet_index != index) {
        selector_free_preview(state);
        if (load_sprite_sheet(state->app, pet->sprite_path, &state->preview_sheet)) {
            state->preview_pet_index = index;
            selector_set_preview_state(state, PET_STATE_IDLE);
            selector_render_preview(state);
        } else {
            state->preview_pet_index = -1;
            InvalidateRect(state->hwnd, &state->preview_rect, TRUE);
        }
    }
}

static LRESULT CALLBACK selector_window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    SelectorState *state = (SelectorState *) GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (msg) {
        case WM_CREATE: {
            CREATESTRUCTW *create = (CREATESTRUCTW *) lparam;
            int i;
            state = (SelectorState *) create->lpCreateParams;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR) state);
            state->hwnd = hwnd;
            state->preview_pet_index = -1;
            state->preview_rect.left = 472;
            state->preview_rect.top = 48;
            state->preview_rect.right = state->preview_rect.left + 216;
            state->preview_rect.bottom = state->preview_rect.top + 232;

            CreateWindowExW(0, L"STATIC", L"Choose a pet. Double-click a name or press Start.", WS_CHILD | WS_VISIBLE,
                16, 16, 420, 24, hwnd, NULL, g_app.instance, NULL);
            CreateWindowExW(0, L"STATIC", L"Preview", WS_CHILD | WS_VISIBLE,
                472, 16, 216, 24, hwnd, NULL, g_app.instance, NULL);

            state->listbox = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
                WS_CHILD | WS_VISIBLE | LBS_NOTIFY | WS_VSCROLL | WS_TABSTOP,
                16, 48, 420, 300, hwnd, (HMENU) 101, g_app.instance, NULL);
            state->name_label = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE,
                472, 292, 216, 24, hwnd, NULL, g_app.instance, NULL);
            CreateWindowExW(0, L"STATIC", L"Description", WS_CHILD | WS_VISIBLE,
                472, 324, 216, 20, hwnd, NULL, g_app.instance, NULL);
            state->description_edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL,
                472, 348, 216, 76, hwnd, (HMENU) 104, g_app.instance, NULL);
            state->start_button = CreateWindowExW(0, L"BUTTON", L"Start", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                598, 432, 90, 28, hwnd, (HMENU) 102, g_app.instance, NULL);
            CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                502, 432, 90, 28, hwnd, (HMENU) 103, g_app.instance, NULL);

            for (i = 0; i < (int) state->app->pets.count; ++i) {
                SendMessageW(state->listbox, LB_ADDSTRING, 0, (LPARAM) state->app->pets.items[i].display_name);
            }
            SendMessageW(state->listbox, LB_SETCURSEL, 0, 0);
            selector_update_details(state, 0);
            SetTimer(hwnd, SELECTOR_PREVIEW_TIMER_ID, 120, NULL);
            SetFocus(state->listbox);
            return 0;
        }
        case WM_COMMAND:
            if (!state) {
                return 0;
            }
            if (LOWORD(wparam) == 101 && HIWORD(wparam) == LBN_SELCHANGE) {
                selector_update_details(state, (int) SendMessageW(state->listbox, LB_GETCURSEL, 0, 0));
                return 0;
            }
            if (LOWORD(wparam) == 101 && HIWORD(wparam) == LBN_DBLCLK) {
                selector_start_or_close(state, 1);
                return 0;
            }
            if (LOWORD(wparam) == 102) {
                selector_start_or_close(state, 1);
                return 0;
            }
            if (LOWORD(wparam) == 103) {
                selector_start_or_close(state, 0);
                return 0;
            }
            return 0;
        case WM_TIMER:
            if (state && wparam == SELECTOR_PREVIEW_TIMER_ID) {
                selector_advance_preview(state);
            }
            return 0;
        case WM_PAINT:
            if (state) {
                PAINTSTRUCT ps;
                HDC dc = BeginPaint(hwnd, &ps);
                HBRUSH panel_brush = CreateSolidBrush(RGB(250, 250, 250));
                HBRUSH border_brush = CreateSolidBrush(RGB(210, 210, 210));
                RECT panel = state->preview_rect;
                BLENDFUNCTION blend;
                int draw_x;
                int draw_y;

                FillRect(dc, &panel, panel_brush);
                FrameRect(dc, &panel, border_brush);

                if (state->preview_dc && state->preview_dib && state->preview_pixels) {
                    draw_x = panel.left + ((panel.right - panel.left) - FRAME_W) / 2;
                    draw_y = panel.top + ((panel.bottom - panel.top) - FRAME_H) / 2;
                    blend.BlendOp = AC_SRC_OVER;
                    blend.BlendFlags = 0;
                    blend.SourceConstantAlpha = 255;
                    blend.AlphaFormat = AC_SRC_ALPHA;
                    AlphaBlend(dc, draw_x, draw_y, FRAME_W, FRAME_H, state->preview_dc, 0, 0, FRAME_W, FRAME_H, blend);
                } else {
                    RECT text_rect = panel;
                    SetBkMode(dc, TRANSPARENT);
                    DrawTextW(dc, L"Preview unavailable", -1, &text_rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                }

                DeleteObject(panel_brush);
                DeleteObject(border_brush);
                EndPaint(hwnd, &ps);
                return 0;
            }
            return DefWindowProcW(hwnd, msg, wparam, lparam);
        case WM_CLOSE:
            if (state) {
                selector_start_or_close(state, 0);
            }
            return 0;
        case WM_DESTROY:
            if (state) {
                KillTimer(hwnd, SELECTOR_PREVIEW_TIMER_ID);
                selector_free_preview(state);
                state->done = 1;
            }
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
}

static int ensure_selector_class(HINSTANCE instance) {
    static int registered = 0;
    WNDCLASSEXW wc;
    if (registered) {
        return 1;
    }
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.hInstance = instance;
    wc.lpfnWndProc = selector_window_proc;
    wc.lpszClassName = SELECTOR_WINDOW_CLASS;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH) (COLOR_WINDOW + 1);
    if (!RegisterClassExW(&wc)) {
        return 0;
    }
    registered = 1;
    return 1;
}

static int run_selector(App *app) {
    SelectorState state;
    MSG msg;
    int screen_w;
    int screen_h;
    int x;
    int y;

    memset(&state, 0, sizeof(state));
    state.app = app;
    state.result = -1;

    if (!ensure_selector_class(app->instance)) {
        return -1;
    }

    screen_w = GetSystemMetrics(SM_CXSCREEN);
    screen_h = GetSystemMetrics(SM_CYSCREEN);
    x = (screen_w - 460) / 2;
    y = (screen_h - 510) / 2;
    x = (screen_w - 720) / 2;

    state.hwnd = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        SELECTOR_WINDOW_CLASS,
        L"Pet Selector",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        x, y, 720, 510,
        NULL,
        NULL,
        app->instance,
        &state
    );

    if (!state.hwnd) {
        return -1;
    }

    ShowWindow(state.hwnd, SW_SHOW);
    UpdateWindow(state.hwnd);

    while (!state.done && GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (msg.message == WM_HOTKEY && (int) msg.wParam == HOTKEY_EXIT_ID) {
            selector_start_or_close(&state, 0);
            continue;
        }
        if (!IsDialogMessageW(state.hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    return state.result;
}

static int app_init(App *app, HINSTANCE instance) {
    memset(app, 0, sizeof(*app));
    app->instance = instance;
    app->pet_window.current_pet_index = -1;
    srand((unsigned int) GetTickCount());

    if (FAILED(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED))) {
        return 0;
    }
    if (!resolve_pet_root(app)) {
        MessageBoxW(NULL, L"Could not locate the pet root directory. Set DIGIT_PET_PATH or run beside a my folder.", APP_NAME, MB_ICONERROR);
        CoUninitialize();
        return 0;
    }
    if (!scan_pets(app)) {
        MessageBoxW(NULL, L"No valid pets were found in the pet root directory.", APP_NAME, MB_ICONERROR);
        CoUninitialize();
        return 0;
    }
    return 1;
}

static void app_shutdown(App *app) {
    if (app->hotkey_registered) {
        UnregisterHotKey(NULL, HOTKEY_EXIT_ID);
        app->hotkey_registered = 0;
    }
    unload_pet(app);
    free_pet_list(&app->pets);
    if (app->wic) {
        IWICImagingFactory_Release(app->wic);
        app->wic = NULL;
    }
    CoUninitialize();
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE prev_instance, PWSTR cmd_line, int show_cmd) {
    MSG msg;
    int selected_index;

    (void) prev_instance;
    (void) cmd_line;
    (void) show_cmd;

    if (!app_init(&g_app, instance)) {
        return 1;
    }

    selected_index = run_selector(&g_app);
    if (selected_index < 0) {
        app_shutdown(&g_app);
        return 0;
    }

    if (!load_pet_by_index(&g_app, selected_index)) {
        MessageBoxW(NULL, L"Failed to load the selected pet spritesheet.", APP_NAME, MB_ICONERROR);
        app_shutdown(&g_app);
        return 1;
    }

    if (!create_pet_window(&g_app)) {
        MessageBoxW(NULL, L"Failed to create the desktop pet window.", APP_NAME, MB_ICONERROR);
        app_shutdown(&g_app);
        return 1;
    }

    g_app.hotkey_registered = RegisterHotKey(NULL, HOTKEY_EXIT_ID, 0, VK_ESCAPE);
    present_pet_frame(&g_app);

    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (msg.message == WM_HOTKEY && (int) msg.wParam == HOTKEY_EXIT_ID) {
            if (g_app.pet_window.hwnd) {
                DestroyWindow(g_app.pet_window.hwnd);
            }
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    app_shutdown(&g_app);
    return 0;
}
