#include "pet_common.h"
#include "pet_render.h"
#include "pet_ai.h"
#include "pet_wnd.h"
#include "pet_io.h"

/* 全局变量定义 */
App g_app = {0};
HWND g_focused_pet = NULL;
wchar_t g_base_dir[MAX_PATH];

/* MSVC 浮点支持符号（无 CRT 时需要） */
#ifdef _MSC_VER
int _fltused = 0;
#endif

/* MinGW 自定义入口点：跳过 crt0.o 启动代码以减小 exe 体积。
   仍然链接默认库（libgcc/libmingwex）以提供数学函数等支持。
   由于是纯 C 程序、静态链接且无全局构造函数，可以安全地
   跳过 CRT 初始化，直接调用 WinMain。msvcrt.dll 会在
   DllMain 中自行初始化，因此 malloc/calloc/rand 等函数
   在入口点执行时已经可用。 */
#ifdef __GNUC__
void __stdcall entry(void)
{
    int ret = WinMain(GetModuleHandleW(NULL), NULL, NULL, SW_SHOWDEFAULT);
    ExitProcess(ret);
}
#endif

int WINAPI WinMain(HINSTANCE hinst, HINSTANCE hPrev, LPSTR cmdLine, int cmdShow)
{
    /* 动态分配大型数组，避免占用 .data 段导致 exe 膨胀 */
    g_app.pets = (Pet *)calloc(MAX_PETS, sizeof(Pet));
    g_app.instances = (PetInst *)calloc(MAX_INSTANCES, sizeof(PetInst));
    if (!g_app.pets || !g_app.instances) return 1;

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

    /* 创建预览渲染缓冲区 */
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

    /* 注册窗口类 */
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

    /* 创建选择器窗口 */
    RECT rc = {0, 0, 300, 136};
    AdjustWindowRect(&rc, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);
    g_app.selector = CreateWindowExW(0, L"PetSelector", L"CodeX-Pet-3.0",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top, NULL, NULL, hinst, NULL);

    ShowWindow(g_app.selector, SW_SHOW);
    UpdateWindow(g_app.selector);
    SetTimer(g_app.selector, IDT_PREVIEW, 30, NULL);

    /* 消息循环 */
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        /* 方向键松开：停止奔跑 */
        if (msg.message == WM_KEYUP) {
            if ((msg.wParam == VK_LEFT || msg.wParam == VK_RIGHT ||
                 msg.wParam == VK_UP   || msg.wParam == VK_DOWN) && g_focused_pet) {
                PetInst *pi = (PetInst *)GetWindowLongPtrW(g_focused_pet, GWLP_USERDATA);
                if (pi && pi->alive && pi->temp_anim && (pi->state == 1 || pi->state == 2)) {
                    /* 优雅停止：让奔跑动画以原始速度播完当前周期 */
                    pi->run_loop_mode = 0;
                    pi->move_dx = 0;
                    pi->move_dy = 0;
                    continue;
                }
            }
        }
        if (msg.message == WM_KEYDOWN || msg.message == WM_SYSKEYDOWN) {
            /* 回车：生成宠物 */
            if (msg.wParam == VK_RETURN) {
                spawn_pet();
                continue;
            }
            /* ESC：退出 */
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

            /* 动画触发键映射 */
            int target = -1;
            switch (msg.wParam) {
            case VK_SPACE: target = 4; break; /* 跳跃 */
            case 'W':      target = 3; break; /* 挥手 */
            case 'E':      target = 5; break; /* 失败 */
            case 'Q':      target = 6; break; /* 等待 */
            case 'R':      target = 8; break; /* 审查 */
            case VK_LEFT:  target = 2; break; /* 向左跑 */
            case VK_RIGHT: target = 1; break; /* 向右跑 */
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
                /* 方向键仅当焦点在宠物窗口时生效 */
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

                /* 跳跃中仅允许方向键和空格（二段跳） */
                if (pi && pi->jump_active) {
                    if (msg.wParam != VK_LEFT && msg.wParam != VK_RIGHT && msg.wParam != VK_SPACE)
                        continue;
                }

                /* 跳跃（target==4）始终允许触发以支持二段跳；
                   其他临时动画跳过重复触发同一状态 */
                if (!pi || !pi->alive || !pi->temp_anim || pi->state != target || target == 4)
                    pet_trigger_anim(g_focused_pet, target);

                /* 设置奔跑方向 */
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

    /* 清理资源 */
    destroy_all_pets();
    for (int i = 0; i < g_app.pet_count; i++) free_pet(&g_app.pets[i]);
    free(g_app.pets);
    free(g_app.instances);
    if (g_app.prev_memdc) { DeleteDC(g_app.prev_memdc); g_app.prev_memdc = NULL; }
    if (g_app.prev_dib) { DeleteObject(g_app.prev_dib); g_app.prev_dib = NULL; }
    if (g_app.ui_font) { DeleteObject(g_app.ui_font); g_app.ui_font = NULL; }
    if (g_app.desc_font) { DeleteObject(g_app.desc_font); g_app.desc_font = NULL; }
    IWICImagingFactory_Release(g_app.wic);
    CoUninitialize();
    return 0;
}
