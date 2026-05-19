#ifndef PET_JSON_H
#define PET_JSON_H

/* 在 JSON 字符串中查找指定键对应的值（跳过键名和引号） */
const char *json_find_value(const char *json, const char *key);

/* 从 src 复制字符串到 dst，遇引号或达到上限时停止 */
void json_copy_str(const char *src, char *dst, int max);

#endif /* PET_JSON_H */
