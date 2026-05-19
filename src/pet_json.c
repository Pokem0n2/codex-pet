#include "pet_common.h"
#include "pet_json.h"

const char *json_find_value(const char *json, const char *key)
{
    const char *p = strstr(json, key);
    if (!p) return NULL;
    p += strlen(key);
    while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == ':'))
        p++;
    if (*p != '"') return NULL;
    p++;
    return p;
}

void json_copy_str(const char *src, char *dst, int max)
{
    int i = 0;
    while (src[i] && src[i] != '"' && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}
