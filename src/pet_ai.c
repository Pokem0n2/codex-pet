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
    *nx = 0.5f; *ny = 0.5f;
    float a, f;
    int n, side;
    switch (pi->ai_traj_type) {
    case TRAJ_LINE:
        *nx = t;
        *ny = 0.3f + t * 0.4f;
        break;
    case TRAJ_RECT:
    case TRAJ_TRI:
    case TRAJ_POLY: {
        /* 统一多边形轨迹：RECT=4边, TRI=3边, POLY=3-7边 */
        if (pi->ai_traj_type == TRAJ_RECT) n = 4;
        else if (pi->ai_traj_type == TRAJ_TRI) n = 3;
        else n = 3 + (pi->ai_p1 % 5);
        float lt = t * n;
        side = (int)lt;
        f = lt - side;
        float a1 = side * 2.0f * PI / n;
        float a2 = (side + 1) * 2.0f * PI / n;
        float c1 = cosf(a1), s1 = sinf(a1);
        float c2 = cosf(a2), s2 = sinf(a2);
        *nx = (0.5f + 0.5f * c1) + ((0.5f + 0.5f * c2) - (0.5f + 0.5f * c1)) * f;
        *ny = (0.5f + 0.5f * s1) + ((0.5f + 0.5f * s2) - (0.5f + 0.5f * s1)) * f;
        break;
    }
    case TRAJ_CIRCLE:
    case TRAJ_ELLIPSE:
    case TRAJ_ARC: {
        /* 统一圆弧类轨迹 */
        a = t * (pi->ai_traj_type == TRAJ_ARC ? PI : 2.0f * PI);
        float rx = 0.5f;
        float ry = (pi->ai_traj_type == TRAJ_ELLIPSE) ? 0.3f : 0.5f;
        *nx = 0.5f + rx * cosf(a);
        *ny = 0.5f + ry * sinf(a);
        break;
    }
    case TRAJ_FIGURE8:
        a = t * 2.0f * PI;
        *nx = 0.5f + 0.4f * sinf(a);
        *ny = 0.5f + 0.4f * sinf(2.0f * a);
        break;
    case TRAJ_ZIGZAG: {
        int seg = (int)(t * 8.0f);
        f = t * 8.0f - seg;
        *nx = (seg + f) / 8.0f;
        *ny = (seg % 2 == 0) ? f : 1.0f - f;
        break;
    }
    case TRAJ_CYCLE: {
        float theta = t * 4.0f * PI;
        *nx = (theta - sinf(theta)) / (4.0f * PI);
        *ny = (1.0f - cosf(theta)) / 2.0f;
        break;
    }
    }
}

void ai_update_pos(PetInst *pi)
{
    /* 自由移动仅在奔跑状态（向左/向右）下允许 */
    if (pi->state != 1 && pi->state != 2) return;

    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    int margin = 20;
    float scale_x = (float)(sw - pi->w - 2 * margin);
    float scale_y = (float)(sh - pi->h - 2 * margin);

    float t = pi->ai_traj_t;
    float nx, ny;
    ai_trajectory_point(pi, t, &nx, &ny);

    /* 计算轨迹切线方向 */
    float dt = 0.0001f;
    float nx2, ny2;
    ai_trajectory_point(pi, t + dt, &nx2, &ny2);
    float dir_x = (nx2 - nx) * scale_x;
    float dir_y = (ny2 - ny) * scale_y;
    float dir_len = sqrtf(dir_x * dir_x + dir_y * dir_y);
    if (dir_len < 0.001f) dir_len = 0.001f;

    /* 按弧长比例推进 t，使速度约为 1 像素/帧 */
    pi->ai_traj_t += dt / dir_len;
    if (pi->ai_traj_t > 1.0f) pi->ai_traj_t -= 1.0f;

    /* 沿切线方向移动 1 像素，用子像素累加器保证平滑 */
    pi->ai_subx += dir_x / dir_len;
    pi->ai_suby += dir_y / dir_len;

    int ix = (int)pi->ai_subx;
    int iy = (int)pi->ai_suby;
    pi->ai_subx -= (float)ix;
    pi->ai_suby -= (float)iy;

    int old_x = pi->x;
    int old_y = pi->y;
    pi->x += ix;
    pi->y += iy;

    /* 边界限制 */
    if (pi->x < 0) pi->x = 0;
    if (pi->x > sw - pi->w) pi->x = sw - pi->w;
    if (pi->y < 0) pi->y = 0;
    if (pi->y > sh - pi->h) pi->y = sh - pi->h;

    /* 完全被边界阻挡时快进 t，避免抖动 */
    if (pi->x == old_x && pi->y == old_y && (ix != 0 || iy != 0)) {
        pi->ai_subx = 0.0f;
        pi->ai_suby = 0.0f;
        pi->ai_traj_t += dt * 5.0f / dir_len;
        if (pi->ai_traj_t > 1.0f) pi->ai_traj_t -= 1.0f;
        return;
    }

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
    int base_min, base_max;
    switch (state) {
    case 0:  base_min = 3000; base_max = 7000; break; /* 闲置 */
    case 1:
    case 2:  base_min = 4000; base_max = 9000; break; /* 奔跑 */
    case 3:  base_min = 2000; base_max = 4000; break; /* 挥手 */
    case 4:  base_min = 1500; base_max = 3000; break; /* 跳跃 */
    case 5:  base_min = 2000; base_max = 4000; break; /* 失败 */
    case 6:  base_min = 2000; base_max = 5000; break; /* 等待 */
    case 8:  base_min = 2000; base_max = 4000; break; /* 审查 */
    default: base_min = 2000; base_max = 5000; break;
    }
    /* 活跃宠物动作更短，慵懒宠物停留更久 */
    int adj = (activity - 1) * 800;
    base_min -= adj;
    base_max -= adj;
    if (base_min < 800) base_min = 800;
    if (base_max < base_min + 500) base_max = base_min + 500;
    return base_min + rand() % (base_max - base_min);
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

    /* 轨迹：80% 保持当前轨迹，20% 切换 */
    if ((rand() % 100) < 20) {
        pi->ai_traj_type = rand() % 10;
        pi->ai_traj_t = (float)rand() / (float)RAND_MAX;
    }

    /* 从非奔跑切换到奔跑时，根据当前位置初始化 t 以减少跳跃 */
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
