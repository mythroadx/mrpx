#include "vmrp/vmrp.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "vmrp/bridge.h"
#include "vmrp/fileLib.h"
#include "vmrp/memory.h"
#include "vmrp/utils.h"
#include "vmrp/debug.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

static uint8_t *mrpMem;  // 模拟器的全部内存
static uc_engine *uc = NULL;

// 返回的内存禁止free
#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void *getMrpMemPtr(uint32_t addr) {
    return getMrpMemRangePtr(addr, 1);
}

void *getMrpMemRangePtr(uint32_t addr, uint32_t len) {
    if (mrpMem == NULL || addr < START_ADDRESS || addr > END_ADDRESS ||
        len > END_ADDRESS - addr) {
        printf("Invalid Guest memory range: address=0x%08" PRIX32 ", length=%" PRIu32 "\n", addr, len);
        return NULL;
    }
    return mrpMem + (addr - START_ADDRESS);
}

static char *getGuestString(uint32_t addr, uint32_t charSize) {
    uint8_t *ptr = getMrpMemRangePtr(addr, charSize);
    if (ptr == NULL) {
        return NULL;
    }
    uint32_t available = END_ADDRESS - addr;
    for (uint32_t i = 0; i <= available - charSize; i += charSize) {
        if (ptr[i] == 0 && (charSize == 1 || ptr[i + 1] == 0)) {
            return (char *)ptr;
        }
    }
    printf("Unterminated Guest string: address=0x%08" PRIX32 ", character size=%" PRIu32 "\n", addr, charSize);
    return NULL;
}

char *getMrpStringPtr(uint32_t addr) {
    return getGuestString(addr, 1);
}

char *getMrpWideStringPtr(uint32_t addr) {
    if (addr % 2 != 0) {
        printf("Unaligned Guest wide string: address=0x%08" PRIX32 "\n", addr);
        return NULL;
    }
    return getGuestString(addr, 2);
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
uint32_t toMrpMemAddr(void *ptr) {
    if (ptr == NULL) {
        return 0;
    }
    uintptr_t address = (uintptr_t)ptr;
    uintptr_t base = (uintptr_t)mrpMem;
    if (mrpMem == NULL || address < base || address - base >= TOTAL_MEMORY) {
        printf("Invalid host pointer for Guest address conversion: %p\n", ptr);
        return 0;
    }
    return (uint32_t)(address - base) + START_ADDRESS;
}

#ifdef DEBUG
static void hook_block(uc_engine *uc, uint64_t address, uint32_t size, void *user_data) {
    printf(">>> Tracing basic block at 0x%" PRIx64 ", block size = 0x%x\n", address, size);
}
static void hook_mem_valid(uc_engine *uc, uc_mem_type type, uint64_t address, int size, int64_t value, void *user_data) {
    printf(">>> Tracing mem_valid mem_type:%s at 0x%" PRIx64 ", size:0x%x, value:0x%" PRIx64 "\n",
           memTypeStr(type), address, size, value);
    if (type == UC_MEM_READ && size <= 4) {
        uint32_t v, pc;
        uc_mem_read(uc, address, &v, size);
        uc_reg_read(uc, UC_ARM_REG_PC, &pc);
        printf("PC:0x%X,read:0x%X\n", pc, v);
    }
}

#endif

static bool hook_mem_invalid(uc_engine *uc, uc_mem_type type, uint64_t address, int size, int64_t value, void *user_data) {
    printf(">>> Tracing mem_invalid mem_type:%s at 0x%" PRIx64 ", size:0x%x, value:0x%" PRIx64 "\n",
           memTypeStr(type), address, size, value);
    dumpREG(uc);
    return false;
}

int freeVmrp(uc_engine *uc) {
    uc_err err = uc_close(uc);
    if (err != UC_ERR_OK) {
        printf("Failed to close Unicorn: %s\n", uc_strerror(err));
    }
    free(mrpMem);
    mrpMem = NULL;
    return 0;
}

void stopVmrp(void) {
    if (uc != NULL) {
        freeVmrp(uc);
        uc = NULL;
    }
}

uc_engine *initVmrp() {
    uc_engine *uc;
    uc_err err;
    uc_hook trace;

    err = uc_open(UC_ARCH_ARM, UC_MODE_ARM, &uc);
    if (err) {
        printf("Failed on uc_open() with error returned: %u (%s)\n", err, uc_strerror(err));
        return NULL;
    }

    mrpMem = malloc(TOTAL_MEMORY);
    if (mrpMem == NULL) {
        printf("Failed to allocate Guest memory.\n");
        uc_close(uc);
        return NULL;
    }
    // unicorn存在BUG，UC_HOOK_MEM_INVALID只能拦截第一次UC_MEM_FETCH_PROT，所以干脆设置成可执行，统一在UC_HOOK_CODE事件中处理
    err = uc_mem_map_ptr(uc, START_ADDRESS, TOTAL_MEMORY, UC_PROT_ALL, mrpMem);
    if (err) {
        printf("Failed mem map: %u (%s)\n", err, uc_strerror(err));
        goto end;
    }
    initMemoryManager(MEMORY_MANAGER_ADDRESS, MEMORY_MANAGER_SIZE);

    err = bridge_init(uc);
    if (err) {
        printf("Failed bridge_init(): %u (%s)\n", err, uc_strerror(err));
        goto end;
    }

#ifdef DEBUG
    uc_hook_add(uc, &trace, UC_HOOK_BLOCK, hook_block, NULL, 1, 0);
    uc_hook_add(uc, &trace, UC_HOOK_MEM_VALID, hook_mem_valid, NULL, 1, 0);
    uc_hook_add(uc, &trace, UC_HOOK_CODE, hook_code_debug, NULL, 1, 0);
#endif
    uc_hook_add(uc, &trace, UC_HOOK_MEM_INVALID, hook_mem_invalid, NULL, 1, 0, 0);

    // 设置栈
    uint32_t value = STACK_ADDRESS + STACK_SIZE;  // 满递减
    uc_reg_write(uc, UC_ARM_REG_SP, &value);

    return uc;
end:
    uc_close(uc);
    free(mrpMem);
    mrpMem = NULL;
    return NULL;
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
int32_t c_event(int32_t code, int32_t p1, int32_t p2) {
    if (uc) {
        return bridge_dsm_mr_event(uc, code, p1, p2);
    }
    return MR_FAILED;
}
#endif

int32_t event(int32_t code, int32_t p1, int32_t p2) {
    if (uc) {
        return bridge_dsm_mr_event(uc, code, p1, p2);
    }
    return MR_FAILED;
}

int32_t timer() {
    if (uc) {
        return bridge_dsm_mr_timer(uc);
    }
    return MR_FAILED;
}

int32_t networkCallback(uint32_t addr, int32_t result, uint32_t userDataAddr) {
    if (uc != NULL) {
        return bridge_dsm_network_cb(uc, addr, result, userDataAddr);
    }
    return MR_FAILED;
}

int32_t loadCode() {
    char *filename = "cfunction.ext";
    int32_t len = my_getLen(filename);
    if (len <= 0) {
        return MR_FAILED;
    }
    if ((uint32_t)len > CODE_SIZE) {
        printf("Guest module is larger than the code region: %d bytes.\n", len);
        return MR_FAILED;
    }
    char *buf = readFile(filename);
    if (buf == NULL) {
        return MR_FAILED;
    }
    uc_err err = uc_mem_write(uc, CODE_ADDRESS, buf, (size_t)len);
    free(buf);
    if (err != UC_ERR_OK) {
        printf("Failed to load %s: %s\n", filename, uc_strerror(err));
        return MR_FAILED;
    }
    return MR_SUCCESS;
}

int startVmrp(const char *mrpFilename, const char *moduleName) {
    uc = initVmrp();
    if (uc == NULL) {
        printf("initVmrp() fail.\n");
        return MR_FAILED;
    }

    if (loadCode() == MR_FAILED) {
        printf("loadCode failed: cfunction.ext was not found or could not be loaded.\n");
        freeVmrp(uc);
        uc = NULL;
        return MR_FAILED;
    }
    if (bridge_ext_init(uc) != UC_ERR_OK) {
        freeVmrp(uc);
        uc = NULL;
        return MR_FAILED;
    }

    if (bridge_dsm_init(uc) == MR_SUCCESS) {
        printf("bridge_dsm_init success\n");
        dumpREG(uc);

        const char *filename = mrpFilename ? mrpFilename : "dsm_gm.mrp";
        const char *extName = moduleName ? moduleName : "start.mr";
        // char *extName = "cfunction.ext";

        uint32_t ret = bridge_dsm_mr_start_dsm(uc, (char *)filename, (char *)extName, NULL);
        printf("bridge_dsm_mr_start_dsm('%s','%s',NULL): 0x%X\n", filename, extName, ret);
        if (ret != MR_SUCCESS) {
            freeVmrp(uc);
            uc = NULL;
            return MR_FAILED;
        }
    } else {
        printf("bridge_dsm_init failed.\n");
        freeVmrp(uc);
        uc = NULL;
        return MR_FAILED;
    }
    return MR_SUCCESS;
}