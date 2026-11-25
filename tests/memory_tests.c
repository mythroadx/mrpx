#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "vmrp/memory.h"

static _Alignas(8) uint8_t heap[4096];
static const void *watchedSource;
static size_t expectedCopyLength;
static size_t copyCount;

void *getMrpMemPtr(uint32_t address) {
    assert(address == 0);
    return heap;
}

void *getMrpMemRangePtr(uint32_t address, uint32_t len) {
    assert(address == 0 && len == sizeof(heap));
    return heap;
}

static void *checkedMemmove(void *dest, const void *src, size_t len) {
    if (src == watchedSource) {
        assert(len == expectedCopyLength);
        copyCount++;
    }
    return memmove(dest, src, len);
}

#define memmove checkedMemmove
#include "../src/memory.c"
#undef memmove

static void testResize(uint32_t oldLength, uint32_t newLength) {
    initMemoryManager(0, sizeof(heap));
    uint8_t *data = my_mallocExt(oldLength);
    assert(data != NULL);
    for (uint32_t i = 0; i < oldLength; i++) {
        data[i] = (uint8_t)(i + 1);
    }
    watchedSource = data;
    expectedCopyLength = oldLength < newLength ? oldLength : newLength;
    copyCount = 0;
    uint8_t *resized = my_reallocExt(data, newLength);
    assert(resized != NULL);
    assert(copyCount == 1);
    for (uint32_t i = 0; i < expectedCopyLength; i++) {
        assert(resized[i] == (uint8_t)(i + 1));
    }
    my_freeExt(resized);
    assert(LG_mem_left == sizeof(heap));
}

static void testFailedResizePreservesAllocation(void) {
    initMemoryManager(0, sizeof(heap));
    uint8_t *data = my_mallocExt(4);
    assert(data != NULL);
    memcpy(data, "test", 4);
    watchedSource = data;
    copyCount = 0;
    assert(my_reallocExt(data, sizeof(heap) * 2) == NULL);
    assert(copyCount == 0);
    assert(memcmp(data, "test", 4) == 0);
    my_freeExt(data);
    assert(LG_mem_left == sizeof(heap));
}

static void testInvalidAllocationBoundaries(void) {
    initMemoryManager(0, sizeof(heap));
    assert(my_mallocExt(UINT32_MAX) == NULL);
    assert(my_mallocExt(UINT32_MAX - 4) == NULL);
    assert(my_malloc(UINT32_MAX) == NULL);
    uint8_t *data = my_mallocExt(12);
    assert(data != NULL);
    assert(!my_freeExtChecked(heap));
    assert(!my_freeExtChecked(data + 1));
    uint32_t oldLength = *((uint32_t *)data - 1);
    *((uint32_t *)data - 1) = UINT32_MAX;
    assert(!my_freeExtChecked(data));
    assert(my_reallocExt(data, 24) == NULL);
    *((uint32_t *)data - 1) = oldLength;
    assert(my_freeExtChecked(data));
    assert(!my_freeExtChecked(data));
    assert(LG_mem_left == sizeof(heap));

    initMemoryManager(0, sizeof(heap));
    ((LG_mem_free_t *)heap)->next = UINT32_MAX;
    assert(my_mallocExt(4) == NULL);
}

int main(void) {
    testResize(4, 12);
    testResize(12, 4);
    testFailedResizePreservesAllocation();
    testInvalidAllocationBoundaries();
    puts("Memory regression tests passed.");
    return 0;
}
