#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vmrp/bridge.h"
#include "vmrp/memory.h"
#include "vmrp/fileLib.h"
#include "vmrp/vmrp.h"

#include "../src/bridge.c"
#include "../src/vmrp.c"

static int drawCount;
static int editCount;

void consoleGuestLog(const char *text) {
    assert(text != NULL);
}

void guiDrawBitmap(uint16_t *bmp, int32_t x, int32_t y, int32_t w, int32_t h) {
    assert(bmp != NULL);
    drawCount++;
}

int32_t editCreate(const char *title, const char *text, int32_t type, int32_t max_size) {
    assert(memcmp(title, "\0t\0i\0t\0l\0e\0", 12) == 0);
    assert(memcmp(text, "\0t\0e\0x\0t\0", 10) == 0);
    editCount++;
    return 1234;
}

int32 editRelease(int32 edit) {
    return MR_SUCCESS;
}

char *editGetText(int32 edit) {
    return NULL;
}

static uint32_t callBridge(uc_engine *engine, BridgeCB fn, uint32_t r0, uint32_t r1,
                           uint32_t r2, uint32_t r3) {
    assert(uc_reg_write(engine, UC_ARM_REG_R0, &r0) == UC_ERR_OK);
    assert(uc_reg_write(engine, UC_ARM_REG_R1, &r1) == UC_ERR_OK);
    assert(uc_reg_write(engine, UC_ARM_REG_R2, &r2) == UC_ERR_OK);
    assert(uc_reg_write(engine, UC_ARM_REG_R3, &r3) == UC_ERR_OK);
    BridgeMap map = {.name = "boundary-test"};
    fn(&map, engine);
    uint32_t result;
    assert(uc_reg_read(engine, UC_ARM_REG_R0, &result) == UC_ERR_OK);
    return result;
}

static void testRanges(void) {
    assert(getMrpMemRangePtr(START_ADDRESS, TOTAL_MEMORY) == mrpMem);
    assert(getMrpMemRangePtr(END_ADDRESS, 0) == mrpMem + TOTAL_MEMORY);
    assert(getMrpMemRangePtr(END_ADDRESS - 1, 1) == mrpMem + TOTAL_MEMORY - 1);
    assert(getMrpMemRangePtr(START_ADDRESS - 1, 0) == NULL);
    assert(getMrpMemRangePtr(END_ADDRESS, 1) == NULL);
    assert(getMrpMemRangePtr(END_ADDRESS - 1, 2) == NULL);
    assert(getMrpMemRangePtr(UINT32_MAX, 2) == NULL);
    assert(getMrpMemRangePtr(START_ADDRESS, UINT32_MAX) == NULL);
    assert(getMrpMemPtr(0) == NULL);
    assert(toMrpMemAddr(NULL) == 0);
    int unrelated;
    assert(toMrpMemAddr(&unrelated) == 0);
    assert(toMrpMemAddr(mrpMem + TOTAL_MEMORY) == 0);
    assert(toMrpMemAddr(mrpMem + TOTAL_MEMORY - 1) == END_ADDRESS - 1);

    uint8_t *tail = getMrpMemRangePtr(END_ADDRESS - 4, 4);
    memcpy(tail, "abc", 4);
    assert(getMrpStringPtr(END_ADDRESS - 4) == (char *)tail);
    tail[3] = 'd';
    assert(getMrpStringPtr(END_ADDRESS - 4) == NULL);
    tail[2] = tail[3] = 0;
    assert(getMrpWideStringPtr(END_ADDRESS - 4) == (char *)tail);
    tail[2] = 1;
    assert(getMrpWideStringPtr(END_ADDRESS - 4) == NULL);
    assert(getMrpWideStringPtr(END_ADDRESS - 1) == NULL);
}

static void testBuffersAndFileIO(uc_engine *engine) {
    const uint32_t failed = (uint32_t)MR_FAILED;
    uint8_t *tail = getMrpMemRangePtr(END_ADDRESS - 4, 4);
    memcpy(tail, "data", 4);
    assert(callBridge(engine, br_memset, END_ADDRESS - 4, 'x', 4, 0) == END_ADDRESS - 4);
    assert(memcmp(tail, "xxxx", 4) == 0);
    assert(callBridge(engine, br_memset, END_ADDRESS - 1, 0, 2, 0) == 0);
    assert(tail[3] == 'x');
    memcpy(getMrpMemPtr(CODE_ADDRESS), "data", 4);
    assert(callBridge(engine, br_memcpy, END_ADDRESS - 4, CODE_ADDRESS, 4, 0) == END_ADDRESS - 4);
    assert(memcmp(tail, "data", 4) == 0);
    assert(callBridge(engine, br_memcpy, CODE_ADDRESS, END_ADDRESS - 1, 2, 0) == 0);

    const char *path = "build/tests/guest-io.tmp";
    int32_t file = my_open(path, MR_FILE_CREATE | MR_FILE_RDWR);
    assert(file != 0);
    assert(callBridge(engine, br_mr_write, file, END_ADDRESS - 1, 2, 0) == failed);
    assert(my_getLen(path) == 0);
    assert(callBridge(engine, br_mr_write, file, END_ADDRESS - 4, 4, 0) == 4);
    assert(my_seek(file, 0, SEEK_SET) == MR_SUCCESS);
    assert(callBridge(engine, br_mr_read, file, END_ADDRESS - 1, 2, 0) == failed);
    assert(callBridge(engine, br_mr_read, file, END_ADDRESS - 4, 4, 0) == 4);
    assert(memcmp(tail, "data", 4) == 0);
    assert(my_close(file) == MR_SUCCESS);
    assert(remove(path) == 0);

    memset(tail, 'x', 4);
    assert(callBridge(engine, br_mr_open, END_ADDRESS - 4, MR_FILE_RDONLY, 0, 0) == 0);
    assert(callBridge(engine, br_log, END_ADDRESS - 4, 0, 0, 0) == failed);
    assert(callBridge(engine, br_mr_send, 1, END_ADDRESS - 1, 2, 0) == failed);
    assert(callBridge(engine, br_mr_recv, 1, CODE_ADDRESS, UINT32_MAX, 0) == failed);
    assert(callBridge(engine, br_mr_playSound, 0, END_ADDRESS - 1, 2, 0) == failed);
    assert(callBridge(engine, br_getDatetime, END_ADDRESS - 1, 0, 0, 0) == failed);
    uint32_t unalignedDatetime = CODE_ADDRESS + 1;
    assert(callBridge(engine, br_getDatetime, unalignedDatetime, 0, 0, 0) == MR_SUCCESS);
    mr_datetime datetime;
    memcpy(&datetime, getMrpMemRangePtr(unalignedDatetime, sizeof(datetime)), sizeof(datetime));
    assert(datetime.month >= 1 && datetime.month <= 12);
    assert(callBridge(engine, br_mem_get, END_ADDRESS - 1, CODE_ADDRESS, 0, 0) == failed);
    assert(callBridge(engine, br_mr_editGetText, 1234, 0, 0, 0) == 0);
}

static void testExecutableCacheCoherency(uc_engine *engine) {
    const uint32_t entry = MEMORY_MANAGER_ADDRESS + 0x1000;
    const uint32_t stop = entry + 8;
    const uint32_t original[] = {0xE3A00001, 0xE12FFF1E};
    uint32_t replacement = 0xE3A00002;
    const char *path = "build/tests/guest-code.tmp";
    assert(uc_mem_write(engine, entry, original, sizeof(original)) == UC_ERR_OK);

    uint32_t cpsr;
    assert(uc_reg_read(engine, UC_ARM_REG_CPSR, &cpsr) == UC_ERR_OK);
    cpsr &= ~0x20u;
    assert(uc_reg_write(engine, UC_ARM_REG_CPSR, &cpsr) == UC_ERR_OK);
    assert(uc_reg_write(engine, UC_ARM_REG_LR, &stop) == UC_ERR_OK);
    assert(uc_emu_start(engine, entry, stop, 1000000, 20) == UC_ERR_OK);
    uint32_t result;
    assert(uc_reg_read(engine, UC_ARM_REG_R0, &result) == UC_ERR_OK && result == 1);

    int32_t file = my_open(path, MR_FILE_CREATE | MR_FILE_RDWR);
    assert(file != 0);
    assert(my_write(file, &replacement, sizeof(replacement)) == (int32_t)sizeof(replacement));
    assert(my_seek(file, 0, SEEK_SET) == MR_SUCCESS);
    assert(callBridge(engine, br_mr_read, file, entry, sizeof(replacement), 0) ==
           sizeof(replacement));
    assert(my_close(file) == MR_SUCCESS);

    assert(uc_reg_write(engine, UC_ARM_REG_LR, &stop) == UC_ERR_OK);
    assert(uc_emu_start(engine, entry, stop, 1000000, 20) == UC_ERR_OK);
    assert(uc_reg_read(engine, UC_ARM_REG_R0, &result) == UC_ERR_OK && result == 2);
    assert(remove(path) == 0);
}

static void testStackAndUI(uc_engine *engine) {
    const uint32_t failed = (uint32_t)MR_FAILED;
    uint32_t sp = END_ADDRESS - 2;
    assert(uc_reg_write(engine, UC_ARM_REG_SP, &sp) == UC_ERR_OK);
    assert(callBridge(engine, br_mr_sendto, 1, CODE_ADDRESS, 4, 0) == failed);
    uint32_t value;
    sp = UINT32_MAX - 1;
    assert(uc_reg_write(engine, UC_ARM_REG_SP, &sp) == UC_ERR_OK);
    assert(!getArg(engine, 5, &value));
    sp = STACK_ADDRESS + STACK_SIZE - 4;
    assert(uc_reg_write(engine, UC_ARM_REG_SP, &sp) == UC_ERR_OK);
    uint32_t height = SCREEN_HEIGHT;
    memcpy(getMrpMemRangePtr(sp, 4), &height, 4);
    assert(callBridge(engine, br_mr_recvfrom, 1, CODE_ADDRESS, 4, END_ADDRESS - 1) == failed);
    uint32_t portAddress = END_ADDRESS - 1;
    memcpy(getMrpMemRangePtr(sp, 4), &portAddress, 4);
    assert(callBridge(engine, br_mr_recvfrom, 1, CODE_ADDRESS, 4, CODE_ADDRESS + 32) == failed);
    memcpy(getMrpMemRangePtr(sp, 4), &height, 4);
    assert(callBridge(engine, br_mr_drawBitmap, END_ADDRESS - 2, 0, 0, SCREEN_WIDTH) == failed);
    assert(drawCount == 0);
    callBridge(engine, br_mr_drawBitmap, MEMORY_MANAGER_ADDRESS + 1024 * 1024, 0, 0, SCREEN_WIDTH);
    assert(drawCount == 1);

    memcpy(getMrpMemRangePtr(CODE_ADDRESS, 22), "\0t\0i\0t\0l\0e\0\0\0t\0e\0x\0t\0", 22);
    assert(callBridge(engine, br_mr_editCreate, CODE_ADDRESS, CODE_ADDRESS + 12, 0, 32) == 1234);
    assert(editCount == 1);
    assert(callBridge(engine, br_mr_editCreate, 0, CODE_ADDRESS, 0, 32) == failed);
    assert(editCount == 1);
    assert(callBridge(engine, br_mr_initNetwork, END_ADDRESS, CODE_ADDRESS, 0, 0) == failed);
}

static void testAllocationAndEXT(uc_engine *engine) {
    const uint32_t failed = (uint32_t)MR_FAILED;
    assert(callBridge(engine, br_mr_malloc, UINT32_MAX, 0, 0, 0) == 0);
    uint32_t address = callBridge(engine, br_mr_malloc, 32, 0, 0, 0);
    assert(address >= MEMORY_MANAGER_ADDRESS && address < END_ADDRESS);
    assert(callBridge(engine, br_mem_free, address + 1, 32, 0, 0) == failed);
    assert(callBridge(engine, br_mem_free, address, 32, 0, 0) == MR_SUCCESS);
    assert(callBridge(engine, br_mem_free, CODE_ADDRESS, 32, 0, 0) == failed);
    assert(callBridge(engine, br__mr_c_function_new, CODE_ADDRESS, 4, 0, 0) == failed);
    assert(callBridge(engine, br__mr_c_function_new, CODE_ADDRESS, 20, 0, 0) == MR_SUCCESS);
    assert(mr_c_function_P != NULL);
    assert(toMrpMemAddr(mr_c_function_P) >= MEMORY_MANAGER_ADDRESS);
    my_freeExt(mr_c_function_P);
    mr_c_function_P = NULL;
}

static void testARMHookBoundary(uc_engine *engine) {
    uint32_t program[] = {
        0xE59FC000,  // ldr r12, [pc]
        0xE12FFF1C,  // bx r12
        toMrpMemAddr(mr_table) + 0xC
    };
    const uint32_t entry = CODE_ADDRESS + 0x100;
    assert(uc_mem_write(engine, entry, program, sizeof(program)) == UC_ERR_OK);
    uint32_t dst = END_ADDRESS - 1;
    uint32_t src = CODE_ADDRESS + 0x300;
    uint32_t len = 2;
    uint32_t stop = CODE_ADDRESS;
    uint8_t *tail = getMrpMemRangePtr(dst, 1);
    *tail = 0xA5;
    assert(uc_reg_write(engine, UC_ARM_REG_R0, &dst) == UC_ERR_OK);
    assert(uc_reg_write(engine, UC_ARM_REG_R1, &src) == UC_ERR_OK);
    assert(uc_reg_write(engine, UC_ARM_REG_R2, &len) == UC_ERR_OK);
    assert(uc_reg_write(engine, UC_ARM_REG_LR, &stop) == UC_ERR_OK);
    assert(uc_emu_start(engine, entry, stop, 1000000, 50) == UC_ERR_OK);
    uint32_t result;
    assert(uc_reg_read(engine, UC_ARM_REG_R0, &result) == UC_ERR_OK);
    assert(result == 0);
    assert(*tail == 0xA5);

    dst = END_ADDRESS - 2;
    memcpy(getMrpMemRangePtr(src, 2), "ok", 2);
    assert(uc_reg_write(engine, UC_ARM_REG_R0, &dst) == UC_ERR_OK);
    assert(uc_reg_write(engine, UC_ARM_REG_R1, &src) == UC_ERR_OK);
    assert(uc_reg_write(engine, UC_ARM_REG_R2, &len) == UC_ERR_OK);
    assert(uc_reg_write(engine, UC_ARM_REG_LR, &stop) == UC_ERR_OK);
    assert(uc_emu_start(engine, entry, stop, 1000000, 50) == UC_ERR_OK);
    assert(uc_reg_read(engine, UC_ARM_REG_R0, &result) == UC_ERR_OK);
    assert(result == dst);
    assert(memcmp(getMrpMemRangePtr(dst, 2), "ok", 2) == 0);
}

static void testSimulatedSms(uc_engine *engine) {
    uint32_t program[] = {
        0xE59FC004,  // ldr r12, [pc, #4]
        0xE12FFF3C,  // blx r12
        0xE3A0402A,  // mov r4, #42
        toMrpMemAddr(mr_table) + 0xEC
    };
    const uint32_t entry = CODE_ADDRESS + 0x140;
    const uint32_t number = CODE_ADDRESS + 0x300;
    const uint32_t content = CODE_ADDRESS + 0x320;
    assert(uc_mem_write(engine, entry, program, sizeof(program)) == UC_ERR_OK);
    assert(uc_mem_write(engine, number, "00000", 6) == UC_ERR_OK);
    assert(uc_mem_write(engine, content, "simulation", 11) == UC_ERR_OK);
    const uint32_t requests[][3] = {
        {number, content, 0},
        {number, content, 1},
        {END_ADDRESS, UINT32_MAX, UINT32_MAX}
    };
    for (size_t i = 0; i < SDL_arraysize(requests); i++) {
        uint32_t continued = 0;
        assert(uc_reg_write(engine, UC_ARM_REG_R0, &requests[i][0]) == UC_ERR_OK);
        assert(uc_reg_write(engine, UC_ARM_REG_R1, &requests[i][1]) == UC_ERR_OK);
        assert(uc_reg_write(engine, UC_ARM_REG_R2, &requests[i][2]) == UC_ERR_OK);
        assert(uc_reg_write(engine, UC_ARM_REG_R4, &continued) == UC_ERR_OK);
        assert(uc_emu_start(engine, entry, entry + 12, 1000000, 50) == UC_ERR_OK);
        uint32_t result;
        assert(uc_reg_read(engine, UC_ARM_REG_R0, &result) == UC_ERR_OK);
        assert(uc_reg_read(engine, UC_ARM_REG_R4, &continued) == UC_ERR_OK);
        assert(result == MR_SUCCESS);
        assert(continued == 42);
    }
}

#ifdef __linux__
static void testDirectoryOrder(void) {
    char root[] = "/tmp/vmrp-directory-test-XXXXXX";
    assert(mkdtemp(root) != NULL);
    const char *names[] = {"z.mrp", "b.mrp", "a.mrp"};
    char path[256];
    for (size_t i = 0; i < SDL_arraysize(names); i++) {
        snprintf(path, sizeof(path), "%s/%s", root, names[i]);
        int32_t file = my_open(path, MR_FILE_CREATE | MR_FILE_RDWR);
        assert(file != 0 && my_close(file) == MR_SUCCESS);
    }
    int32_t directory = my_opendir(root);
    assert(directory != MR_FAILED);
    for (size_t i = 0; i < SDL_arraysize(names); i++) {
        char *name;
        do { name = my_readdir(directory); } while (name != NULL && name[0] == '.');
        assert(name != NULL && strcmp(name, names[SDL_arraysize(names) - i - 1]) == 0);
    }
    assert(my_readdir(directory) == NULL);
    int32_t second = my_opendir(root);
    assert(second != MR_FAILED && my_closedir(directory) == MR_SUCCESS);
    assert(my_readdir(second) != NULL);
    assert(my_closedir(second) == MR_SUCCESS);
    for (size_t i = 0; i < SDL_arraysize(names); i++) {
        snprintf(path, sizeof(path), "%s/%s", root, names[i]);
        assert(my_remove(path) == MR_SUCCESS);
    }
    directory = my_opendir(root);
    assert(directory != MR_FAILED);
    char *name;
    do { name = my_readdir(directory); } while (name != NULL && name[0] == '.');
    assert(name == NULL && my_closedir(directory) == MR_SUCCESS);
    assert(my_rmDir(root) == MR_SUCCESS);
}
#endif

int main(void) {
    uc_engine *engine = initVmrp();
    assert(engine != NULL);
    testRanges();
    testBuffersAndFileIO(engine);
    testExecutableCacheCoherency(engine);
    testStackAndUI(engine);
    testARMHookBoundary(engine);
    testSimulatedSms(engine);
    testAllocationAndEXT(engine);
#ifdef __linux__
    testDirectoryOrder();
#endif
    assert(pthread_mutex_destroy(&mutex) == 0);
    freeVmrp(engine);
    assert(getMrpMemRangePtr(START_ADDRESS, 1) == NULL);
    puts("Guest boundary regression tests passed.");
    return 0;
}
