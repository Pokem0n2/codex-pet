#include "pet_common.h"
#include "pet_render.h"
#include "pet_ai.h"

void pet_trigger_anim(HWND hwnd, int target)
{
    PetInst *pi = (PetInst *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (!pi || !pi->alive || !pi->pet || !pi->pet->pixels) return;
    if (target == 4 && pi->jump_active && pi->jump_count >= 2)
        return; /* 已达最大二段跳，忽略 */

    /* 跳跃中触发非奔跑动画时立即落地 */
    if (pi->jump_active && target != 4 && target != 1 && target != 2) {
        pi->y = pi->jump_origin_y;
        SetWindowPos(hwnd, NULL, pi->x, pi->y, 0, 0,
            SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
    }

    pi->state = target;
    pi->frame = 0;
    pi->temp_anim = 1;
    pi->run_loop_mode = (target == 1 || target == 2) ? 1 : 0;
    pi->run_dir_x = (target == 1) ? 1 : (target == 2) ? -1 : 0;

    if (target == 4) {
        if (pi->jump_active) {
            /* 二段跳加速 */
            pi->jump_count = 2;
            pi->jump_vy = -12.3f;
        } else {
            /* 首次跳跃 */
            pi->jump_active = 1;
            pi->jump_count = 1;
            pi->jump_origin_y = pi->y;
            pi->jump_vy = -10.1f;
        }
    } else {
        if (pi->jump_active) {
            if (target == 1 || target == 2) {
                /* 左右方向键中断跳跃：停止下落，保持当前 y */
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
    render_scaled_frame_to(pi->pet, 0, target * CELL_H, pi->dib_pixels, pi->w, pi->h);
    present_buffer(hwnd, pi->memdc, pi->w, pi->h);
}

void ai_trajectory_point(PetInst *pi, float t, float *nx, float *ny)
{
    /* 统一参数曲线：通过 ai_p1/ai_p2 参数产生视觉多样性 */
    float freq_x = 1.0f + (float)(pi->ai_p1 % 4);
    float freq_y = 1.0f + (float)(pi->ai_p2 % 4);
    float ax = 0.3f + 0.2f * (float)(pi->ai_p1 % 3);
    float ay = 0.3f + 0.2f * (float)(pi->ai_p2 % 3);
    *nx = 0.5f + ax * sinf(t * freq_x * 2.0f * PI);
    *ny = 0.5f + ay * cosf(t * freq_y * 2.0f * PI);
}

void ai_update_pos(PetInst *pi)
{
    /* 自由移动仅在奔跑状态（向左/向右）下允许 */
    if (pi->state != 1 && pi->state != 2) return;

    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    int margin = 20;

    /* 以固定速率推进 t */
    pi->ai_traj_t += 0.003f;
    if (pi->ai_traj_t > 1.0f) pi->ai_traj_t -= 1.0f;

    float nx, ny;
    ai_trajectory_point(pi, pi->ai_traj_t, &nx, &ny);

    int target_x = margin + (int)(nx * (float)(sw - pi->w - 2 * margin));
    int target_y = margin + (int)(ny * (float)(sh - pi->h - 2 * margin));

    /* 逐步向目标位置移动，速度限制为 2 像素/帧 */
    int dx = target_x - pi->x;
    int dy = target_y - pi->y;
    if (dx > 2) dx = 2; else if (dx < -2) dx = -2;
    if (dy > 2) dy = 2; else if (dy < -2) dy = -2;

    pi->x += dx;
    pi->y += dy;

    if (pi->x < 0) pi->x = 0;
    if (pi->x > sw - pi->w) pi->x = sw - pi->w;
    if (pi->y < 0) pi->y = 0;
    if (pi->y > sh - pi->h) pi->y = sh - pi->h;

    SetWindowPos(pi->hwnd, NULL, pi->x, pi->y, 0, 0,
        SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
}

/* AI 状态转换：根据当前状态和随机数选择下一个状态 */
static int ai_next_state(int state, int r)
{
    /* 各状态的转移概率（cumulative thresholds） */
    static const unsigned char tbl[][8] = {
        {30,50,70,82,88,93,97,100}, /* 闲置 */
        {20,50,65,80,88,93,97,100}, /* 向右跑 */
        {20,50,65,80,88,93,97,100}, /* 向左跑 */
        {55,70,85,90,94,97,100,  0}, /* 挥手 */
        {45,65,85,90,95,98,100,  0}, /* 跳跃 */
        {75,88,98,100, 0, 0, 0,  0}, /* 失败 */
        {45,62,79,88,94,97,100,  0}, /* 等待 */
        { 0, 0, 0, 0, 0, 0, 0,  0}, /* 奔跑（不使用） */
        {35,55,75,85,92,97,100,  0}, /* 审查 */
    };
    /* 各状态的目标状态映射 */
    static const unsigned char dst[][8] = {
        {0,1,2,3,6,4,8,5},
        {0,1,2,8,4,3,6,5},
        {0,2,1,8,4,3,6,5},
        {0,1,2,8,6,4,5,0},
        {0,1,2,8,3,6,5,0},
        {0,1,2,8,0,0,0,0},
        {0,1,2,3,8,4,5,0},
        {0,0,0,0,0,0,0,0},
        {0,1,2,3,8,6,4,0},
    };
    const unsigned char *t = tbl[state], *d = dst[state];
    for (int i = 0; i < 8 && t[i]; i++)
        if (r < t[i]) return d[i];
    return 0;
}

int ai_weighted_state(PetInst *pi)
{
    int activity = pi->ai_p1 % 3;
    int run_bias = pi->ai_p2 % 3;
    int r = rand() % 100;
    int si = pi->state;

    /* 闲置状态根据活跃度调整奔跑概率 */
    if (si == 0) {
        if (r < 30 - activity * 5) return 0;
        if (r < 50 + activity * 5) return (run_bias == 0) ? 2 : 1;
        if (r < 70 + activity * 5) return (run_bias == 0) ? 1 : 2;
        return ai_next_state(0, r);
    }
    /* 奔跑状态根据活跃度调整继续奔跑概率 */
    if (si == 1 || si == 2) {
        if (r < 20) return 0;
        if (r < 50 + activity * 5) return si;
        if (r < 65 + activity * 5) return (si == 1) ? 2 : 1;
        return ai_next_state(si, r);
    }
    return ai_next_state(si, r);
}

int ai_state_duration(int state, int activity)
{
    static const struct { short mn, mx; } d[] = {
        {3000,7000},{4000,9000},{4000,9000},{2000,4000},
        {1500,3000},{2000,4000},{2000,5000},{2000,5000},{2000,4000}
    };
    int i = (unsigned)state > 8 ? 7 : state;
    int mn = d[i].mn - (activity - 1) * 800;
    int mx = d[i].mx - (activity - 1) * 800;
    if (mn < 800) mn = 800;
    if (mx < mn + 500) mx = mn + 500;
    return mn + rand() % (mx - mn);
}

void ai_apply_state(PetInst *pi, int state, DWORD now)
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

    /* 切换状态时重置子像素累加器，防止漂移 */
    pi->ai_subx = 0.0f;
    pi->ai_suby = 0.0f;

    render_scaled_frame_to(pi->pet, 0, state * CELL_H, pi->dib_pixels, pi->w, pi->h);
    present_buffer(pi->hwnd, pi->memdc, pi->w, pi->h);
}

void ai_pick_action(PetInst *pi, DWORD now)
{
    int next = ai_weighted_state(pi);
    int activity = pi->ai_p1 % 3;

    /* 从非奔跑切换到奔跑时，根据当前位置初始化 t */
    if ((pi->state != 1 && pi->state != 2) && (next == 1 || next == 2)) {
        int sw = GetSystemMetrics(SM_CXSCREEN);
        int margin = 20;
        float scale_x = (float)(sw - pi->w - 2 * margin);
        if (scale_x > 0.0f) {
            pi->ai_traj_t = (float)(pi->x - margin) / scale_x;
            if (pi->ai_traj_t < 0.0f) pi->ai_traj_t = 0.0f;
            if (pi->ai_traj_t > 1.0f) pi->ai_traj_t = 1.0f;
        }
    }

    pi->ai_next_action = now + ai_state_duration(next, activity);

    /* 当前正在奔跑时，优雅停止并推迟状态切换直到奔跑周期自然结束 */
    if (pi->state == 1 || pi->state == 2) {
        pi->run_loop_mode = 0;
        pi->ai_next_state = next;
    } else {
        pi->ai_next_state = -1;
        ai_apply_state(pi, next, now);
    }
}
