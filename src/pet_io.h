#ifndef PET_IO_H
#define PET_IO_H

/* 初始化基准目录（exe 所在目录） */
void init_base_dir(void);

/* 扫描 my-pet/ 目录下所有宠物定义 */
void scan_pets(void);

/* 加载宠物精灵图，返回是否成功 */
int load_pet(Pet *pet);

/* 释放宠物精灵图内存 */
void free_pet(Pet *pet);

#endif /* PET_IO_H */
