#include "pet_common.h"
#include "pet_json.h"
#include "pet_io.h"

/* 宽字符串拼接 */
static void wcat(wchar_t *dst, const wchar_t *src, int max)
{
    int d = (int)wcslen(dst);
    int s = 0;
    while (src[s] && d < max - 1)
        dst[d++] = src[s++];
    dst[d] = 0;
}

void init_base_dir(void)
{
    GetModuleFileNameW(NULL, g_base_dir, MAX_PATH);
    wchar_t *p = wcsrchr(g_base_dir, L'\\');
    if (p) *(p + 1) = 0;
}

void scan_pets(void)
{
    WIN32_FIND_DATAW fd;
    wchar_t search[MAX_PATH];
    wcscpy(search, g_base_dir);
    wcat(search, L"my-pet\\*", MAX_PATH);
    HANDLE h = FindFirstFileW(search, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
            wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
            continue;

        wchar_t json_path[MAX_PATH];
        wcscpy(json_path, g_base_dir);
        wcat(json_path, L"my-pet\\", MAX_PATH);
        wcat(json_path, fd.cFileName, MAX_PATH);
        wcat(json_path, L"\\pet.json", MAX_PATH);

        HANDLE fh = CreateFileW(json_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        if (fh == INVALID_HANDLE_VALUE) continue;

        DWORD size = GetFileSize(fh, NULL);
        if (size == 0 || size >= 65536) {
            CloseHandle(fh);
            continue;
        }

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
    } while (g_app.pet_count < MAX_PETS && FindNextFileW(h, &fd));
    FindClose(h);
}

int load_pet(Pet *pet)
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
                /* 将透明像素的 RGB 清零，避免 AlphaBlend 混合伪影 */
                for (UINT i = 0; i < w * h; i++) {
                    if (pet->pixels[i * 4 + 3] == 0)
                        *(DWORD *)&pet->pixels[i * 4] = 0;
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

void free_pet(Pet *pet)
{
    if (pet->pixels) {
        free(pet->pixels);
        pet->pixels = NULL;
    }
}
