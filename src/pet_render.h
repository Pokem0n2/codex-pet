#ifndef PET_RENDER_H
#define PET_RENDER_H

/* 创建宠物的渲染缓冲区（DIB Section），返回是否成功 */
int ensure_buffer(PetInst *pi);

/* 释放宠物的渲染缓冲区 */
void free_buffer(PetInst *pi);

/* 将精灵图中指定帧渲染到目标像素缓冲区（1:1 像素拷贝） */
void render_frame_to_buffer(Pet *pet, int fx, int fy, BYTE *dst);

/* 将精灵图帧缩放渲染到目标缓冲区 */
void render_scaled_frame_to(Pet *pet, int fx, int fy, BYTE *dst, int dw, int dh);

/* 使用分层窗口将 memdc 内容呈现到窗口上 */
void present_buffer(HWND hwnd, HDC memdc);

/* 更新预览动画帧 */
void update_preview(void);

#endif /* PET_RENDER_H */
