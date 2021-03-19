#include <unordered_map>
#include "hashmap.h"

using Map = std::unordered_map<int, uint32_t>;

hashmap* hashmap_new() {
    Map* map = new Map;
    return (hashmap*)map;
}

void hashmap_set(hashmap* hm, int id, uint32_t value) {
    Map& map = *(Map*)hm;
    map[id] = value;
}

int hashmap_get(hashmap* hm, int id, uint32_t* value) {
    Map& map = *(Map*)hm;
    auto it = map.find(id);
    if(it == map.end()) {
        return 0;
    }
    if(value) *value = it->second;
    return 1;
}