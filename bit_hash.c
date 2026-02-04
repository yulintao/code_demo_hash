#include "bit_hash.h"

#include <stdlib.h>

typedef struct bit_hash_node {
    uint64_t key;
    uint64_t value;
    int32_t next;
} bit_hash_node_t;

static bit_hash_node_t *bit_hash_node_at(bit_hash_resource_t *resource, int32_t offset) {
    return (bit_hash_node_t *)(resource->base + ((uint32_t)offset * resource->unit_size));
}

int bit_hash_resource_init(bit_hash_resource_t *resource, uint32_t size, uint32_t unit_size) {
    if (!resource || size == 0 || unit_size < sizeof(bit_hash_node_t)) {
        return -1;
    }

    resource->base = calloc(size, unit_size);
    if (!resource->base) {
        return -1;
    }

    resource->unit_size = unit_size;
    resource->size = size;
    resource->free_head = 0;

    for (uint32_t i = 0; i < size; ++i) {
        bit_hash_node_t *node = bit_hash_node_at(resource, (int32_t)i);
        node->next = (i + 1 < size) ? (int32_t)(i + 1) : -1;
    }

    return 0;
}

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

int32_t bit_hash_alloc(bit_hash_resource_t *resource) {
    if (!resource || resource->free_head < 0) {
        return -1;
    }

    int32_t offset = resource->free_head;
    bit_hash_node_t *node = bit_hash_node_at(resource, offset);
    resource->free_head = node->next;
    node->next = -1;
    node->key = 0;
    node->value = 0;
    return offset;
}

int bit_hash_free(bit_hash_resource_t *resource, int32_t offset) {
    if (!resource || offset < 0 || (uint32_t)offset >= resource->size) {
        return -1;
    }

    bit_hash_node_t *node = bit_hash_node_at(resource, offset);
    node->next = resource->free_head;
    node->key = 0;
    node->value = 0;
    resource->free_head = offset;
    return 0;
}

int bit_hash_init(bit_hash_table_t *table, uint32_t size, bit_hash_resource_t *resource) {
    if (!table || size == 0 || !resource) {
        return -1;
    }

    table->buckets = malloc(sizeof(int32_t) * size);
    if (!table->buckets) {
        return -1;
    }

    table->size = size;
    table->pool = resource;
    for (uint32_t i = 0; i < size; ++i) {
        table->buckets[i] = -1;
    }

    return 0;
}

void bit_hash_destroy(bit_hash_table_t *table) {
    if (!table) {
        return;
    }

    free(table->buckets);
    table->buckets = NULL;
    table->size = 0;
    table->pool = NULL;
}

int bit_hash_insert(bit_hash_table_t *table, uint64_t key, uint64_t value) {
    if (!table || !table->buckets || !table->pool) {
        return -1;
    }

    uint32_t bucket = (uint32_t)(key % table->size);
    int32_t current_offset = table->buckets[bucket];

    while (current_offset >= 0) {
        bit_hash_node_t *node = bit_hash_node_at(table->pool, current_offset);
        if (node->key == key) {
            node->value = value;
            return 0;
        }
        current_offset = node->next;
    }

    int32_t new_offset = bit_hash_alloc(table->pool);
    if (new_offset < 0) {
        return -1;
    }

    bit_hash_node_t *new_node = bit_hash_node_at(table->pool, new_offset);
    new_node->key = key;
    new_node->value = value;
    new_node->next = table->buckets[bucket];
    table->buckets[bucket] = new_offset;
    return 0;
}

int bit_hash_delete(bit_hash_table_t *table, uint64_t key) {
    if (!table || !table->buckets || !table->pool) {
        return -1;
    }

    uint32_t bucket = (uint32_t)(key % table->size);
    int32_t current_offset = table->buckets[bucket];
    int32_t prev_offset = -1;

    while (current_offset >= 0) {
        bit_hash_node_t *node = bit_hash_node_at(table->pool, current_offset);
        if (node->key == key) {
            if (prev_offset < 0) {
                table->buckets[bucket] = node->next;
            } else {
                bit_hash_node_t *prev = bit_hash_node_at(table->pool, prev_offset);
                prev->next = node->next;
            }
            return bit_hash_free(table->pool, current_offset);
        }
        prev_offset = current_offset;
        current_offset = node->next;
    }

    return -1;
}

int64_t bit_hash_search(bit_hash_table_t *table, uint64_t key) {
    if (!table || !table->buckets || !table->pool) {
        return -1;
    }

    uint32_t bucket = (uint32_t)(key % table->size);
    int32_t current_offset = table->buckets[bucket];

    while (current_offset >= 0) {
        bit_hash_node_t *node = bit_hash_node_at(table->pool, current_offset);
        if (node->key == key) {
            return (int64_t)node->value;
        }
        current_offset = node->next;
    }

    return -1;
}
