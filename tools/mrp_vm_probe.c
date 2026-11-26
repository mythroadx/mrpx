/* Standalone research probe: archived VM/parser plus local host stubs, not DSM. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "mr.h"
#include "mem.h"
#include "string.h"

_Static_assert(sizeof(void *) == 4, "This probe requires a 32-bit target");
_Static_assert(sizeof(int) == 4 && sizeof(mrp_Number) == 4, "Expected 32-bit integer VM");

void *mr_malloc(uint32 size) { return malloc(size); }
void mr_free(void *ptr, uint32 size) { (void)size; free(ptr); }
void *mr_realloc(void *ptr, uint32 oldsize, uint32 size) {
    (void)oldsize;
    return realloc(ptr, size);
}
void *mr_mallocExt(uint32 size) { return malloc(size); }
void *mr_mallocExt0(uint32 size) { return calloc(1, size); }
void mr_freeExt(void *ptr) { free(ptr); }
void *mr_reallocExt(void *ptr, uint32 size) { return realloc(ptr, size); }

void mr_printf(const char *format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
}

int mr_isdigit(int ch) { return ch >= '0' && ch <= '9'; }
int mr_isxdigit(int ch) {
    return mr_isdigit(ch) || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
}
int mr_isalpha(int ch) {
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
}
int mr_isalnum(int ch) { return mr_isalpha(ch) || mr_isdigit(ch); }
int mr_iscntrl(int ch) { return (unsigned int)ch < 32u || ch == 127; }
int mr_isspace(int ch) {
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' || ch == '\f' || ch == '\v';
}

static int c_and(mrp_State *state) {
    int a = (int)mrp_tonumber(state, 1), b = (int)mrp_tonumber(state, 2);
    mrp_pushnumber(state, a & b);
    return 1;
}
static int c_or(mrp_State *state) {
    int a = (int)mrp_tonumber(state, 1), b = (int)mrp_tonumber(state, 2);
    mrp_pushnumber(state, a | b);
    return 1;
}
static int c_xor(mrp_State *state) {
    int a = (int)mrp_tonumber(state, 1), b = (int)mrp_tonumber(state, 2);
    mrp_pushnumber(state, a ^ b);
    return 1;
}
static int c_not(mrp_State *state) {
    mrp_pushnumber(state, !(int)mrp_tonumber(state, 1));
    return 1;
}
static int c_mod(mrp_State *state) {
    int a = (int)mrp_tonumber(state, 1), b = (int)mrp_tonumber(state, 2);
    mrp_pushnumber(state, a % b);
    return 1;
}

typedef struct {
    const char *data;
    size_t length;
} Reader;

typedef struct {
    FILE *file;
    int failed;
} Writer;

static const char *read_chunk(mrp_State *state, void *data, size_t *size) {
    Reader *reader = (Reader *)data;
    (void)state;
    *size = reader->length;
    reader->length = 0;
    return reader->data;
}

static int write_chunk(mrp_State *state, const void *data, size_t size, void *output) {
    Writer *writer = (Writer *)output;
    (void)state;
    if (fwrite(data, 1, size, writer->file) != size) writer->failed = 1;
    return writer->failed;
}

static int check_results(mrp_State *state, const char *label) {
    static const int expected[] = {2, 7, 5, -7, 2, 7, 5, 0, 2};
    int status = mrp_pcall(state, 0, 9, 0);
    int i, ok = 1;
    if (status) {
        fprintf(stderr, "%s execution failed (%d): %s\n", label, status, mrp_tostring(state, -1));
        return 0;
    }
    printf("%s:", label);
    for (i = 0; i < 9; i++) {
        if (mrp_type(state, i + 1) != MRP_TNUMBER) {
            printf(" <non-number>");
            ok = 0;
            continue;
        }
        int actual = (int)mrp_tonumber(state, i + 1);
        printf(" %d", actual);
        if (actual != expected[i]) ok = 0;
    }
    printf("\n");
    if (!ok) fprintf(stderr, "%s results did not match the expected values\n", label);
    mrp_settop(state, 0);
    return ok;
}

static int power_calls;
static int power_args_ok;

static int numeric_power(mrp_State *state) {
    power_calls++;
    power_args_ok = mrp_type(state, 1) == MRP_TNUMBER &&
                    mrp_type(state, 2) == MRP_TNUMBER &&
                    mrp_tonumber(state, 1) == 2 && mrp_tonumber(state, 2) == 3;
    mrp_pushnumber(state, 203);
    return 1;
}

static int left_power(mrp_State *state) {
    power_calls++;
    power_args_ok = mrp_type(state, 1) == MRP_TTABLE &&
                    (mrp_type(state, 2) == MRP_TTABLE ||
                     (mrp_type(state, 2) == MRP_TNUMBER && mrp_tonumber(state, 2) == 3));
    mrp_pushnumber(state, 301);
    return 1;
}

static int right_power(mrp_State *state) {
    power_calls++;
    power_args_ok = mrp_type(state, 1) == MRP_TNUMBER &&
                    mrp_tonumber(state, 1) == 2 && mrp_type(state, 2) == MRP_TTABLE;
    mrp_pushnumber(state, 302);
    return 1;
}

static void power_table(mrp_State *state, const char *name, const char *field,
                        mrp_CFunction callback) {
    mrp_newtable(state);
    mrp_newtable(state);
    mrp_pushstring(state, field);
    mrp_pushcfunction(state, callback);
    mrp_settable(state, -3);
    mrp_setmetatable(state, -2);
    mrp_setglobal(state, name);
}

static int check_power_cases(void) {
    enum { GLOBAL = 1, NONFUNCTION = 2, LEFT = 4, RIGHT = 8, WRONG_NAME = 16 };
    static const struct {
        const char *name, *source, *error;
        int flags, expected, calls;
    } cases[] = {
        {"missing_global", "return 2 # 3", "err:1020", 0, 0, 0},
        {"nonfunction_global", "return 2 # 3", "err:1020", NONFUNCTION, 0, 0},
        {"numeric_global", "return 2 # 3", NULL, GLOBAL, 203, 1},
        {"numeric_string", "return '2' # 3", NULL, GLOBAL, 203, 1},
        {"left_metamethod", "return a # 3", NULL, GLOBAL | LEFT, 301, 1},
        {"right_metamethod", "return 2 # b", NULL, GLOBAL | RIGHT, 302, 1},
        {"both_metamethods", "return a # b", NULL, LEFT | RIGHT, 301, 1},
        {"wrong_metamethod_name", "return a # 3", "expect num(arith)", WRONG_NAME, 0, 0}
    };
    size_t i;
    int ok = 1;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        mrp_State *state = mrp_open();
        Reader reader = {cases[i].source, strlen2(cases[i].source)};
        int status, passed;
        const char *error;
        if (!state) {
            fprintf(stderr, "Cannot create power probe state\n");
            return 0;
        }
        if (cases[i].flags & GLOBAL) mrp_register(state, "__op", numeric_power);
        if (cases[i].flags & NONFUNCTION) {
            mrp_pushnumber(state, 42);
            mrp_setglobal(state, "__op");
        }
        if (cases[i].flags & LEFT) power_table(state, "a", "__op", left_power);
        if (cases[i].flags & RIGHT) power_table(state, "b", "__op", right_power);
        if (cases[i].flags & WRONG_NAME) power_table(state, "a", "__pow", left_power);
        power_calls = 0;
        power_args_ok = 0;
        status = mrp_load(state, read_chunk, &reader, "@power-probe");
        if (status) {
            fprintf(stderr, "%s compile failed (%d): %s\n", cases[i].name,
                    status, mrp_tostring(state, -1));
            mrp_close(state);
            return 0;
        }
        status = mrp_pcall(state, 0, 1, 0);
        if (cases[i].error) {
            error = mrp_tostring(state, -1);
            passed = status == MRP_ERRRUN && error &&
                     strstr2(error, cases[i].error);
            printf("power %s: error %d: %s\n", cases[i].name, status,
                   error ? error : "<non-string>");
        } else {
            passed = status == 0 && mrp_type(state, -1) == MRP_TNUMBER &&
                     mrp_tonumber(state, -1) == cases[i].expected;
            if (status == 0 && mrp_type(state, -1) == MRP_TNUMBER)
                printf("power %s: %d\n", cases[i].name, (int)mrp_tonumber(state, -1));
            else
                printf("power %s: unexpected status %d\n", cases[i].name, status);
        }
        passed = passed && power_calls == cases[i].calls &&
                 (!power_calls || power_args_ok);
        if (!passed) {
            fprintf(stderr, "Power case %s failed (calls=%d, args_ok=%d)\n",
                    cases[i].name, power_calls, power_args_ok);
            ok = 0;
        }
        mrp_close(state);
    }
    return ok;
}

int main(int argc, char **argv) {
    static const char source[] =
        "local x,y=6,3; return x & y, x | y, x ^ y, ~x, "
        "_and(x,y), _or(x,y), _xor(x,y), _not(x), _mod(x,4)";
    Reader reader = {source, sizeof(source) - 1};
    mrp_State *state;
    Writer writer;
    FILE *input;
    char *bytes;
    long length;
    int status, close_status;

    if (argc != 2) {
        fprintf(stderr, "Usage: mrp_vm_probe <new-bytecode-output>\n");
        return 1;
    }
    mr_tm_init();
    state = mrp_open();
    if (!state) {
        fprintf(stderr, "Failed to create VM state\n");
        return 1;
    }
    mrp_register(state, "_and", c_and);
    mrp_register(state, "_or", c_or);
    mrp_register(state, "_xor", c_xor);
    mrp_register(state, "_not", c_not);
    mrp_register(state, "_mod", c_mod);

    status = mrp_load(state, read_chunk, &reader, "@probe.mr");
    if (status) {
        fprintf(stderr, "Source parse failed (%d): %s\n", status, mrp_tostring(state, -1));
        mrp_close(state);
        return 1;
    }
    writer.file = fopen(argv[1], "wbx");
    writer.failed = 0;
    if (!writer.file) {
        perror("Cannot create new bytecode output");
        mrp_close(state);
        return 1;
    }
    status = mrp_dump(state, write_chunk, &writer);
    close_status = fclose(writer.file);
    if (status != 1 || writer.failed || close_status) {
        fprintf(stderr, "Bytecode dump failed; output may be incomplete\n");
        mrp_close(state);
        return 1;
    }
    if (!check_results(state, "source")) {
        mrp_close(state);
        return 1;
    }

    input = fopen(argv[1], "rb");
    if (!input) {
        perror("Cannot reopen bytecode");
        mrp_close(state);
        return 1;
    }
    if (fseek(input, 0, SEEK_END) || (length = ftell(input)) <= 0 ||
        length > 65536 || fseek(input, 0, SEEK_SET)) {
        fprintf(stderr, "Invalid generated bytecode size\n");
        fclose(input);
        mrp_close(state);
        return 1;
    }
    bytes = (char *)malloc((size_t)length);
    if (!bytes) {
        fprintf(stderr, "Cannot allocate bytecode buffer\n");
        fclose(input);
        mrp_close(state);
        return 1;
    }
    status = fread(bytes, 1, (size_t)length, input) == (size_t)length;
    close_status = fclose(input);
    if (!status || close_status) {
        fprintf(stderr, "Cannot read generated bytecode\n");
        free(bytes);
        mrp_close(state);
        return 1;
    }
    reader.data = bytes;
    reader.length = (size_t)length;
    status = mrp_load(state, read_chunk, &reader, "@probe.mr");
    free(bytes);
    if (status) {
        fprintf(stderr, "Bytecode load failed (%d): %s\n", status, mrp_tostring(state, -1));
        mrp_close(state);
        return 1;
    }
    status = check_results(state, "bytecode");
    mrp_close(state);
    if (status) status = check_power_cases();
    return status ? 0 : 1;
}
