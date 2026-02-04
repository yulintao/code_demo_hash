#include "bit_hash.h"

#include <stdlib.h>

// 向上取整到 2 的幂，便于掩码索引
static uint32_t bit_hash_roundup_pow2(uint32_t value) {
    if (value == 0) {
        return 1;
    }

    --value;
    value |= value >> 1U;
    value |= value >> 2U;
    value |= value >> 4U;
    value |= value >> 8U;
    value |= value >> 16U;
    return value + 1U;
}

// 64 位混合哈希，降低冲突
static uint64_t bit_hash_mix64(uint64_t value) {
    value ^= value >> 33U;
    value *= 0xff51afd7ed558ccdULL;
    value ^= value >> 33U;
    value *= 0xc4ceb9fe1a85ec53ULL;
    value ^= value >> 33U;
    return value;
}

// 对外提供的 hash 计算函数
uint64_t bit_hash_compute(uint64_t key) {
    return bit_hash_mix64(key);
}

// 计算探测步长，保证为奇数
static uint32_t bit_hash_step(uint64_t hash) {
    uint32_t step = (uint32_t)((hash >> 32U) ^ hash);
    step |= 1U;
    return step;
}

// 计算锁数量（分段锁，避免过多锁开销）
static uint32_t bit_hash_lock_count(uint32_t size) {
    uint32_t count = 1U;
    while (count < size && count < 1024U) {
        count <<= 1U;
    }
    return count;
}

// 初始化内存池并建立空闲链表
int bit_hash_resource_init(bit_hash_resource_t *resource, uint32_t size, uint32_t unit_size) {
    if (!resource || size == 0 || unit_size < sizeof(int32_t)) {
        return -1;
    }

    // 申请连续内存
    resource->base = calloc(size, unit_size);
    if (!resource->base) {
        return -1;
    }

    resource->unit_size = unit_size;
    resource->size = size;
    resource->free_head = 0;

    // 初始化空闲链表（用节点首 4 字节保存 next）
    for (uint32_t i = 0; i < size; ++i) {
        int32_t *next = (int32_t *)(resource->base + (i * unit_size));
        *next = (i + 1 < size) ? (int32_t)(i + 1) : -1;
    }

    return 0;
}

// 销毁内存池
void bit_hash_resource_destroy(bit_hash_resource_t *resource) {
    if (!resource) {
        return;
    }

    free(resource->base);
    resource->base = NULL;
    resource->unit_size = 0;
    resource->size = 0;
    resource->free_head = -1;
}

// 从内存池分配节点
int32_t bit_hash_alloc(bit_hash_resource_t *resource) {
    if (!resource || resource->free_head < 0) {
        return -1;
    }

    int32_t offset = resource->free_head;
    int32_t *next = (int32_t *)(resource->base + ((uint32_t)offset * resource->unit_size));
    resource->free_head = *next;
    *next = -1;
    return offset;
}

// 释放节点回内存池
int bit_hash_free(bit_hash_resource_t *resource, int32_t offset) {
    if (!resource || offset < 0 || (uint32_t)offset >= resource->size) {
        return -1;
    }

    int32_t *next = (int32_t *)(resource->base + ((uint32_t)offset * resource->unit_size));
    *next = resource->free_head;
    resource->free_head = offset;
    return 0;
}

// 初始化哈希表及其锁
int bit_hash_init(bit_hash_table_t *table, uint32_t size, bit_hash_resource_t *resource) {
    if (!table || size == 0 || !resource) {
        return -1;
    }

    (void)resource;
    // 桶数取 2 的幂，便于掩码索引
    uint32_t bucket_count = bit_hash_roundup_pow2(size);
    table->keys = calloc(bucket_count, sizeof(uint64_t));
    if (!table->keys) {
        return -1;
    }
    table->values = calloc(bucket_count, sizeof(uint64_t));
    if (!table->values) {
        free(table->keys);
        table->keys = NULL;
        return -1;
    }
    table->states = calloc(bucket_count, sizeof(uint8_t));
    if (!table->states) {
        free(table->values);
        free(table->keys);
        table->values = NULL;
        table->keys = NULL;
        return -1;
    }

    table->size = bucket_count;
    table->mask = bucket_count - 1U;
    table->lock_count = bit_hash_lock_count(bucket_count);
    table->locks = malloc(sizeof(pthread_spinlock_t) * table->lock_count);
    if (!table->locks) {
        free(table->states);
        free(table->values);
        free(table->keys);
        table->states = NULL;
        table->values = NULL;
        table->keys = NULL;
        return -1;
    }
    for (uint32_t i = 0; i < table->lock_count; ++i) {
        if (pthread_spin_init(&table->locks[i], PTHREAD_PROCESS_PRIVATE) != 0) {
            for (uint32_t j = 0; j < i; ++j) {
                pthread_spin_destroy(&table->locks[j]);
            }
            free((void *)table->locks);
            free(table->states);
            free(table->values);
            free(table->keys);
            table->locks = NULL;
            table->states = NULL;
            table->values = NULL;
            table->keys = NULL;
            return -1;
        }
    }

    return 0;
}

// 销毁哈希表并释放资源
void bit_hash_destroy(bit_hash_table_t *table) {
    if (!table) {
        return;
    }

    // 销毁分段锁
    if (table->locks) {
        for (uint32_t i = 0; i < table->lock_count; ++i) {
            pthread_spin_destroy(&table->locks[i]);
        }
    }
    free((void *)table->locks);
    table->locks = NULL;
    free(table->states);
    table->states = NULL;
    free(table->values);
    table->values = NULL;
    free(table->keys);
    table->keys = NULL;
    table->size = 0;
    table->mask = 0;
    table->lock_count = 0;
}

// 插入或更新键值对（桶级写锁）
int bit_hash_insert(bit_hash_table_t *table, uint64_t key, uint64_t value) {
    if (!table || !table->keys || !table->values || !table->states) {
        return -1;
    }

    uint64_t hash = bit_hash_compute(key);
    uint32_t bucket = (uint32_t)hash & table->mask;
    uint32_t step = bit_hash_step(hash);

    for (int attempt = 0; attempt < 2; ++attempt) {
        uint32_t first_deleted = table->size;
        for (uint32_t i = 0; i < table->size; ++i) {
            uint32_t index = (bucket + i * step) & table->mask;
            uint32_t lock_index = index & (table->lock_count - 1U);
            pthread_spin_lock(&table->locks[lock_index]);
            uint8_t state = table->states[index];
            if (state == 1U) {
                if (table->keys[index] == key) {
                    table->values[index] = value;
                    pthread_spin_unlock(&table->locks[lock_index]);
                    return 0;
                }
                pthread_spin_unlock(&table->locks[lock_index]);
            } else if (state == 2U) {
                if (first_deleted == table->size) {
                    first_deleted = index;
                }
                pthread_spin_unlock(&table->locks[lock_index]);
            } else {
                uint32_t target = (first_deleted == table->size) ? index : first_deleted;
                if (target != index) {
                    pthread_spin_unlock(&table->locks[lock_index]);
                    uint32_t target_lock = target & (table->lock_count - 1U);
                    pthread_spin_lock(&table->locks[target_lock]);
                    if (table->states[target] != 2U) {
                        pthread_spin_unlock(&table->locks[target_lock]);
                        break;
                    }
                }
                table->keys[target] = key;
                table->values[target] = value;
                table->states[target] = 1U;
                pthread_spin_unlock(&table->locks[target & (table->lock_count - 1U)]);
                return 0;
            }
        }
        if (first_deleted != table->size) {
            uint32_t target_lock = first_deleted & (table->lock_count - 1U);
            pthread_spin_lock(&table->locks[target_lock]);
            if (table->states[first_deleted] == 2U) {
                table->keys[first_deleted] = key;
                table->values[first_deleted] = value;
                table->states[first_deleted] = 1U;
                pthread_spin_unlock(&table->locks[target_lock]);
                return 0;
            }
            pthread_spin_unlock(&table->locks[target_lock]);
        }
    }

    return -1;
}

// 删除键（桶级写锁）
int bit_hash_delete(bit_hash_table_t *table, uint64_t key) {
    if (!table || !table->keys || !table->values || !table->states) {
        return -1;
    }

    uint64_t hash = bit_hash_compute(key);
    uint32_t bucket = (uint32_t)hash & table->mask;
    uint32_t step = bit_hash_step(hash);
    for (uint32_t i = 0; i < table->size; ++i) {
        uint32_t index = (bucket + i * step) & table->mask;
        uint32_t lock_index = index & (table->lock_count - 1U);
        pthread_spin_lock(&table->locks[lock_index]);
        uint8_t state = table->states[index];
        if (state == 0U) {
            pthread_spin_unlock(&table->locks[lock_index]);
            break;
        }
        if (state == 1U && table->keys[index] == key) {
            table->states[index] = 2U;
            pthread_spin_unlock(&table->locks[lock_index]);
            return 0;
        }
        pthread_spin_unlock(&table->locks[lock_index]);
    }
    return -1;
}

// 查询键（桶级读锁）
int64_t bit_hash_search(bit_hash_table_t *table, uint64_t key) {
    if (!table || !table->keys || !table->values || !table->states) {
        return -1;
    }

    uint64_t hash = bit_hash_compute(key);
    uint32_t bucket = (uint32_t)hash & table->mask;
    uint32_t step = bit_hash_step(hash);
    for (uint32_t i = 0; i < table->size; ++i) {
        uint32_t index = (bucket + i * step) & table->mask;
        uint32_t lock_index = index & (table->lock_count - 1U);
        pthread_spin_lock(&table->locks[lock_index]);
        uint8_t state = table->states[index];
        if (state == 0U) {
            pthread_spin_unlock(&table->locks[lock_index]);
            break;
        }
        if (state == 1U && table->keys[index] == key) {
            int64_t value = (int64_t)table->values[index];
            pthread_spin_unlock(&table->locks[lock_index]);
            return value;
        }
        pthread_spin_unlock(&table->locks[lock_index]);
    }

    return -1;
}
