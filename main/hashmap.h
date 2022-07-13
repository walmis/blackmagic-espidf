#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hashmap hashmap;

hashmap *hashmap_new();
void hashmap_set(hashmap *hm, int id, uint32_t value);
int hashmap_get(hashmap *hm, int id, uint32_t *value);

#ifdef __cplusplus
}
#endif