# cpw.exe 逆向分析报告

## 结论

`cpw.exe` 不是 .NET 程序，而是一个 **x64 原生 Windows GUI 程序**，带有较完整的 **DWARF 调试信息和符号名**。  
因此目前能恢复到：

- 源码模块名
- 大量函数名/全局变量名
- 主要数据结构
- 主流程与窗口消息逻辑
- 接近源码级的伪代码

但**不能直接恢复原始 `.c` 文件全文**；本报告给出的内容是“高可信源码级还原”。

## 二进制基本信息

- 文件：`cpw.exe`
- 类型：PE32+ / x86_64 / Windows GUI
- 编译器：`GCC 14.2.0 (MinGW-w64 x86_64-msvcrt-posix-seh)`
- 编译时间戳：`2026-05-15 13:06:21 UTC`
- 节区：`.text .data .rdata .pdata .xdata .bss .idata .tls .reloc`
- 调试信息：存在 `.debug_info .debug_line .debug_str ...`

## 恢复出的源码模块

DWARF 与字符串中可见以下编译单元：

- `main.c`
- `pet.c`
- `sprite.c`
- `animation.c`
- `window.c`
- `selector.c`

这说明它原本就是一个比较清晰的 C 模块化项目。

## 程序用途判断

该程序是一个“桌宠 / desktop pet”类应用，核心特征：

- 从 `petdex` 目录枚举宠物
- 读取 `pet.json` 中的 `displayName`
- 加载 `spritesheet.webp` 或 `spritesheet.png`
- 弹出选择器窗口
- 选择后创建一个透明分层窗口显示宠物
- 支持拖拽、预览、左右切换宠物、全局 ESC 退出

## 关键资源与约定

### 路径来源

优先读取环境变量：

- `DIGIT_PET_PATH`

否则回退到程序目录下的默认路径，字符串中可见：

- `\\my`

### 每个宠物目录内容

程序会尝试读取：

- `pet.json`
- `spritesheet.webp`
- `spritesheet.png`

其中 `webp` 优先，找不到时回退到 `png`。

### pet.json 解析方式

它没有使用 JSON 库，而是用：

- `fgets`
- `strstr("\"displayName\"")`
- `strchr(':')`
- 手工跳过空格和引号

因此当前实现只是“文本扫描式解析”。

## 已恢复常量

从 `.rdata` 可恢复：

- 状态名：`Idle / Run Right / Run Left / Waving / Jumping / Failed / Waiting / Running / Review`
- `FRAME_W = 192`
- `FRAME_H = 208`
- `FRAME_COUNTS ≈ [6, 8, 8, 4, 5, 8, 6, 6, 6]`
- `FRAME_DURATIONS ≈ [183, 133, 133, 175, 168, 153, 168, 137, 172]`

## 近似数据结构还原

### 1. 宠物列表项

`get_all_pets()` 按 `0x80` 字节一项分配，且前 64 字节写入目录名，后 64 字节写入显示名，可还原为：

```c
typedef struct PetInfo {
    char id[64];
    char display_name[64];
} PetInfo; // 0x80
```

### 2. 动画帧

`load_spritesheet()` 为每个状态分配 `count * 0x4c` 字节，`render_frame()` 会读取：

- `+0x40`：帧在总图中的偏移
- `+0x44`：帧宽
- `+0x48`：帧高

近似可写为：

```c
typedef struct FrameInfo {
    unsigned char reserved[0x40];
    int offset;   // 横向/线性偏移
    int width;    // 192
    int height;   // 208
} FrameInfo; // 0x4c
```

### 3. 精灵表 / 当前宠物

`calloc(1, 0x128)` + 偏移访问可还原为：

```c
typedef struct AnimBucket {
    FrameInfo *frames;   // +0x00
    int count;           // +0x08
    int current_frame;   // +0x0c
} AnimBucket; // 0x10

typedef struct SpriteSheet {
    char id[64];             // +0x00
    unsigned char pad0[0x40];
    int sheet_w;             // +0x80
    int sheet_h;             // +0x84
    unsigned char *pixels;   // +0x88 BGRA 像素
    AnimBucket states[9];    // +0x90
    int current_state;       // +0x120
} SpriteSheet; // 0x128
```

### 4. 桌宠实例

`g_desktop_pets` 每项 `0x98` 字节，包含：

- 窗口句柄
- pet 索引
- 坐标/拖拽起点
- 时间戳
- 对应像素缓存指针

可视为：

```c
typedef struct DesktopPet {
    HWND hwnd;
    int pet_idx;
    unsigned char _pad0[0x8c];
} DesktopPet; // 0x98
```

详细字段可继续顺着 `pet_wnd_proc()` 精修。

## 主流程还原

### WinMain

```c
int WinMain(HINSTANCE hInst, HINSTANCE prev, LPSTR cmd, int show) {
    g_hinst = hInst;
    if (CoInitializeEx(NULL, COINIT_APARTMENTTHREADED) < 0) {
        return 1;
    }

    selector = create_pet_selector();
    if (!selector || selector->count <= 0) {
        MessageBoxW(NULL, L"No pets found in petdex.", L"Error", MB_ICONERROR);
        CoUninitialize();
        return 1;
    }

    selected = run_pet_selector(selector);
    destroy_pet_selector(selector);
    if (selected <= 0) {
        CoUninitialize();
        return 0;
    }

    pet_list = get_all_pets(&list_ptr);
    chosen = pet_list[selected - 1];

    pet = calloc(1, sizeof(SpriteSheet));
    strncpy_s(pet->id, 64, chosen.id, -1);
    build spritesheet path (.webp else .png);

    if (!load_spritesheet(pet, path)) {
        MessageBoxW(NULL, L"Failed to load spritesheet.", L"Error", MB_ICONERROR);
        cleanup...
        return 1;
    }

    hwnd = create_layered_window(pet, pet_list, pet_count, selected - 1);
    ShowWindow(hwnd, SW_SHOW);
    render_frame(hwnd, pet);

    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    cleanup_window(hwnd, pet);
    free(pet);
    free_pet_list(pet_list);
    CoUninitialize();
    return 0;
}
```

## 模块职责还原

### `main.c`

- `main`
- `WinMain`

职责：

- 初始化 COM
- 打开选择器
- 加载所选宠物
- 创建主分层窗口
- 进入消息循环

### `pet.c`

- `get_petdex_path_w`
- `state_name`
- `get_all_pets`
- `free_pet_list`

职责：

- 解析 `DIGIT_PET_PATH`
- 枚举宠物目录
- 扫描 `pet.json`
- 维护宠物列表

### `sprite.c`

- `load_spritesheet`
- `free_spritesheet`
- `render_frame`
- `reload_pet_window`
- `load_spritesheet_for_drag`

职责：

- 用 WIC 解码 `png/webp`
- 统一转成 `32bpp BGRA`
- 建立状态帧表
- 把当前帧绘制到 layered window

### `animation.c`

- `advance_animation`
- `set_state`

职责：

- 切换状态
- 推进当前状态的帧索引

### `window.c`

- `create_layered_window`
- `wnd_proc`
- `cleanup_window`
- `update_drag`
- `create_desktop_pet`
- `mouse_hook_proc`
- `keyboard_hook_proc`
- `pet_wnd_proc`

职责：

- 主宠物窗口创建
- 多桌宠窗口维护
- 鼠标拖拽
- 全局 ESC 退出
- 宠物窗口自己的计时/绘制/销毁逻辑

### `selector.c`

- `create_pet_selector`
- `destroy_pet_selector`
- `run_pet_selector`
- `selector_wnd_proc`
- `draw_preview_frame`

职责：

- 选择器窗口
- 宠物名称列表
- 右侧预览图
- 记录选中项并返回

## 全局变量概览

业务上最关键的全局变量：

- `g_pet`
- `g_pet_list`
- `g_pet_count`
- `g_current_pet_idx`
- `g_hwnd`
- `g_wic_factory`
- `g_petdex_path`
- `g_desktop_pet_count`
- `g_desktop_pets`
- `g_pet_pixels`
- `g_pet_sheet_w`
- `g_drag_window`
- `g_drag_pixels`
- `g_drag_sheet_w`
- `g_drag_sheet_h`
- `g_dragging`
- `g_anim_state`
- `g_preview_frame`
- `g_cached_path`
- `g_cached_pixels`
- `g_selected_pet_id`

这说明程序高度依赖全局状态。

## 用到的系统函数总结

### 文件与路径

- `GetEnvironmentVariableW`
- `FindFirstFileW`
- `FindNextFileW`
- `FindClose`
- `GetFileAttributesW`
- `GetModuleFileNameW`
- `WideCharToMultiByte`
- `MultiByteToWideChar`

### COM / WIC

- `CoInitializeEx`
- `CoCreateInstance`
- `CoUninitialize`

用途：

- 创建 `IWICImagingFactory`
- 打开图片解码器
- 统一转成 `32bpp BGRA`

### GUI / 窗口

- `RegisterClassExW`
- `CreateWindowExW`
- `DefWindowProcW`
- `ShowWindow`
- `UpdateWindow`
- `SetWindowPos`
- `SetLayeredWindowAttributes`
- `UpdateLayeredWindow`
- `SetTimer`
- `KillTimer`
- `BeginPaint`
- `EndPaint`
- `InvalidateRect`
- `DestroyWindow`
- `PostQuitMessage`
- `MessageBoxW`

### GDI 绘制

- `GetDC`
- `ReleaseDC`
- `CreateCompatibleDC`
- `CreateDIBSection`
- `SelectObject`
- `DeleteObject`
- `DeleteDC`
- `BitBlt`
- `FillRect`
- `CreateSolidBrush`

### 输入与 Hook

- `SetWindowsHookExW`
- `UnhookWindowsHookEx`
- `CallNextHookEx`
- `SetCapture`
- `ReleaseCapture`
- `GetCursorPos`
- `GetMessagePos`
- `GetAsyncKeyState`

### C 运行库

- `malloc`
- `calloc`
- `realloc`
- `free`
- `memcpy`
- `memset`
- `fgets`
- `_wfopen`
- `fclose`
- `strstr`
- `strchr`
- `strrchr`
- `strncmp`
- `strncpy_s`
- `wcscmp`
- `wcsncpy_s`
- `wsprintfW`

## 关键逻辑分析

### 1. 宠物目录扫描

`get_all_pets()` 的逻辑：

1. 解析根目录
2. 用 `FindFirstFileW("%ls\\*")` 遍历目录
3. 跳过 `.` 和 `..`
4. 检查 `spritesheet.webp`，不存在则检查 `spritesheet.png`
5. 用 `pet.json` 扫 `displayName`
6. 把目录名写入 `id[64]`
7. 把 `displayName` 写入 `display_name[64]`

### 2. 图片解码

`load_spritesheet()`：

1. 延迟创建 `g_wic_factory`
2. 打开图像解码器
3. 取第一帧
4. 获取宽高
5. 转成 `32bpp BGRA`
6. 分配整张像素缓冲区
7. 拷贝所有像素
8. 初始化 9 个状态对应的帧表

### 3. 主窗口绘制

`render_frame()`：

1. 读取当前状态 `current_state`
2. 取该状态当前帧 `current_frame`
3. 计算该帧在总图中的偏移
4. 创建临时 `DIBSection`
5. 逐行 `memcpy` 当前帧到临时位图
6. 调用 `UpdateLayeredWindow()` 更新透明窗体

### 4. 拖拽逻辑

`mouse_hook_proc()` + `update_drag()`：

1. 鼠标按下时开始准备拖拽
2. 鼠标移动超过阈值后标记 `g_moved`
3. 为拖拽预览创建独立透明窗口
4. 实时把拖拽中的预览图更新到屏幕
5. 松开鼠标后销毁拖拽窗口
6. 再调用 `create_desktop_pet()` 在目标位置生成真正桌宠

### 5. 选择器

`run_pet_selector()`：

1. 创建 `PetSelectorClass`
2. 显示列表窗口
3. 通过 `selector_wnd_proc()` 处理按钮、列表框、预览刷新
4. 把结果写到 `g_exit_code`
5. 关闭消息循环并返回所选索引

## 最值得优先优化的部分

### P1. 每帧都创建/销毁 GDI 对象，开销偏大

`render_frame()` 每次刷新都会：

- `GetDC`
- `CreateCompatibleDC`
- `CreateDIBSection`
- `SelectObject`
- `UpdateLayeredWindow`
- `DeleteObject`
- `DeleteDC`
- `ReleaseDC`

这意味着动画每一帧都重新创建绘图上下文，成本明显偏高。

建议：

- 缓存 `HDC + HBITMAP + 像素缓冲`
- 仅在宠物切换或尺寸变化时重建
- 定时器回调中只做“更新像素 + UpdateLayeredWindow”

### P1. 拖拽预览路径重复做逐像素混合

`update_drag()` 在鼠标移动时：

- 创建新的 `DIBSection`
- 逐像素读取源图
- 按 alpha 阈值与灰底混合
- 再 `UpdateLayeredWindow`

这是拖拽时最重的一条热点路径。

建议：

- 在 `start_drag` 时一次性生成拖拽预览位图
- 鼠标移动时只移动窗口位置
- 不要每次移动都重新混合整张图

### P1. 资源没有宠物级缓存，切换会重复解码

`reload_pet_window()` 每次左右切换宠物时都会：

- `free_spritesheet`
- 重新走 WIC 解码
- 重新分配像素
- 重新初始化帧表

建议：

- 引入 `pet_id -> SpriteSheet` 缓存
- 最近使用宠物保留在内存中
- 选择器预览也复用同一缓存

### P1. `pet.json` 解析方式太脆弱

当前做法依赖文本扫描，容易被这些情况破坏：

- 换行方式变化
- `displayName` 前后有额外空白
- 字段顺序变化
- 转义引号
- 缩进或压缩格式不同

建议：

- 引入轻量 JSON 解析器
- 至少做严格的键值扫描和 UTF-8 处理

### P2. 全局状态过多，维护成本高

当前窗口、拖拽、预览、缓存、选择器状态几乎都挂在全局变量上。

影响：

- 函数耦合强
- 难以测试
- 多实例扩展困难
- 容易出现切换状态残留

建议：

- 把主窗口状态封装成 `AppContext`
- 把拖拽态封装成 `DragContext`
- 把选择器状态封装成 `SelectorContext`

### P2. 选择器阶段与正式运行阶段重复扫描 `petdex`

当前至少有两次 `get_all_pets()`：

- `create_pet_selector()`
- `WinMain()` 选择完成后再次读取

建议：

- 选择器返回时直接保留已有 `PetInfo *`
- 不要重新扫目录

### P2. Hook 设计偏重

全局键盘/鼠标 Hook 可用，但复杂度较高，也增加了状态同步成本。

建议：

- 键盘退出可优先考虑 `RegisterHotKey`
- 拖拽尽量优先用窗口捕获消息，而不是全局鼠标 Hook

### P3. 发布版不应携带完整调试信息

当前二进制保留了：

- 模块名
- 函数名
- 调试节

对逆向很友好，但不适合正式发布。

建议：

- release 版 strip 符号
- 单独保存调试文件

## 优先优化顺序建议

1. 缓存渲染资源，消除 `render_frame()` 的频繁 GDI 创建销毁
2. 把拖拽预览改成“开始拖拽时预生成一次”
3. 引入宠物图像缓存，避免左右切换反复解码
4. 替换 `pet.json` 的字符串扫描解析
5. 收拢全局变量，改成上下文结构体
6. 去掉重复的 `get_all_pets()` 扫描

## 如果继续往下恢复源码

下一步最适合做的是按本报告手工补出 6 个 `.c/.h` 骨架：

- `main.c`
- `pet.c`
- `sprite.c`
- `animation.c`
- `window.c`
- `selector.c`

然后依据函数名和本报告伪代码逐个填充。  
如果继续，我可以在这个目录里直接给你生成一套“可编译的近似恢复源码骨架”。
