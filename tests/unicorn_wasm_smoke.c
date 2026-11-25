#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unicorn/unicorn.h>

#define CODE_ADDRESS 0x10000
#define DATA_ADDRESS 0x11000

typedef struct {
    uint64_t stopAddress;
    unsigned instructions;
    unsigned writes;
    bool stopped;
} HookState;

static void checkUc(const char *operation, uc_err error) {
    if (error != UC_ERR_OK) {
        fprintf(stderr, "%s: %s\n", operation, uc_strerror(error));
        exit(EXIT_FAILURE);
    }
}

static void checkValue(const char *label, uint32_t actual, uint32_t expected) {
    if (actual != expected) {
        fprintf(stderr, "%s: got %" PRIu32 ", expected %" PRIu32 "\n",
                label, actual, expected);
        exit(EXIT_FAILURE);
    }
}

static void onCode(uc_engine *engine, uint64_t address, uint32_t size,
                   void *userData) {
    HookState *state = userData;
    state->instructions++;
    if (address == state->stopAddress) {
        state->stopped = true;
        checkUc("uc_emu_stop", uc_emu_stop(engine));
    }
}

static void onWrite(uc_engine *engine, uc_mem_type type, uint64_t address,
                    int size, int64_t value, void *userData) {
    HookState *state = userData;
    checkValue("write address", (uint32_t)address, DATA_ADDRESS);
    checkValue("write size", (uint32_t)size, 4);
    checkValue("write value", (uint32_t)value, 12);
    state->writes++;
}

static void runProgram(const char *name, uc_mode mode, const uint8_t *code,
                       size_t codeSize, size_t stopOffset) {
    uc_engine *engine = NULL;
    uc_hook codeHook, writeHook;
    HookState state = {.stopAddress = CODE_ADDRESS + stopOffset};
    uint32_t address = DATA_ADDRESS;
    uint32_t value = 0;
    uint8_t *memory = calloc(1, 0x2000);
    if (memory == NULL) {
        fputs("Guest backing allocation failed\n", stderr);
        exit(EXIT_FAILURE);
    }
    checkUc("uc_open", uc_open(UC_ARCH_ARM, mode, &engine));
    checkUc("uc_mem_map_ptr", uc_mem_map_ptr(engine, CODE_ADDRESS, 0x2000,
                                          UC_PROT_ALL, memory));
    checkUc("uc_mem_write", uc_mem_write(engine, CODE_ADDRESS, code, codeSize));
    checkUc("write r1", uc_reg_write(engine, UC_ARM_REG_R1, &address));
    checkUc("code hook", uc_hook_add(engine, &codeHook, UC_HOOK_CODE,
                                    onCode, &state, CODE_ADDRESS,
                                    CODE_ADDRESS + codeSize));
    checkUc("write hook", uc_hook_add(engine, &writeHook, UC_HOOK_MEM_WRITE,
                                     onWrite, &state, 1, 0));
    uint64_t start = CODE_ADDRESS | (mode == UC_MODE_THUMB ? 1 : 0);
    checkUc("uc_emu_start", uc_emu_start(engine, start,
                                       CODE_ADDRESS + codeSize, 0, 32));
    checkUc("read r0", uc_reg_read(engine, UC_ARM_REG_R0, &value));
    checkValue("r0", value, 12);
    checkUc("read r2", uc_reg_read(engine, UC_ARM_REG_R2, &value));
    checkValue("r2", value, 12);
    checkUc("read memory", uc_mem_read(engine, DATA_ADDRESS, &value, sizeof(value)));
    checkValue("stored value", value, 12);
    memcpy(&value, memory + DATA_ADDRESS - CODE_ADDRESS, sizeof(value));
    checkValue("shared backing value", value, 12);
    checkValue("instruction callbacks", state.instructions, 5);
    checkValue("write callbacks", state.writes, 1);
    checkValue("callback stopped execution", state.stopped, true);
    checkUc("uc_close", uc_close(engine));
    free(memory);
    printf("PASS %s arithmetic, memory and callbacks\n", name);
}

static void testInterworking(void) {
    const uint8_t branch[] = {0x10, 0xff, 0x2f, 0xe1};
    const uint8_t thumb[] = {0x2a, 0x21, 0x70, 0x47};
    uc_engine *engine = NULL;
    uint32_t destination = CODE_ADDRESS + 0x101;
    uint32_t link = CODE_ADDRESS + sizeof(branch);
    uint32_t result = 0;
    checkUc("interworking open", uc_open(UC_ARCH_ARM, UC_MODE_ARM, &engine));
    checkUc("interworking map", uc_mem_map(engine, CODE_ADDRESS, 0x1000, UC_PROT_ALL));
    checkUc("write branch", uc_mem_write(engine, CODE_ADDRESS, branch, sizeof(branch)));
    checkUc("write Thumb body", uc_mem_write(engine, CODE_ADDRESS + 0x100,
                                           thumb, sizeof(thumb)));
    checkUc("write destination", uc_reg_write(engine, UC_ARM_REG_R0, &destination));
    checkUc("write lr", uc_reg_write(engine, UC_ARM_REG_LR, &link));
    checkUc("interworking execute", uc_emu_start(engine, CODE_ADDRESS, link, 0, 8));
    checkUc("read result", uc_reg_read(engine, UC_ARM_REG_R1, &result));
    checkValue("interworking result", result, 42);
    checkUc("read cpsr", uc_reg_read(engine, UC_ARM_REG_CPSR, &result));
    checkValue("returned to ARM", result & 0x20, 0);
    checkUc("interworking close", uc_close(engine));
    puts("PASS ARM/Thumb interworking");
}

static void testFaultAndInstructionLimit(void) {
    const uint8_t load[] = {0x00, 0x00, 0x91, 0xe5};
    const uint8_t loop[] = {0xfe, 0xff, 0xff, 0xea};
    uc_engine *engine = NULL;
    uint32_t address = 0x20000;
    checkUc("fault uc_open", uc_open(UC_ARCH_ARM, UC_MODE_ARM, &engine));
    checkUc("fault uc_mem_map", uc_mem_map(engine, CODE_ADDRESS, 0x1000, UC_PROT_ALL));
    checkUc("write load", uc_mem_write(engine, CODE_ADDRESS, load, sizeof(load)));
    checkUc("fault write r1", uc_reg_write(engine, UC_ARM_REG_R1, &address));
    uc_err error = uc_emu_start(engine, CODE_ADDRESS, CODE_ADDRESS + sizeof(load), 0, 8);
    checkValue("unmapped read", error, UC_ERR_READ_UNMAPPED);
    checkUc("write loop", uc_mem_write(engine, CODE_ADDRESS + 0x100, loop, sizeof(loop)));
    checkUc("bounded loop", uc_emu_start(engine, CODE_ADDRESS + 0x100,
                                       CODE_ADDRESS + 0x104, 0, 16));
    uint32_t pc = 0;
    checkUc("read pc", uc_reg_read(engine, UC_ARM_REG_PC, &pc));
    checkValue("loop pc", pc, CODE_ADDRESS + 0x100);
    checkUc("fault uc_close", uc_close(engine));
    puts("PASS unmapped read, recovery and instruction limit");
}

int main(void) {
    unsigned major = 0, minor = 0;
    unsigned version = uc_version(&major, &minor);
    printf("Unicorn %u.%u.%u (runtime 0x%08x)\n",
           major, minor, (version >> 8) & 0xff, version);
    checkValue("major version", major, 2);
    checkValue("minor version", minor, 1);
    checkValue("patch version", (version >> 8) & 0xff, 4);

    const uint8_t arm[] = {
        0x07, 0x00, 0xa0, 0xe3,
        0x05, 0x00, 0x80, 0xe2,
        0x00, 0x00, 0x81, 0xe5,
        0x00, 0x20, 0x91, 0xe5,
        0x00, 0x00, 0xa0, 0xe1,
    };
    const uint8_t thumb[] = {
        0x07, 0x20,
        0x05, 0x30,
        0x08, 0x60,
        0x0a, 0x68,
        0x00, 0xbf,
    };
    runProgram("ARM", UC_MODE_ARM, arm, sizeof(arm), 16);
    runProgram("Thumb", UC_MODE_THUMB, thumb, sizeof(thumb), 8);
    testInterworking();
    testFaultAndInstructionLimit();
    puts("VMRP_UNICORN_WASM_OK");
    return EXIT_SUCCESS;
}
