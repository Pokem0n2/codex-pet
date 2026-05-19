#ifndef PET_WND_H
#define PET_WND_H

/* 宠物窗口过程 */
LRESULT CALLBACK PetWndProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l);

/* 选择器窗口过程 */
LRESULT CALLBACK SelWndProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l);

/* 生成新的宠物实例 */
void spawn_pet(void);

/* 强制设置焦点（跨线程安全） */
void force_set_focus(HWND hwnd);

/* 销毁所有宠物实例 */
void destroy_all_pets(void);

/* 选择变更回调 */
void on_sel_change(int idx);

/* 带行距的文本绘制 */
void draw_text_with_spacing(HDC hdc, RECT *rc, const wchar_t *text, int spacing);

#endif /* PET_WND_H */
