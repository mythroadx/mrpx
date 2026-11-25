#include "vmrp/vmrp.h"
#include "vmrp/memory.h"
#include "vmrp/utils.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

typedef struct {
    uint32 next;
    uint32 len;
} LG_mem_free_t;

uint32 LG_mem_min;  // 从未分配过的长度？
uint32 LG_mem_top;  // 动态申请到达的最高内存值
LG_mem_free_t LG_mem_free;
char *LG_mem_base;
uint32 LG_mem_len;
char *Origin_LG_mem_base;
uint32 Origin_LG_mem_len;
char *LG_mem_end;
uint32 LG_mem_left;  // 剩余内存

#define realLGmemSize(x) (((x) + 7) & (0xfffffff8))

void initMemoryManager(uint32_t baseAddress, uint32_t len) {
    printf("initMemoryManager: baseAddress:0x%X len: 0x%X\n", baseAddress, len);
    Origin_LG_mem_base = getMrpMemRangePtr(baseAddress, len);
    if (Origin_LG_mem_base == NULL || len < sizeof(LG_mem_free_t)) {
        printf("Invalid Guest heap region.\n");
        LG_mem_base = NULL;
        LG_mem_len = LG_mem_left = 0;
        return;
    }
    Origin_LG_mem_len = len;

    LG_mem_base = (char *)((uintptr_t)(Origin_LG_mem_base + 3) & ~(uintptr_t)3);
    LG_mem_len = (Origin_LG_mem_len - (LG_mem_base - Origin_LG_mem_base)) & (~7u);
    LG_mem_end = LG_mem_base + LG_mem_len;
    LG_mem_free.next = 0;
    LG_mem_free.len = 0;
    ((LG_mem_free_t *)LG_mem_base)->next = LG_mem_len;
    ((LG_mem_free_t *)LG_mem_base)->len = LG_mem_len;
    LG_mem_left = LG_mem_len;
#ifdef MEM_DEBUG
    LG_mem_min = LG_mem_len;
    LG_mem_top = 0;
#endif
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void printMemoryInfo() {
    printf(".......total:%d, min:%d, free:%d, top:%d\n", LG_mem_len, LG_mem_min, LG_mem_left, LG_mem_top);
    printf(".......base:%p, end:%p\n", LG_mem_base, LG_mem_end);
    printf(".......obase:%p, olen:%d\n", Origin_LG_mem_base, Origin_LG_mem_len);
}

void *my_malloc(uint32 len) {
    LG_mem_free_t *previous, *nextfree, *l;
    void *ret;

    if (LG_mem_base == NULL || len > UINT32_MAX - 7) {
        printf("my_malloc invalid memory request\n");
        return NULL;
    }
    len = (uint32)realLGmemSize(len);
    if (len >= LG_mem_left) {
        printf("my_malloc no memory\n");
        goto err;
    }
    if (!len) {
        printf("my_malloc invalid memory request");
        goto err;
    }
    if (LG_mem_free.next > LG_mem_len) {
        printf("my_malloc corrupted memory");
        goto err;
    }
    previous = &LG_mem_free;
    nextfree = (LG_mem_free_t *)(LG_mem_base + previous->next);
    while ((char *)nextfree < LG_mem_end) {
        uint32 offset = (uint32)((char *)nextfree - LG_mem_base);
        if (offset % 8 != 0 || LG_mem_len - offset < sizeof(*nextfree) ||
            nextfree->len < sizeof(*nextfree) || nextfree->len % 8 != 0 ||
            nextfree->len > LG_mem_len - offset || nextfree->next < offset + nextfree->len ||
            nextfree->next > LG_mem_len) {
            printf("my_malloc corrupted free list\n");
            return NULL;
        }
        if (nextfree->len == len) {
            previous->next = nextfree->next;
            LG_mem_left -= len;
#ifdef MEM_DEBUG
            if (LG_mem_left < LG_mem_min)
                LG_mem_min = LG_mem_left;
            if (LG_mem_top < previous->next)
                LG_mem_top = previous->next;
#endif
            ret = (void *)nextfree;
            goto end;
        }
        if (nextfree->len > len) {
            l = (LG_mem_free_t *)((char *)nextfree + len);
            l->next = nextfree->next;
            l->len = (uint32)(nextfree->len - len);
            previous->next += len;
            LG_mem_left -= len;
#ifdef MEM_DEBUG
            if (LG_mem_left < LG_mem_min)
                LG_mem_min = LG_mem_left;
            if (LG_mem_top < previous->next)
                LG_mem_top = previous->next;
#endif
            ret = (void *)nextfree;
            goto end;
        }
        previous = nextfree;
        nextfree = (LG_mem_free_t *)(LG_mem_base + nextfree->next);
    }
    printf("my_malloc no memory\n");
err:
    return 0;
end:
    return ret;
}

bool my_free(void *p, uint32 len) {
    LG_mem_free_t *free, *n;
    uintptr_t address = (uintptr_t)p;
    uintptr_t base = (uintptr_t)LG_mem_base;
    if (LG_mem_base == NULL || p == NULL || address < base ||
        address - base >= LG_mem_len || (address - base) % 8 != 0 ||
        len == 0 || len > UINT32_MAX - 7) {
        printf("my_free invalid allocation\n");
        return false;
    }
    len = (uint32)realLGmemSize(len);
    if (len > LG_mem_len - (address - base) || len > LG_mem_len - LG_mem_left) {
        printf("my_free invalid length\n");
        return false;
    }
    free = &LG_mem_free;
    if (free->next > LG_mem_len) {
        printf("my_free corrupted free list\n");
        return false;
    }
    n = (LG_mem_free_t *)(LG_mem_base + free->next);
    while ((char *)n < LG_mem_end) {
        uint32 offset = (uint32)((char *)n - LG_mem_base);
        if (offset % 8 != 0 || LG_mem_len - offset < sizeof(*n) ||
            n->len < sizeof(*n) || n->len % 8 != 0 || n->len > LG_mem_len - offset ||
            n->next < offset + n->len || n->next > LG_mem_len) {
            printf("my_free corrupted free list\n");
            return false;
        }
        if ((uintptr_t)n >= address) {
            break;
        }
        if ((uintptr_t)n + n->len > address) {
            printf("my_free overlaps a free block\n");
            return false;
        }
        free = n;
        n = (LG_mem_free_t *)(LG_mem_base + n->next);
    }
    if ((uintptr_t)n < address + len) {
        printf("my_free overlaps a free block\n");
        return false;
    }
    if ((free != &LG_mem_free) && ((char *)free + free->len == p)) {
        free->len += len;
    } else {
        free->next = (uint32)((char *)p - LG_mem_base);
        free = (LG_mem_free_t *)p;
        free->next = (uint32)((char *)n - LG_mem_base);
        free->len = len;
    }
    if (((char *)n < LG_mem_end) && ((char *)p + len == (char *)n)) {
        free->next = n->next;
        free->len += n->len;
    }
    LG_mem_left += len;
    return true;
}

void *my_realloc(void *p, uint32 oldlen, uint32 len) {
    unsigned long minsize = (oldlen > len) ? len : oldlen;
    void *newblock;
    if (p == NULL) {
        return my_malloc(len);
    }
    if (len == 0) {
        my_free(p, oldlen);
        return NULL;
    }
    newblock = my_malloc(len);
    if (newblock == NULL) {
        return newblock;
    }
    memmove(newblock, p, minsize);
    my_free(p, oldlen);
    return newblock;
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void *my_mallocExt(uint32 len) {
    uint32 *p;
    if (len == 0) {
        return NULL;
    }
    if (len > UINT32_MAX - sizeof(uint32) - 7) {
        printf("my_mallocExt length overflow\n");
        return NULL;
    }
    p = my_malloc(len + sizeof(uint32));
    if (p) {
        *p = len;
        return (void *)(p + 1);
    }
    return p;
}

void *my_mallocExt0(uint32 len) {
    uint32 *p = my_mallocExt(len);
    if (p) {
        memset(p, 0, len);
        return p;
    }
    return p;
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void my_freeExt(void *p) {
    my_freeExtChecked(p);
}

static bool getExtSize(void *p, uint32 *len) {
    uintptr_t address = (uintptr_t)p;
    uintptr_t base = (uintptr_t)LG_mem_base;
    if (LG_mem_base == NULL || address < base || address - base < sizeof(uint32) ||
        address - base >= LG_mem_len || (address - base - sizeof(uint32)) % 8 != 0) {
        printf("Invalid Guest allocation pointer: %p\n", p);
        return false;
    }
    memcpy(len, (char *)p - sizeof(uint32), sizeof(*len));
    if (*len == 0 || *len > LG_mem_len - (address - base)) {
        printf("Invalid Guest allocation length: %" PRIu32 "\n", *len);
        return false;
    }
    return true;
}

bool my_freeExtChecked(void *p) {
    if (p == NULL) {
        return true;
    }
    uint32 len;
    if (!getExtSize(p, &len)) {
        return false;
    }
    return my_free((char *)p - sizeof(uint32), len + sizeof(uint32));
}

void *my_reallocExt(void *p, uint32 newLen) {
    if (p == NULL) {
        return my_mallocExt(newLen);
    } else if (newLen == 0) {
        my_freeExt(p);
        return NULL;
    } else {
        uint32 oldlen;
        if (!getExtSize(p, &oldlen)) {
            return NULL;
        }
        uint32 minsize = (oldlen < newLen) ? oldlen : newLen;
        void *newblock = my_mallocExt(newLen);
        if (newblock == NULL) {
            return newblock;
        }
        memmove(newblock, p, minsize);
        my_freeExt(p);
        return newblock;
    }
}
