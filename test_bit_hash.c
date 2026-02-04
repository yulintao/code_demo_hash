#include "bit_hash.h"

#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>

// 基本插入、查询、更新测试
static void test_insert_search_update(void) {
    bit_hash_resource_t pool;
    bit_hash_table_t table;

    assert(bit_hash_resource_init(&pool, 8, 64) == 0);
    assert(bit_hash_init(&table, 4, &pool) == 0);

    int32_t offset_a = bit_hash_alloc(&pool);
    int32_t offset_b = bit_hash_alloc(&pool);
    assert(offset_a >= 0);
    assert(offset_b >= 0);

    assert(bit_hash_insert(&table, 10, (uint64_t)offset_a) == 0);
    assert(bit_hash_insert(&table, 20, (uint64_t)offset_b) == 0);
    assert(bit_hash_search(&table, 10) == offset_a);
    assert(bit_hash_search(&table, 20) == offset_b);
    assert(bit_hash_search(&table, 30) == -1);

    assert(bit_hash_insert(&table, 10, (uint64_t)offset_b) == 0);
    assert(bit_hash_search(&table, 10) == offset_b);

    assert(bit_hash_free(&pool, offset_a) == 0);
    assert(bit_hash_free(&pool, offset_b) == 0);
    bit_hash_destroy(&table);
    bit_hash_resource_destroy(&pool);
}

// 删除与内存池复用测试
static void test_delete_reuse(void) {
    bit_hash_resource_t pool;
    bit_hash_table_t table;

    assert(bit_hash_resource_init(&pool, 2, 64) == 0);
    assert(bit_hash_init(&table, 2, &pool) == 0);

    int32_t offset_a = bit_hash_alloc(&pool);
    int32_t offset_b = bit_hash_alloc(&pool);
    assert(offset_a >= 0);
    assert(offset_b >= 0);

    assert(bit_hash_insert(&table, 1, (uint64_t)offset_a) == 0);
    assert(bit_hash_insert(&table, 2, (uint64_t)offset_b) == 0);
    assert(bit_hash_insert(&table, 3, 30) == -1);

    assert(bit_hash_delete(&table, 1) == 0);
    assert(bit_hash_search(&table, 1) == -1);
    assert(bit_hash_free(&pool, offset_a) == 0);
    offset_a = bit_hash_alloc(&pool);
    assert(offset_a >= 0);
    assert(bit_hash_insert(&table, 3, (uint64_t)offset_a) == 0);
    assert(bit_hash_search(&table, 3) == offset_a);

    assert(bit_hash_delete(&table, 42) == -1);

    assert(bit_hash_free(&pool, offset_a) == 0);
    assert(bit_hash_free(&pool, offset_b) == 0);
    bit_hash_destroy(&table);
    bit_hash_resource_destroy(&pool);
}

// 多线程插入任务参数
typedef struct {
    bit_hash_table_t *table;
    uint64_t start_key;
    uint64_t count;
    int32_t *offsets;
} insert_task_t;

// 线程入口：批量插入
static void *insert_worker(void *arg) {
    insert_task_t *task = (insert_task_t *)arg;
    for (uint64_t i = 0; i < task->count; ++i) {
        bit_hash_insert(task->table, task->start_key + i, (uint64_t)task->offsets[i]);
    }
    return NULL;
}

// 多线程并发插入测试
static void test_multithreaded_insert(void) {
    bit_hash_resource_t pool;
    bit_hash_table_t table;
    const uint64_t inserts_per_thread = 100;

    assert(bit_hash_resource_init(&pool, 512, 64) == 0);
    assert(bit_hash_init(&table, 256, &pool) == 0);

    insert_task_t task1 = { .table = &table, .start_key = 1000, .count = inserts_per_thread };
    insert_task_t task2 = { .table = &table, .start_key = 2000, .count = inserts_per_thread };
    int32_t offsets1[100];
    int32_t offsets2[100];
    for (uint64_t i = 0; i < inserts_per_thread; ++i) {
        offsets1[i] = bit_hash_alloc(&pool);
        offsets2[i] = bit_hash_alloc(&pool);
        assert(offsets1[i] >= 0);
        assert(offsets2[i] >= 0);
    }
    task1.offsets = offsets1;
    task2.offsets = offsets2;
    pthread_t thread1;
    pthread_t thread2;

    assert(pthread_create(&thread1, NULL, insert_worker, &task1) == 0);
    assert(pthread_create(&thread2, NULL, insert_worker, &task2) == 0);
    assert(pthread_join(thread1, NULL) == 0);
    assert(pthread_join(thread2, NULL) == 0);

    for (uint64_t i = 0; i < inserts_per_thread; ++i) {
        assert(bit_hash_search(&table, 1000 + i) >= 0);
        assert(bit_hash_search(&table, 2000 + i) >= 0);
    }

    for (uint64_t i = 0; i < inserts_per_thread; ++i) {
        assert(bit_hash_free(&pool, offsets1[i]) == 0);
        assert(bit_hash_free(&pool, offsets2[i]) == 0);
    }
    bit_hash_destroy(&table);
    bit_hash_resource_destroy(&pool);
}

// 测试入口
int main(void) {
    test_insert_search_update();
    test_delete_reuse();
    test_multithreaded_insert();
    printf("bit_hash tests passed.\n");
    return 0;
}
