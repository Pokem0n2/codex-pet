#ifndef PET_COMMON_H
#define PET_COMMON_H

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

/* ---------- 常量 ---------- */
#define MAX_PETS        64
#define MAX_INSTANCES   256
#define CELL_W          192
#define CELL_H          208
#define PET_W           48
#define PET_H           52
#define PREV_W          96
#define PREV_H          104
#define ROWS            9

#define IDC_LISTBOX     100
#define IDT_PREVIEW     1
#define IDT_PET         2

#define AI_TIMEOUT      10000

#define PI 3.14159265f

/* 每行动画帧数（仅在 pet_data.c 中定义） */
extern const unsigned char g_frame_counts[ROWS];

/* 每帧持续时间（毫秒），仅在 pet_data.c 中定义 */
extern const short g_frame_durations[ROWS][8];

/* ---------- 数据结构 ---------- */
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
    int w, h;               /* 实例渲染尺寸 */
    /* 拖拽状态 */
    int dragging;
    int drag_anchor_x;
    int drag_anchor_y;
    int last_drag_x;
    DWORD last_drag_tick;
    float drag_speed;
    int temp_anim;
    /* 跳跃物理 */
    int jump_active;
    int jump_count;
    int jump_origin_y;
    float jump_vy;
    /* 方向性奔跑移动 */
    int move_dx;
    int move_dy;
    float move_subx;
    float move_suby;
    /* 连续奔跑循环 */
    int run_loop_mode;
    /* 自动漫跑方向（1=右，-1=左，0=无） */
    int run_dir_x;
    /* AI 状态 */
    int ai_active;
    DWORD ai_last_interaction;
    DWORD ai_next_action;
    int ai_next_state;
    float ai_traj_t;
    float ai_subx;
    float ai_suby;
    int ai_p1;
    int ai_p2;
} PetInst;

typedef struct {
    HINSTANCE hinst;
    IWICImagingFactory *wic;
    Pet *pets;
    int pet_count;
    int selected;
    HWND selector;
    HWND combobox;
    HWND desc_label;
    HWND hint_label;
    HFONT ui_font;
    HFONT desc_font;
    PetInst *instances;
    int instance_count;
    int preview_state;
    int preview_frame;
    DWORD preview_next;
    HDC prev_memdc;
    HBITMAP prev_dib;
    HBITMAP prev_oldbmp;
    BYTE *prev_pixels;
    int prev_w, prev_h;
} App;

extern App g_app;
extern HWND g_focused_pet;
extern wchar_t g_base_dir[MAX_PATH];

#endif /* PET_COMMON_H */
