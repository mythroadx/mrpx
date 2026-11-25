#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "vmrp/bridge.h"
#include "vmrp/controls.h"
#include "vmrp/edit.h"
#include "vmrp/memory.h"

static int eventCount;
static int lastButton;
static bool reopen;
static bool failAllocation;
static const char title[] = {0, 'T', 0, 0};
static const char initial[] = {0, 'A', 0, 0};
static char lastMessage[200];

void *my_mallocExt(uint32 length) {
    return failAllocation ? NULL : malloc(length);
}
void my_freeExt(void *memory) { free(memory); }
int wstrlen(char *text) {
    int bytes = 0;
    while (text[bytes] || text[bytes + 1]) bytes += 2;
    return bytes;
}
void consoleMessage(const char *text) {
    SDL_strlcpy(lastMessage, text, sizeof(lastMessage));
}
void controlsEditChanged(const char *caption, const char *text, int type, int limit) {
    if (caption != NULL) {
        assert(strcmp(caption, "T") == 0);
        assert(strcmp(text, "A") == 0);
    }
}
int32_t event(int32_t code, int32_t p0, int32_t p1) {
    assert(code == MR_DIALOG_EVENT && p1 == 0);
    assert(!editWaiting());
    eventCount++;
    lastButton = p0;
    if (reopen) {
        reopen = false;
        assert(editCreate(title, initial, MR_EDIT_ANY, 8) == 1234);
    }
    return MR_SUCCESS;
}

int main(void) {
    assert(submitEditText("x") == MR_FAILED);
    assert(cancelEdit() == MR_FAILED);
    assert(eventCount == 0);
    assert(editCreate(title, initial, MR_EDIT_ANY, 2) == 1234);
    assert(memcmp(editGetText(1234), initial, sizeof(initial)) == 0);
    uint32_t generation = editGeneration();
    assert(submitEditText("abc") == MR_FAILED);
    assert(submitEditText("\xc0\xaf") == MR_FAILED);
    assert(submitEditText("\xe4\xb8") == MR_FAILED);
    assert(submitEditText("\xf0\x9f\x98\x80") == MR_FAILED);
    assert(editWaiting() && eventCount == 0 && editGeneration() == generation);
    assert(submitEditText("\xe4\xb8\xad" "A") == MR_SUCCESS);
    const char expected[] = {0x4e, 0x2d, 0, 'A', 0, 0};
    assert(memcmp(editGetText(1234), expected, sizeof(expected)) == 0);
    assert(lastButton == MR_DIALOG_KEY_OK && eventCount == 1);
    assert(submitEditText("x") == MR_FAILED);
    assert(eventCount == 1);
    assert(editRelease(1234) == MR_SUCCESS && editGetText(1234) == NULL);
    assert(editCreate(title, initial, MR_EDIT_ANY, 3) == 1234);
    assert(submitEditText("A\nB") == MR_FAILED && editWaiting());
    assert(submitEditText("A\n") == MR_SUCCESS);
    assert(memcmp(editGetText(1234), "\0A\0\r\0\n\0", 8) == 0);
    assert(editCreate(title, initial, MR_EDIT_ANY, 3) == 1234);
    assert(submitEditText("A\r\n") == MR_SUCCESS);
    assert(memcmp(editGetText(1234), "\0A\0\r\0\n\0", 8) == 0);

    assert(editCreate(title, initial, MR_EDIT_NUMERIC, 3) == 1234);
    assert(submitEditText("1x") == MR_FAILED && editWaiting());
    assert(submitEditText("123") == MR_SUCCESS);
    assert(editCreate(title, initial, MR_EDIT_PASSWORD, 10) == 1234);
    failAllocation = true;
    assert(submitEditText("secret") == MR_FAILED && editWaiting());
    failAllocation = false;
    assert(submitEditText("secret") == MR_SUCCESS);
    assert(strstr(lastMessage, "secret") == NULL);

    assert(editCreate(title, initial, MR_EDIT_ANY, 0) == 1234);
    assert(submitEditText("") == MR_SUCCESS);
    assert(memcmp(editGetText(1234), "\0", 2) == 0);
    assert(editCreate(title, initial, MR_EDIT_ANY, 8) == 1234);
    reopen = true;
    assert(cancelEdit() == MR_SUCCESS);
    assert(lastButton == MR_DIALOG_KEY_CANCEL && editWaiting());
    assert(editGeneration() != generation);
    assert(editRelease(1234) == MR_SUCCESS);
    assert(editCreate(title, initial, MR_EDIT_ANY, -1) == MR_FAILED);
    assert(editCreate(title, initial, 99, 10) == MR_FAILED);
    puts("edit tests passed");
    return 0;
}
