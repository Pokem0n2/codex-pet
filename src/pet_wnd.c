#include "pet_common.h"
#include "pet_render.h"
#include "pet_ai.h"
#include "pet_wnd.h"
#include "pet_io.h"

void spawn_pet(void)
{
    if (g_app.selected < 0 || g_app.selected >= g_app.pet_count) return;
    Pet *p = &g_app.pets[g_app.selected];
    if (!load_pet(p)) return;
    if (g_app.instance_count >= MAX_INSTANCES) return;

    int pw = g_app.prev_w, ph = g_app.prev_h;
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    int x = rand() % (sw - pw);
    int y = rand() % (sh - ph);

    int idx = -1;
    for (int i = 0; i < MAX_INSTANCES; i++) {
        if (!g_app.instances[i].alive) { idx = i; break; }
    }
    if (idx < 0) return;

    PetInst *pi = &g_app.instances[idx];
    memset(pi, 0, sizeof(PetInst));
    pi->pet = p;
    pi->w = pw;
    pi->h = ph;
    pi->x = x;
    pi->y = y;
    pi->prev_x = x;
    pi->alive = 1;

    HWND hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
        L"PetWindow", p->name, WS_POPUP, x, y, pw, ph, NULL, NULL, g_app.hinst, pi);
    if (!hwnd) { pi->alive = 0; return; }

    g_app.instance_count++;
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);

    /* 立即渲染第一帧 */
    ensure_buffer(pi);
    render_scaled_frame_to(p, 0, 0, pi->dib_pixels, pi->w, pi->h);
    present_buffer(hwnd, pi->memdc, pi->w, pi->h);

    /* 保持焦点在组合框，确保方向键和回车键继续工作 */
    SetFocus(g_app.combobox);
}

void force_set_focus(HWND hwnd)
{
    SetFocus(hwnd);
}

void destroy_all_pets(void)
{
    for (int i = 0; i < MAX_INSTANCES; i++) {
        if (g_app.instances[i].alive && g_app.instances[i].hwnd)
            DestroyWindow(g_app.instances[i].hwnd);
    }
}

/* 重置宠物的临时动画和移动状态到初始值 */
static void reset_pet_state(PetInst *pi)
{
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
}

/* 完成临时动画后回到闲置状态，处理 AI 状态切换 */
static void finish_temp_anim(PetInst *pi, DWORD now)
{
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

LRESULT CALLBACK PetWndProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l)
{
    PetInst *pi = (PetInst *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTW *cs = (CREATESTRUCTW *)l;
        pi = (PetInst *)cs->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)pi);
        pi->hwnd = hwnd;
        ensure_buffer(pi);
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
        DWORD dt_tick = now - pi->last_drag_tick;
        if (dt_tick == 0) dt_tick = 1;

        pi->x = new_x;
        pi->y = new_y;
        if (pi->y < 0) pi->y = 0;
        pi->last_drag_x = new_x;
        pi->last_drag_tick = now;

        /* 拖拽方向：仅水平分量影响动画状态 */
        if (dx < 0 && pi->state != 2) {
            pi->state = 2; pi->frame = 0;
            reset_pet_state(pi);
        } else if (dx > 0 && pi->state != 1) {
            pi->state = 1; pi->frame = 0;
            reset_pet_state(pi);
        }
        pi->drag_speed = (float)(dx < 0 ? -dx : dx) / (float)dt_tick;

        SetWindowPos(hwnd, NULL, new_x, new_y, 0, 0,
            SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);

        /* 拖拽时实时推进帧 */
        if (now >= pi->next_tick) {
            pi->frame++;
            if (pi->frame >= g_frame_counts[pi->state]) pi->frame = 0;
            int base = g_frame_durations[pi->state][pi->frame];
            float eff = pi->drag_speed;
            if (now - pi->last_drag_tick > 50) eff = 0.0f;
            int adj = (int)(base / (1.0f + eff * 3.0f));
            if (adj < 15) adj = 15;
            pi->next_tick = now + adj;
        }
        render_scaled_frame_to(pi->pet, pi->frame * CELL_W, pi->state * CELL_H,
            pi->dib_pixels, pi->w, pi->h);
        present_buffer(hwnd, pi->memdc, pi->w, pi->h);
        return 0;
    }
    case WM_LBUTTONUP: {
        if (!pi || !pi->dragging) return 0;
        pi->dragging = 0;
        ReleaseCapture();
        pi->state = 0;
        pi->frame = 0;
        reset_pet_state(pi);
        pi->next_tick = GetTickCount() + g_frame_durations[0][0];
        /* 立即渲染闲置帧 */
        render_scaled_frame_to(pi->pet, 0, 0, pi->dib_pixels, pi->w, pi->h);
        present_buffer(hwnd, pi->memdc, pi->w, pi->h);
        return 0;
    }
    case WM_TIMER: {
        if (!pi || !pi->alive || !pi->pet || !pi->pet->pixels) return 0;
        DWORD now = GetTickCount();
        int needs_render = 0;

        /* 跳跃物理：每帧更新以保持平滑 */
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

        /* 临时奔跑移动（水平或垂直） */
        if (pi->temp_anim && (pi->state == 1 || pi->state == 2)) {
            int moved = 0;
            int dx = pi->move_dx;
            int dy = pi->move_dy;
            /* 松开按键后保持自动奔跑一整个周期 */
            if (dx == 0 && dy == 0 && pi->run_dir_x != 0)
                dx = pi->run_dir_x;

            /* 对角线移动时归一化速度为 1 像素/帧 */
            if (dx != 0 && dy != 0) {
                float len = sqrtf((float)(dx * dx + dy * dy));
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
                if (pi->x > sw - pi->w) pi->x = sw - pi->w;
                moved = 1;
            }
            if (dy != 0) {
                pi->y += dy;
                int sh = GetSystemMetrics(SM_CYSCREEN);
                if (pi->y < 0) pi->y = 0;
                if (pi->y > sh - pi->h) pi->y = sh - pi->h;
                moved = 1;
            }
            if (moved) {
                SetWindowPos(hwnd, NULL, pi->x, pi->y, 0, 0,
                    SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
                needs_render = 1;
            }
        }

        /* AI：无操作超时后自动激活 */
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

        /* 帧推进 */
        if (now >= pi->next_tick) {
            pi->frame++;
            if (pi->frame >= g_frame_counts[pi->state]) {
                if (pi->temp_anim) {
                    if ((pi->state == 1 || pi->state == 2) && pi->run_loop_mode) {
                        pi->frame = 0; /* 连续循环 */
                    } else {
                        finish_temp_anim(pi, now); /* 动画结束，回到闲置 */
                    }
                } else {
                    pi->frame = 0;
                }
            }

            int base;
            if ((pi->state == 1 || pi->state == 2) && pi->run_loop_mode)
                base = 120; /* 连续奔跑时统一速度 */
            else
                base = g_frame_durations[pi->state][pi->frame];

            int adj = base;
            if (pi->dragging) {
                float eff = pi->drag_speed;
                if (now - pi->last_drag_tick > 50) eff = 0.0f;
                adj = (int)(base / (1.0f + eff * 3.0f));
                if (adj < 15) adj = 15;
            }
            pi->next_tick = now + adj;
            needs_render = 1;
        }

        /* 根据实际移动方向切换奔跑朝向 */
        if ((pi->state == 1 || pi->state == 2) && pi->prev_x != pi->x) {
            if (pi->x > pi->prev_x && pi->state != 1) pi->state = 1;
            else if (pi->x < pi->prev_x && pi->state != 2) pi->state = 2;
        }
        pi->prev_x = pi->x;

        if (needs_render) {
            render_scaled_frame_to(pi->pet, pi->frame * CELL_W, pi->state * CELL_H,
                pi->dib_pixels, pi->w, pi->h);
            present_buffer(hwnd, pi->memdc, pi->w, pi->h);
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

void draw_text_with_spacing(HDC hdc, RECT *rc, const wchar_t *text)
{
    /* 使用系统 DrawText 实现自动换行，比手写循环更紧凑 */
    FillRect(hdc, rc, (HBRUSH)(COLOR_BTNFACE + 1));
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT));
    DrawTextW(hdc, text, -1, rc, DT_LEFT | DT_WORDBREAK | DT_EDITCONTROL);
}

void on_sel_change(int idx)
{
    if (idx < 0 || idx >= g_app.pet_count) return;
    g_app.selected = idx;
    load_pet(&g_app.pets[idx]);
    SetWindowTextW(g_app.desc_label, g_app.pets[idx].desc);
    InvalidateRect(g_app.desc_label, NULL, TRUE);
    g_app.preview_state = 7;
    g_app.preview_frame = 0;
    g_app.preview_next = GetTickCount() + 120;
    /* 立即渲染预览首帧，避免空白期 */
    if (g_app.prev_pixels) {
        Pet *p = &g_app.pets[idx];
        if (p && p->pixels) {
            render_preview_frame(p, 0, 7 * CELL_H);
        }
    }
    if (g_app.selector) {
        RECT rc = {15, 15, 15 + PREV_W, 15 + PREV_H};
        InvalidateRect(g_app.selector, &rc, TRUE);
    }
}

LRESULT CALLBACK SelWndProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l)
{
    switch (msg) {
    case WM_CREATE: {
        g_app.ui_font = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, NULL);

        g_app.combobox = CreateWindowExW(0, L"COMBOBOX", NULL,
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
            130, 15, 150, 200, hwnd, (HMENU)IDC_LISTBOX, g_app.hinst, NULL);
        for (int i = 0; i < g_app.pet_count; i++)
            SendMessageW(g_app.combobox, CB_ADDSTRING, 0, (LPARAM)g_app.pets[i].name);
        SendMessageW(g_app.combobox, WM_SETFONT, (WPARAM)g_app.ui_font, TRUE);

        g_app.desc_font = CreateFontW(-11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, NULL);
        g_app.desc_label = CreateWindowExW(0, L"STATIC", L"Select a pet",
            WS_CHILD | WS_VISIBLE | SS_OWNERDRAW,
            130, 40, 150, 60, hwnd, NULL, g_app.hinst, NULL);
        SendMessageW(g_app.desc_label, WM_SETFONT, (WPARAM)g_app.desc_font, TRUE);

        g_app.hint_label = CreateWindowExW(0, L"STATIC", L"ENTER: spawn    ESC: exit",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            130, 100, 150, 20, hwnd, NULL, g_app.hinst, NULL);
        SendMessageW(g_app.hint_label, WM_SETFONT, (WPARAM)g_app.ui_font, TRUE);

        if (g_app.pet_count > 0) {
            SendMessageW(g_app.combobox, CB_SETCURSEL, 0, 0);
            on_sel_change(0);
            RECT rc = {15, 15, 15 + g_app.prev_w, 15 + g_app.prev_h};
            InvalidateRect(hwnd, &rc, TRUE);
        }
        SetFocus(g_app.combobox);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(w) == IDC_LISTBOX && HIWORD(w) == CBN_SELCHANGE)
            on_sel_change((int)SendMessageW(g_app.combobox, CB_GETCURSEL, 0, 0));
        return 0;
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT *dis = (DRAWITEMSTRUCT *)l;
        if (dis->hwndItem == g_app.desc_label) {
            HFONT hFont = (HFONT)SendMessageW(dis->hwndItem, WM_GETFONT, 0, 0);
            HFONT oldFont = (HFONT)SelectObject(dis->hDC, hFont);
            wchar_t text[256];
            GetWindowTextW(dis->hwndItem, text, 256);
            draw_text_with_spacing(dis->hDC, &dis->rcItem, text);
            SelectObject(dis->hDC, oldFont);
            return TRUE;
        }
        break;
    }
    case WM_TIMER:
        update_preview();
        {
            RECT rc = {15, 15, 15 + PREV_W, 15 + PREV_H};
            InvalidateRect(hwnd, &rc, FALSE);
        }
        return 0;
    case WM_ERASEBKGND:
        return DefWindowProcW(hwnd, msg, w, l);
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        /* 清空预览区背景，防止旧帧残留 */
        RECT rc_preview = {15, 15, 15 + PREV_W, 15 + PREV_H};
        FillRect(hdc, &rc_preview, (HBRUSH)(COLOR_BTNFACE + 1));
        if (g_app.selected >= 0 && g_app.selected < g_app.pet_count && g_app.prev_memdc) {
            BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
            AlphaBlend(hdc, 15, 15, PREV_W, PREV_H, g_app.prev_memdc, 0, 0, PREV_W, PREV_H, bf);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_MOUSEWHEEL: {
        int d = (short)HIWORD(w) > 0 ? 4 : -4;
        int nw = g_app.prev_w + d;
        if (nw < 48) nw = 48;
        if (nw > PREV_W) nw = PREV_W;
        int nh = nw * CELL_H / CELL_W;
        if (nw == g_app.prev_w && nh == g_app.prev_h) return 0;
        g_app.prev_w = nw;
        g_app.prev_h = nh;
        /* 立即重绘居中预览 */
        if (g_app.selected >= 0 && g_app.selected < g_app.pet_count) {
            Pet *p = &g_app.pets[g_app.selected];
            if (p && p->pixels) {
                render_preview_frame(p, g_app.preview_frame * CELL_W, 7 * CELL_H);
            }
        }
        RECT rc = {15, 15, 15 + PREV_W, 15 + PREV_H};
        InvalidateRect(hwnd, &rc, TRUE);
        return 0;
    }
    case WM_DESTROY:
        destroy_all_pets();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, w, l);
}
