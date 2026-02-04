#ifndef BIT_HASH_H
#define BIT_HASH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bit_hash_resource {
    uint8_t *base;
    uint32_t unit_size;
    uint32_t size;
    int32_t free_head;
} bit_hash_resource_t;

typedef struct bit_hash_table {
    uint32_t size;
    int32_t *buckets;
    bit_hash_resource_t *pool;
} bit_hash_table_t;

int bit_hash_resource_init(bit_hash_resource_t *resource, uint32_t size, uint32_t unit_size);
void bit_hash_resource_destroy(bit_hash_resource_t *resource);
int32_t bit_hash_alloc(bit_hash_resource_t *resource);
int bit_hash_free(bit_hash_resource_t *resource, int32_t offset);

int bit_hash_init(bit_hash_table_t *table, uint32_t size, bit_hash_resource_t *resource);
void bit_hash_destroy(bit_hash_table_t *table);
int bit_hash_insert(bit_hash_table_t *table, uint64_t key, uint64_t value);
int bit_hash_delete(bit_hash_table_t *table, uint64_t key);
int64_t bit_hash_search(bit_hash_table_t *table, uint64_t key);

#ifdef __cplusplus
}
#endif

#endif
