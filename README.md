# CodeX-Pet

Windows 桌面宠物程序。支持多宠物生成、AI 自动行为、键盘交互控制，最终 exe 仅 10KB。

## 功能

- 从 `my-pet/` 目录加载宠物精灵图
- 宠物选择器窗口（预览动画、显示名称和描述）
- 回车生成宠物、ESC 退出、方向键移动
- 空格跳跃（支持二段跳）、W 挥手、E 失败、Q 等待、R 审查
- AI 自动行为：站立、行走、奔跑等状态随机切换
- 10 种轨迹类型（直线、多边形、圆弧、李萨如、摆线等）
- 分层透明窗口（UpdateLayeredWindow + AlphaBlend）

## 构建方法

### MSVC（推荐，产出 10KB exe）

需要：
- Visual Studio 2022 Build Tools（MSVC v144）
- UPX 5.x（放在 PATH 中或与项目同级）

```
build_msvc.bat
```

一键完成：编译 x86 无 CRT 版本 -> 链接 -> UPX 压缩 -> 产出 `codex-pet.exe`（10,240 字节）。

### MinGW（产出 ~21KB exe）

需要：
- x86_64-w64-mingw32-gcc 14.x
- UPX（可选）

```
make          # 编译
make upx      # 编译 + UPX 压缩
```

## 项目结构

```
codex-pet/
├── build_msvc.bat        # MSVC 一键构建脚本
├── Makefile              # MinGW 构建脚本
├── codex-pet.exe         # 最终产出（10KB）
├── my-pet/               # 宠物资源目录
│   └── <pet-name>/
│       ├── config.json   # 宠物配置（帧数、动画速度）
│       └── sprite.png    # 精灵图
└── src/
    ├── main.c            # 入口点、消息循环、资源管理
    ├── crt_repl.c        # CRT 函数替换（Win32 API + x87 FPU）
    ├── pet_common.h      # 公共类型定义和常量
    ├── pet_ai.c/h        # AI 状态机、轨迹系统
    ├── pet_io.c/h        # 目录扫描、WIC 图像加载
    ├── pet_json.c/h      # JSON 解析
    ├── pet_render.c/h    # 帧渲染、缩放、合成
    ├── pet_wnd.c/h       # 窗口过程、宠物生成
    └── pet_data.c        # 共享常量数据（帧数、帧时长）
```

## 技术细节

### 体积优化策略

| 技术 | 效果 |
|------|------|
| 32 位 x86 构建 | UPX 解压器仅 ~2KB（x64 需 ~8KB） |
| 完全消除 CRT 依赖 | 无 ucrt/vcruntime DLL 导入 |
| x87 FPU 数学函数 | 用 `fsin`/`fcos`/`fsqrt` 替代数学库 |
| PE 节合并 | .rdata 合并到 .text，减少对齐浪费 |
| /FIXED 链接 | 去掉 .reloc 段（省 ~1KB） |
| 动态内存分配 | 大数组用 calloc 而非静态数组 |
| UPX --ultra-brute | 最终压缩 15,872 -> 10,240 字节 |

### CRT 替换（crt_repl.c）

用 Win32 API 和 x87 FPU 内联汇编替代所有 CRT 函数，使 exe 仅依赖 5 个系统 DLL：

`kernel32.dll` `user32.dll` `gdi32.dll` `ole32.dll` `msimg32.dll`

## 操作说明

| 按键 | 功能 |
|------|------|
| Enter | 生成宠物 |
| Space | 跳跃（空中再按 = 二段跳） |
| Left/Right | 向左/右奔跑 |
| Up/Down | 向上/下奔跑 |
| W | 挥手动画 |
| E | 失败动画 |
| Q | 等待动画 |
| R | 审查动画 |
| Esc | 关闭焦点宠物 / 退出程序 |
