#include "runtime.h"
#include "platform.h"

#if defined(NEWOS_RUNTIME_THREAD_SAFE_ALLOC) && NEWOS_RUNTIME_THREAD_SAFE_ALLOC && defined(NEWOS_HAVE_PTHREAD) && NEWOS_HAVE_PTHREAD
#include <pthread.h>
#define RT_ALLOC_THREAD_SAFE 1
#else
#define RT_ALLOC_THREAD_SAFE 0
#endif

static volatile int rt_alloc_atomic_lock;

/*
 * Keep these implementations strictly byte-wise and volatile-backed so the
 * compiler does not fold them back into builtin memcpy or memset calls.
 * That would recurse here in hosted optimized builds.
 */
void *memcpy(void *dst, const void *src, size_t count) {
    volatile unsigned char *out = (volatile unsigned char *)dst;
    const volatile unsigned char *in = (const volatile unsigned char *)src;
    size_t i;

    for (i = 0; i < count; ++i) {
        out[i] = in[i];
    }

    return dst;
}

int memcmp(const void *left, const void *right, size_t count) {
    const unsigned char *left_bytes = (const unsigned char *)left;
    const unsigned char *right_bytes = (const unsigned char *)right;
    size_t i;

    for (i = 0; i < count; ++i) {
        if (left_bytes[i] != right_bytes[i]) {
            return (int)left_bytes[i] - (int)right_bytes[i];
        }
    }
    return 0;
}

void *memset(void *buffer, int byte_value, size_t count) {
    volatile unsigned char *out = (volatile unsigned char *)buffer;
    unsigned char value = (unsigned char)byte_value;
    size_t i;

    for (i = 0; i < count; ++i) {
        out[i] = value;
    }

    return buffer;
}

void *memmove(void *dst, const void *src, size_t count) {
    volatile unsigned char *out = (volatile unsigned char *)dst;
    const volatile unsigned char *in = (const volatile unsigned char *)src;
    size_t i;

    if (out == in) {
        return dst;
    }

    if (out < in) {
        for (i = 0; i < count; ++i) {
            out[i] = in[i];
        }
    } else {
        for (i = count; i > 0; --i) {
            out[i - 1] = in[i - 1];
        }
    }

    return dst;
}

void rt_memset(void *buffer, int byte_value, size_t count) {
    (void)memset(buffer, byte_value, count);
}

typedef struct RtAllocBlock RtAllocBlock;

struct RtAllocBlock {
    size_t size;
    int free;
    RtAllocBlock *next;
};

static RtAllocBlock *rt_alloc_head;
static RtAllocBlock *rt_alloc_tail;

#if RT_ALLOC_THREAD_SAFE
static pthread_mutex_t rt_alloc_mutex = PTHREAD_MUTEX_INITIALIZER;

static void rt_alloc_lock(void) {
    (void)pthread_mutex_lock(&rt_alloc_mutex);
}

static void rt_alloc_unlock(void) {
    (void)pthread_mutex_unlock(&rt_alloc_mutex);
}
#else
static void rt_alloc_lock(void) {
    while (__atomic_exchange_n(&rt_alloc_atomic_lock, 1, __ATOMIC_ACQUIRE) != 0) {
        while (__atomic_load_n(&rt_alloc_atomic_lock, __ATOMIC_RELAXED) != 0) {
        }
    }
}

static void rt_alloc_unlock(void) {
    __atomic_store_n(&rt_alloc_atomic_lock, 0, __ATOMIC_RELEASE);
}
#endif

static size_t rt_alloc_align(size_t value) {
    const size_t alignment = sizeof(void *) * 2U;
    return (value + alignment - 1U) & ~(alignment - 1U);
}

static size_t rt_alloc_page_align(size_t value) {
    const size_t page_size = 4096U;
    return (value + page_size - 1U) & ~(page_size - 1U);
}

static void rt_alloc_split(RtAllocBlock *block, size_t size) {
    RtAllocBlock *next;
    if (block->size <= size + sizeof(RtAllocBlock) + sizeof(void *) * 2U) return;
    next = (RtAllocBlock *)((unsigned char *)(block + 1) + size);
    next->size = block->size - size - sizeof(RtAllocBlock);
    next->free = 1;
    next->next = block->next;
    block->size = size;
    block->next = next;
    if (rt_alloc_tail == block) rt_alloc_tail = next;
}

static RtAllocBlock *rt_alloc_find_free(size_t size) {
    RtAllocBlock *block = rt_alloc_head;
    while (block != 0) {
        if (block->free && block->size >= size) return block;
        block = block->next;
    }
    return 0;
}

static RtAllocBlock *rt_alloc_grow(size_t size) {
    size_t mapping_size;
    void *mapped;
    RtAllocBlock *block;
    if (size > ((size_t)-1) - sizeof(RtAllocBlock)) return 0;
    mapping_size = rt_alloc_page_align(sizeof(RtAllocBlock) + size);
    if (mapping_size < size) return 0;
    mapped = platform_allocate_pages(mapping_size);
    if (mapped == 0) return 0;
    block = (RtAllocBlock *)mapped;
    block->size = mapping_size - sizeof(RtAllocBlock);
    block->free = 0;
    block->next = 0;
    if (rt_alloc_tail != 0) rt_alloc_tail->next = block;
    else rt_alloc_head = block;
    rt_alloc_tail = block;
    rt_alloc_split(block, size);
    return block;
}

static int rt_alloc_blocks_adjacent(struct RtAllocBlock *left, struct RtAllocBlock *right) {
    return (const unsigned char *)(left + 1) + left->size == (const unsigned char *)right;
}

static void rt_alloc_coalesce(void) {
    RtAllocBlock *block = rt_alloc_head;
    while (block != 0 && block->next != 0) {
        if (block->free && block->next->free && rt_alloc_blocks_adjacent(block, block->next)) {
            block->size += sizeof(RtAllocBlock) + block->next->size;
            block->next = block->next->next;
            if (block->next == 0) rt_alloc_tail = block;
        } else {
            block = block->next;
        }
    }
}

static void *rt_malloc_unlocked(size_t size) {
    RtAllocBlock *block;
    if (size == 0U) size = 1U;
    size = rt_alloc_align(size);
    block = rt_alloc_find_free(size);
    if (block != 0) {
        block->free = 0;
        rt_alloc_split(block, size);
        return block + 1;
    }
    block = rt_alloc_grow(size);
    return block == 0 ? 0 : (void *)(block + 1);
}

void *rt_malloc(size_t size) {
    void *ptr;

    rt_alloc_lock();
    ptr = rt_malloc_unlocked(size);
    rt_alloc_unlock();
    return ptr;
}

static void rt_free_unlocked(void *ptr) {
    RtAllocBlock *block;
    if (ptr == 0) return;
    block = ((RtAllocBlock *)ptr) - 1;
    block->free = 1;
    rt_alloc_coalesce();
}

void *rt_realloc(void *ptr, size_t size) {
    RtAllocBlock *block;
    void *next;
    size_t copy_size;
    if (ptr == 0) return rt_malloc(size);
    if (size == 0U) {
        rt_free(ptr);
        return 0;
    }
    rt_alloc_lock();
    block = ((RtAllocBlock *)ptr) - 1;
    size = rt_alloc_align(size);
    if (block->size >= size) {
        rt_alloc_split(block, size);
        rt_alloc_unlock();
        return ptr;
    }
    next = rt_malloc_unlocked(size);
    if (next == 0) {
        rt_alloc_unlock();
        return 0;
    }
    copy_size = block->size < size ? block->size : size;
    memcpy(next, ptr, copy_size);
    rt_free_unlocked(ptr);
    rt_alloc_unlock();
    return next;
}

void rt_free(void *ptr) {
    rt_alloc_lock();
    rt_free_unlocked(ptr);
    rt_alloc_unlock();
}

static void rt_sort_swap(unsigned char *left, unsigned char *right, size_t item_size) {
    size_t i;
    for (i = 0U; i < item_size; ++i) {
        unsigned char temp = left[i];
        left[i] = right[i];
        right[i] = temp;
    }
}

static void rt_sort_sift_down(unsigned char *base, size_t start, size_t end, size_t item_size, int (*compare)(const void *, const void *)) {
    size_t root = start;
    while (root * 2U + 1U <= end) {
        size_t child = root * 2U + 1U;
        size_t swap_index = root;
        if (compare(base + swap_index * item_size, base + child * item_size) < 0) swap_index = child;
        if (child + 1U <= end && compare(base + swap_index * item_size, base + (child + 1U) * item_size) < 0) swap_index = child + 1U;
        if (swap_index == root) return;
        rt_sort_swap(base + root * item_size, base + swap_index * item_size, item_size);
        root = swap_index;
    }
}

void rt_sort(void *base_ptr, size_t count, size_t item_size, int (*compare)(const void *, const void *)) {
    unsigned char *base = (unsigned char *)base_ptr;
    size_t start;
    size_t end;
    if (base == 0 || count < 2U || item_size == 0U || compare == 0) return;
    start = (count - 2U) / 2U + 1U;
    while (start > 0U) {
        start -= 1U;
        rt_sort_sift_down(base, start, count - 1U, item_size, compare);
    }
    end = count - 1U;
    while (end > 0U) {
        rt_sort_swap(base, base + end * item_size, item_size);
        end -= 1U;
        rt_sort_sift_down(base, 0U, end, item_size, compare);
    }
}
