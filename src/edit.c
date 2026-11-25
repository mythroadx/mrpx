#include <SDL2/SDL.h>
#include "vmrp/bridge.h"
#include "vmrp/controls.h"
#include "vmrp/edit.h"
#include "vmrp/memory.h"
#include "vmrp/vmrp.h"

static bool waiting;
static uint32_t generation;
static int editType;
static int maxSize;
static char *heldText;

bool editWaiting(void) {
    return waiting;
}

uint32_t editGeneration(void) {
    return generation;
}

static int rejectInput(const char *message) {
    consoleMessage(message);
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", message);
    return MR_FAILED;
}

int32_t editCreate(const char *title, const char *text, int32_t type, int32_t limit) {
    if (limit < 0 || type < MR_EDIT_ANY || type > MR_EDIT_PASSWORD) {
        return rejectInput("Invalid MRP edit type or character limit.");
    }
    size_t bytes = (size_t)wstrlen((char *)text) + 2;
    char *utfTitle = SDL_iconv_string("UTF-8", "UCS-2BE", title,
                                     (size_t)wstrlen((char *)title) + 2);
    char *utfText = SDL_iconv_string("UTF-8", "UCS-2BE", text, bytes);
    char *copy = my_mallocExt((uint32_t)bytes);
    if (utfTitle == NULL || utfText == NULL || copy == NULL) {
        SDL_free(utfTitle);
        SDL_free(utfText);
        if (copy != NULL) my_freeExt(copy);
        return rejectInput("Could not allocate MRP edit text.");
    }
    memcpy(copy, text, bytes);
    if (heldText != NULL) my_freeExt(heldText);
    heldText = copy;
    editType = type;
    maxSize = limit;
    waiting = true;
    generation++;
    controlsEditChanged(utfTitle, utfText, type, limit);
    SDL_free(utfTitle);
    SDL_free(utfText);
    return 1234;
}

int32 editRelease(int32 edit) {
    waiting = false;
    generation++;
    if (heldText != NULL) my_freeExt(heldText);
    heldText = NULL;
    controlsEditChanged(NULL, NULL, 0, 0);
    return MR_SUCCESS;
}

char *editGetText(int32 edit) {
    return heldText;
}

EM_PORT_API(int) submitEditText(const char *utf8) {
    if (!waiting) return rejectInput("The application is not waiting for text input.");
    size_t length = strlen(utf8);
    if (length > (SIZE_MAX - 2) / 2) return rejectInput("Input is too large.");
    for (size_t i = 0; i < length;) {
        unsigned char first = (unsigned char)utf8[i];
        size_t width = first < 0x80 ? 1 :
                       first >= 0xc2 && first <= 0xdf ? 2 :
                       first >= 0xe0 && first <= 0xef ? 3 : 0;
        if (width == 0 || width > length - i) {
            return rejectInput("Input must be valid UTF-8 representing UCS-2 characters.");
        }
        for (size_t j = 1; j < width; j++) {
            if (((unsigned char)utf8[i + j] & 0xc0) != 0x80) {
                return rejectInput("Input contains invalid UTF-8 continuation bytes.");
            }
        }
        if (width == 3 && ((first == 0xe0 && (unsigned char)utf8[i + 1] < 0xa0) ||
                           (first == 0xed && (unsigned char)utf8[i + 1] >= 0xa0))) {
            return rejectInput("Input contains overlong UTF-8 or a surrogate character.");
        }
        i += width;
    }
    char *converted = SDL_malloc(length * 2 + 2);
    SDL_iconv_t converter = SDL_iconv_open("UCS-2BE", "UTF-8");
    if (converted == NULL || converter == (SDL_iconv_t)-1) {
        SDL_free(converted);
        if (converter != (SDL_iconv_t)-1) SDL_iconv_close(converter);
        return rejectInput("Could not allocate input conversion.");
    }
    const char *input = utf8;
    char *output = converted;
    size_t remaining = length;
    size_t available = length * 2;
    size_t status = length == 0 ? 0 : SDL_iconv(converter, &input, &remaining, &output, &available);
    SDL_iconv_close(converter);
    size_t bytes = (size_t)(output - converted);
    if (status == SDL_ICONV_ERROR || status == SDL_ICONV_E2BIG ||
        status == SDL_ICONV_EILSEQ || status == SDL_ICONV_EINVAL || remaining != 0) {
        SDL_free(converted);
        return rejectInput("Input is not valid UTF-8/UCS-2 text.");
    }
    size_t units = bytes / 2;
    for (size_t i = 0; i < bytes; i += 2) {
        if (converted[i] == 0 && converted[i + 1] == '\n' &&
            (i < 2 || converted[i - 2] != 0 || converted[i - 1] != '\r')) units++;
    }
    if (units > (size_t)maxSize || units > (UINT32_MAX - 2) / 2) {
        SDL_free(converted);
        return rejectInput("Input exceeds the MRP character limit.");
    }
    if (editType == MR_EDIT_NUMERIC) {
        for (size_t i = 0; i < bytes; i += 2) {
            if (converted[i] != 0 || converted[i + 1] < '0' || converted[i + 1] > '9') {
                SDL_free(converted);
                return rejectInput("This MRP input accepts digits only.");
            }
        }
    }
    char *copy = my_mallocExt((uint32_t)units * 2 + 2);
    if (copy == NULL) {
        SDL_free(converted);
        return rejectInput("Could not allocate Guest input text.");
    }
    size_t position = 0;
    for (size_t i = 0; i < bytes; i += 2) {
        if (converted[i] == 0 && converted[i + 1] == '\n' &&
            (i < 2 || converted[i - 2] != 0 || converted[i - 1] != '\r')) {
            copy[position++] = 0;
            copy[position++] = '\r';
        }
        copy[position++] = converted[i];
        copy[position++] = converted[i + 1];
    }
    copy[position] = copy[position + 1] = 0;
    SDL_free(converted);
    if (heldText != NULL) my_freeExt(heldText);
    heldText = copy;
    waiting = false;
    controlsEditChanged(NULL, NULL, 0, 0);
    consoleMessage("Input submitted.");
    event(MR_DIALOG_EVENT, MR_DIALOG_KEY_OK, 0);
    return MR_SUCCESS;
}

EM_PORT_API(int) cancelEdit(void) {
    if (!waiting) return rejectInput("The application is not waiting for text input.");
    waiting = false;
    controlsEditChanged(NULL, NULL, 0, 0);
    consoleMessage("Input cancelled.");
    event(MR_DIALOG_EVENT, MR_DIALOG_KEY_CANCEL, 0);
    return MR_SUCCESS;
}
