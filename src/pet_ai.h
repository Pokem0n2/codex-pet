#ifndef PET_AI_H
#define PET_AI_H

/* 触发宠物播放指定动画 */
void pet_trigger_anim(HWND hwnd, int target);

/* 根据轨迹类型计算归一化坐标点 */
void ai_trajectory_point(PetInst *pi, float t, float *nx, float *ny);

/* 更新 AI 控制下的宠物位置 */
void ai_update_pos(PetInst *pi);

/* 加权随机选择下一个 AI 状态 */
int ai_weighted_state(PetInst *pi);

/* 计算指定状态和活跃度的持续时间 */
int ai_state_duration(int state, int activity);

/* 应用 AI 状态到宠物实例 */
void ai_apply_state(PetInst *pi, int state, DWORD now);

/* 为宠物选择下一个 AI 动作 */
void ai_pick_action(PetInst *pi, DWORD now);

#endif /* PET_AI_H */
