#ifndef BIT_HASH_H
#define BIT_HASH_H

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <pthread.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// 内存池结构体：管理固定大小节点的连续内存
typedef struct bit_hash_resource {
    uint8_t *base;       // 内存池基地址
    uint32_t unit_size;  // 每个节点大小
    uint32_t size;       // 节点总数
    int32_t free_head;   // 空闲链表头（偏移）
} bit_hash_resource_t;

// 哈希表结构体：开放寻址 + 细粒度自旋锁
typedef struct bit_hash_table {
    uint32_t size;          // 槽位数量（2 的幂）
    uint32_t mask;          // 槽位掩码
    uint32_t lock_count;    // 锁数量（2 的幂，分段锁）
    uint64_t *keys;         // 键数组（hashkey）
    uint64_t *values;       // 值数组（mempool 偏移）
    uint8_t *states;        // 槽位状态：0空，1占用，2删除
    pthread_spinlock_t *locks; // 分段自旋锁
} bit_hash_table_t;

// 初始化内存池
int bit_hash_resource_init(bit_hash_resource_t *resource, uint32_t size, uint32_t unit_size);
// 销毁内存池
void bit_hash_resource_destroy(bit_hash_resource_t *resource);
// 从内存池分配一个节点，返回偏移
int32_t bit_hash_alloc(bit_hash_resource_t *resource);
// 释放节点到内存池
int bit_hash_free(bit_hash_resource_t *resource, int32_t offset);

// 初始化哈希表（resource 不参与哈希存储，仅用于外部 mempool）
int bit_hash_init(bit_hash_table_t *table, uint32_t size, bit_hash_resource_t *resource);
// 销毁哈希表
void bit_hash_destroy(bit_hash_table_t *table);
// 插入或更新键值对
int bit_hash_insert(bit_hash_table_t *table, uint64_t key, uint64_t value);
// 删除键
int bit_hash_delete(bit_hash_table_t *table, uint64_t key);
// 查询键，返回值或 -1
int64_t bit_hash_search(bit_hash_table_t *table, uint64_t key);

#ifdef __cplusplus
}
#endif

#endif
