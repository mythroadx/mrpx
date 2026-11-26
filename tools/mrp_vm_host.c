#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "mr.h"
#include "mem.h"

_Static_assert(sizeof(void *) == 4, "Mythroad VM requires a 32-bit target");
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
