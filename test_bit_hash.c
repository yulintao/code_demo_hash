#include "bit_hash.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static void test_insert_search_update(void) {
    bit_hash_resource_t pool;
    bit_hash_table_t table;

    assert(bit_hash_resource_init(&pool, 8, sizeof(uint64_t) * 3) == 0);
    assert(bit_hash_init(&table, 4, &pool) == 0);

    assert(bit_hash_insert(&table, 10, 100) == 0);
    assert(bit_hash_insert(&table, 20, 200) == 0);
    assert(bit_hash_search(&table, 10) == 100);
    assert(bit_hash_search(&table, 20) == 200);
    assert(bit_hash_search(&table, 30) == -1);

    assert(bit_hash_insert(&table, 10, 111) == 0);
    assert(bit_hash_search(&table, 10) == 111);

    bit_hash_destroy(&table);
    bit_hash_resource_destroy(&pool);
}

static void test_delete_reuse(void) {
    bit_hash_resource_t pool;
    bit_hash_table_t table;

    assert(bit_hash_resource_init(&pool, 2, sizeof(uint64_t) * 3) == 0);
    assert(bit_hash_init(&table, 2, &pool) == 0);

    assert(bit_hash_insert(&table, 1, 10) == 0);
    assert(bit_hash_insert(&table, 2, 20) == 0);
    assert(bit_hash_insert(&table, 3, 30) == -1);

    assert(bit_hash_delete(&table, 1) == 0);
    assert(bit_hash_search(&table, 1) == -1);
    assert(bit_hash_insert(&table, 3, 30) == 0);
    assert(bit_hash_search(&table, 3) == 30);

    assert(bit_hash_delete(&table, 42) == -1);

    bit_hash_destroy(&table);
    bit_hash_resource_destroy(&pool);
}

int main(void) {
    test_insert_search_update();
    test_delete_reuse();
    printf("bit_hash tests passed.\n");
    return 0;
}
