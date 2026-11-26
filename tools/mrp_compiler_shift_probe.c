/* Reduced test for the table-size expression used by the archived VM. */
#include <stdio.h>

int mrp_probe_array_size(int n) {
    return n == -1 ? 0 : 1 << n;
}

int main(void) {
    volatile int n = -1;
    int result = mrp_probe_array_size(n);
    printf("n=%d size=%d (expected 0)\n", n, result);
    return result != 0;
}
