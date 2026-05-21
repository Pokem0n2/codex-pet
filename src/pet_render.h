#ifndef PET_RENDER_H
#define PET_RENDER_H

/* 创建宠物的渲染缓冲区（DIB Section），返回是否成功 */
int ensure_buffer(PetInst *pi);

/* 释放宠物的渲染缓冲区 */
void free_buffer(PetInst *pi);

/* 将精灵图帧缩放渲染到目标缓冲区 */
void render_scaled_frame_to(Pet *pet, int fx, int fy, BYTE *dst, int dw, int dh);

/* 使用分层窗口将 memdc 内容呈现到窗口上 */
void present_buffer(HWND hwnd, HDC memdc, int w, int h);

/* 更新预览动画帧 */
void update_preview(void);

/* 将精灵帧居中渲染到固定大小预览缓冲区 */
void render_preview_frame(Pet *pet, int fx, int fy);

#endif /* PET_RENDER_H */
