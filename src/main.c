#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef _WIN32
#include <direct.h>
#else
#include <unistd.h>
#endif

#include "vmrp/bridge.h"
#include "vmrp/vmrp.h"
#include "vmrp/memory.h"
#include "vmrp/dispatch.h"
#include "vmrp/network.h"
#include "vmrp/controls.h"
#include "vmrp/edit.h"
#include "vmrp/display.h"

#include <SDL2/SDL.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#define MOUSE_DOWN 2
#define MOUSE_UP 3
#define MOUSE_MOVE 12

// http://wiki.libsdl.org/Tutorials
// http://lazyfoo.net/tutorials/SDL/index.php

static SDL_Window *window;
static bool isMouseDown = false;

static SDL_Keycode isKeyDown = SDLK_UNKNOWN;
static int heldDirection = MR_KEY_NONE;
#ifndef __EMSCRIPTEN__
static char *runtimeDirectory;
static bool interactive;
static int openNativePackage(const char *path, const char *moduleName);
#endif

void guiDrawBitmap(uint16_t *bmp, int32_t x, int32_t y, int32_t w, int32_t h) {
    if (!drawDisplayBitmap(bmp, x, y, w, h))
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not draw Guest display: %s", SDL_GetError());
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
void setEventEnable(int v) {
    int state = v ? SDL_ENABLE : SDL_DISABLE;
    SDL_EventState(SDL_TEXTINPUT, state);
    SDL_EventState(SDL_KEYDOWN, state);
    SDL_EventState(SDL_KEYUP, state);
    SDL_EventState(SDL_MOUSEMOTION, state);
    SDL_EventState(SDL_MOUSEBUTTONDOWN, state);
    SDL_EventState(SDL_MOUSEBUTTONUP, state);
}
#endif

static void directionEvent(int type, int key) {
    if (type == MR_KEY_PRESS) heldDirection = displayKey(key);
    int guestKey = heldDirection;
    if (type == MR_KEY_RELEASE) heldDirection = MR_KEY_NONE;
    if (guestKey != MR_KEY_NONE) event(type, guestKey, 0);
}

static void mouseEvent(int type, int x, int y) {
    int guestX, guestY;
    displayPoint(x, y, &guestX, &guestY);
    event(type, guestX, guestY);
}

static void keyEvent(int16 type, SDL_Keycode code) {
    if (code >= SDLK_0 && code <= SDLK_9) {
        int32_t key = MR_KEY_0 + (code - SDLK_0);
        event(type, key, 0);  // 按键 0-9
        return;
    }
    switch (code) {
        case SDLK_KP_0:
            event(type, MR_KEY_0, 0);
            break;
        case SDLK_KP_1:
            event(type, MR_KEY_1, 0);
            break;
        case SDLK_KP_2:
            event(type, MR_KEY_2, 0);
            break;
        case SDLK_KP_3:
            event(type, MR_KEY_3, 0);
            break;
        case SDLK_KP_4:
            event(type, MR_KEY_4, 0);
            break;
        case SDLK_KP_5:
            event(type, MR_KEY_5, 0);
            break;
        case SDLK_KP_6:
            event(type, MR_KEY_6, 0);
            break;
        case SDLK_KP_7:
            event(type, MR_KEY_7, 0);
            break;
        case SDLK_KP_8:
            event(type, MR_KEY_8, 0);
            break;
        case SDLK_KP_9:
            event(type, MR_KEY_9, 0);
            break;
        case SDLK_KP_ENTER:
        case SDLK_RETURN:                   // 回车键
            event(type, MR_KEY_SELECT, 0);  // 确认/选择/ok
            break;
        case SDLK_EQUALS:                  // 等号
            event(type, MR_KEY_POUND, 0);  // 按键 #
            break;
        case SDLK_MINUS:                  // 减号
            event(type, MR_KEY_STAR, 0);  // 按键 *
            break;
        case SDLK_w:
        case SDLK_UP:  // 上
            directionEvent(type, MR_KEY_UP);
            break;
        case SDLK_s:
        case SDLK_DOWN:  // 下
            directionEvent(type, MR_KEY_DOWN);
            break;
        case SDLK_a:
        case SDLK_LEFT:  // 左
            directionEvent(type, MR_KEY_LEFT);
            break;
        case SDLK_d:
        case SDLK_RIGHT:  // 右
            directionEvent(type, MR_KEY_RIGHT);
            break;
        case SDLK_q:
        case SDLK_LEFTBRACKET:                // 左中括号
            event(type, MR_KEY_SOFTLEFT, 0);  // 左功能键
            break;
        case SDLK_e:
        case SDLK_RIGHTBRACKET:                // 右中括号
            event(type, MR_KEY_SOFTRIGHT, 0);  // 右功能键
            break;
        case SDLK_TAB:
            event(type, MR_KEY_SEND, 0);  // 接听键
            break;
        case SDLK_ESCAPE:
            event(type, MR_KEY_POWER, 0);  // 挂机键
            break;
        default:
            SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "input: key=%d", code);
            break;
    }
}

static bool handleEvent(SDL_Event *ev) {
#ifndef __EMSCRIPTEN__
    if (ev->type == SDL_DROPFILE) {
        if (openNativePackage(ev->drop.file, NULL) != MR_SUCCESS) {
            consoleMessage(SDL_GetError());
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Could not open MRP", SDL_GetError(), window);
        }
        SDL_free(ev->drop.file);
        return true;
    }
    if (ev->type == SDL_KEYDOWN && ev->key.keysym.sym == SDLK_o &&
        (ev->key.keysym.mod & KMOD_CTRL)) {
        if (!ev->key.repeat) openPackageDialog();
        return true;
    }
#endif
    if (handleControlEvent(ev)) return true;
    if (dispatchRuntimeEvent(ev)) {
        return !runtimeDispatchFailed();
    }
    if (ev->type == SDL_QUIT) {
        return false;
    }
    if (ev->type == SDL_WINDOWEVENT && ev->window.windowID == SDL_GetWindowID(window) &&
        (ev->window.event == SDL_WINDOWEVENT_EXPOSED || ev->window.event == SDL_WINDOWEVENT_SIZE_CHANGED)) {
        if (!redrawDisplay()) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not redraw Guest display: %s", SDL_GetError());
            return false;
        }
    }
    if (ev->type == SDL_KEYDOWN && ev->key.keysym.sym == SDLK_F1) {
        if (!ev->key.repeat) toggleControls();
        return true;
    }
    if (ev->type == SDL_WINDOWEVENT && ev->window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
        if (isKeyDown != SDLK_UNKNOWN) {
            keyEvent(MR_KEY_RELEASE, isKeyDown);
            isKeyDown = SDLK_UNKNOWN;
        }
        if (isMouseDown) {
            int x, y;
            SDL_GetMouseState(&x, &y);
            mouseEvent(MR_MOUSE_UP, x, y);
            isMouseDown = false;
        }
    }
    if (editWaiting() && ev->type == SDL_KEYDOWN && (SDL_GetModState() & KMOD_CTRL)) {
        if (ev->key.keysym.sym == SDLK_z) {
            cancelEdit();
            return true;
        } else if (ev->key.keysym.sym == SDLK_v) {
            char *str = SDL_GetClipboardText();
            if (str == NULL) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not read clipboard: %s", SDL_GetError());
                return true;
            }
            submitEditText(str);
            SDL_free(str);
            return true;
        }
        if (editWaiting()) {
            if (ev->type == SDL_KEYUP && isKeyDown == ev->key.keysym.sym) {
                isKeyDown = SDLK_UNKNOWN;
                keyEvent(MR_KEY_RELEASE, ev->key.keysym.sym);
            }
            return true;
        }
    }
    switch (ev->type) {
        case SDL_KEYDOWN:
            if (isKeyDown == SDLK_UNKNOWN) {
                isKeyDown = ev->key.keysym.sym;
                keyEvent(MR_KEY_PRESS, ev->key.keysym.sym);
            }
            break;
        case SDL_KEYUP:
            if (isKeyDown == ev->key.keysym.sym) {
                isKeyDown = SDLK_UNKNOWN;
                keyEvent(MR_KEY_RELEASE, ev->key.keysym.sym);
            }
            break;
        case SDL_MOUSEMOTION:
            if (isMouseDown) {
                mouseEvent(MR_MOUSE_MOVE, ev->motion.x, ev->motion.y);
            }
            break;
        case SDL_MOUSEBUTTONDOWN:
            isMouseDown = true;
            mouseEvent(MR_MOUSE_DOWN, ev->button.x, ev->button.y);
            break;
        case SDL_MOUSEBUTTONUP:
            isMouseDown = false;
            mouseEvent(MR_MOUSE_UP, ev->button.x, ev->button.y);
            break;
    }
    return true;
}

static bool pumpEvents(uint32_t waitMs) {
    pumpControls();
    if (runtimeDispatchFailed() || controlsHaveFailed()) {
        return false;
    }
    SDL_Event ev;
    if (waitMs > 0 && SDL_WaitEventTimeout(&ev, (int)waitMs)) {
        if (!handleEvent(&ev)) {
            return false;
        }
    }
    while (SDL_PollEvent(&ev)) {
        if (!handleEvent(&ev)) {
            return false;
        }
    }
    return !runtimeDispatchFailed() && !controlsHaveFailed();
}

static bool waitForCapture(uint32_t delay) {
    uint32_t start = SDL_GetTicks();
    do {
        uint32_t elapsed = SDL_GetTicks() - start;
        uint32_t remaining = elapsed < delay ? delay - elapsed : 0;
        if (!pumpEvents(remaining < 50 ? remaining : 50)) {
            fprintf(stderr, "Screenshot capture interrupted by quit or runtime dispatch failure.\n");
            return false;
        }
    } while (SDL_GetTicks() - start < delay);
    return true;
}

static void shutdownRuntime(void) {
    destroyControls();
    editRelease(1234);
    stopRuntimeDispatch();
    my_closeNetwork();
    stopVmrp();
#ifndef __EMSCRIPTEN__
    SDL_free(runtimeDirectory);
    runtimeDirectory = NULL;
#endif
    SDL_DestroyWindow(window);
    window = NULL;
    SDL_Quit();
}

void loop(void) {
#ifdef __EMSCRIPTEN__
    if (!pumpEvents(0)) {
        emscripten_cancel_main_loop();
        shutdownRuntime();
    }
#else
    while (pumpEvents(50)) {
    }
#endif
}

static int setRuntimeDirectory(void) {
#ifdef __EMSCRIPTEN__
    return 0;
#else
    const char *runtimePath = getenv("VMRP_RUNTIME_DIR");
    char *basePath = NULL;

    if (runtimePath == NULL || runtimePath[0] == '\0') {
        basePath = SDL_GetBasePath();
        if (basePath == NULL) {
            return -1;
        }

        runtimePath = basePath;
    }

#ifdef _WIN32
    wchar_t *wide = (wchar_t *)SDL_iconv_string("UTF-16LE", "UTF-8", runtimePath, strlen(runtimePath) + 1);
    int result = wide ? _wchdir(wide) : -1;
    SDL_free(wide);
#else
    int result = chdir(runtimePath);
#endif
    if (result != 0) {
        perror("Failed to set runtime directory");
    }
    SDL_free(basePath);
    if (result == 0) {
#ifdef _WIN32
        wchar_t *current = _wgetcwd(NULL, 0);
        if (current != NULL) {
            runtimeDirectory = SDL_iconv_string("UTF-8", "UTF-16LE", (char *)current,
                                                (wcslen(current) + 1) * sizeof(*current));
            free(current);
        }
#else
        char *current = getcwd(NULL, 0);
        if (current != NULL) {
            runtimeDirectory = SDL_strdup(current);
            free(current);
        }
#endif
        if (runtimeDirectory == NULL) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not resolve runtime directory.");
            return -1;
        }
        for (char *p = runtimeDirectory; *p; p++) {
            if (*p == '\\') *p = '/';
        }
    }
    return result;
#endif
}

#ifndef __EMSCRIPTEN__
static int showIdleScreen(void) {
    if (!clearDisplay()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not display idle screen: %s", SDL_GetError());
        return MR_FAILED;
    }
    SDL_SetWindowTitle(window, "mrpx - Open MRP (Ctrl+O)");
    consoleMessage("No MRP selected. Open an MRP from the mythroad folder (Ctrl+O), or drag it here.");
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "No MRP selected; waiting for manual selection.");
    return MR_SUCCESS;
}

static int openNativePackage(const char *path, const char *moduleName) {
    char *normalized = SDL_strdup(path);
    if (normalized == NULL) return SDL_SetError("Could not allocate MRP path.");
    for (char *p = normalized; *p; p++) {
        if (*p == '\\') *p = '/';
    }
    const char *name = normalized;
    size_t rootLength = strlen(runtimeDirectory);
#ifdef _WIN32
    bool underRoot = SDL_strncasecmp(name, runtimeDirectory, rootLength) == 0;
#else
    bool underRoot = strncmp(name, runtimeDirectory, rootLength) == 0;
#endif
    if (underRoot && name[rootLength] == '/') name += rootLength + 1;
    if (strncmp(name, "./", 2) == 0) name += 2;
    if (strncmp(name, "mythroad/", 9) == 0) name += 9;
    size_t length = strlen(name);
    if (strchr(name, '/') || strchr(name, ':') || length <= 4 ||
        SDL_strcasecmp(name + length - 4, ".mrp") != 0 || length >= 256) {
        SDL_free(normalized);
        return SDL_SetError("Choose an MRP directly inside %s/mythroad.", runtimeDirectory);
    }
    char filename[sizeof("mythroad/") + 256];
    SDL_snprintf(filename, sizeof(filename), "mythroad/%s", name);
    SDL_RWops *file = SDL_RWFromFile(filename, "rb");
    char magic[4];
    bool valid = file != NULL && SDL_RWread(file, magic, 1, sizeof(magic)) == sizeof(magic) &&
                 memcmp(magic, "MRPG", sizeof(magic)) == 0;
    if (file != NULL) SDL_RWclose(file);
    if (!valid) {
        SDL_SetError("Missing or invalid MRP: %s", filename);
        SDL_free(normalized);
        return MR_FAILED;
    }
    destroyControls();
    editRelease(1234);
    stopRuntimeDispatch();
    my_closeNetwork();
    stopVmrp();
    isKeyDown = SDLK_UNKNOWN;
    heldDirection = MR_KEY_NONE;
    isMouseDown = false;
    if (initRuntimeDispatch() != MR_SUCCESS || (interactive && !initControls(window))) {
        SDL_free(normalized);
        return SDL_SetError("Could not initialize the application runtime.");
    }
    if (!clearDisplay()) {
        SDL_free(normalized);
        return MR_FAILED;
    }
    int result = startVmrp(name, moduleName);
    if (result == MR_SUCCESS) {
        SDL_SetWindowTitle(window, name);
    } else {
        editRelease(1234);
        stopRuntimeDispatch();
        my_closeNetwork();
        stopVmrp();
        if (initRuntimeDispatch() != MR_SUCCESS) {
            SDL_free(normalized);
            return SDL_SetError("Could not restore the idle runtime.");
        }
        if (showIdleScreen() != MR_SUCCESS) consoleMessage(SDL_GetError());
        SDL_SetError("Could not start %s. See Console for details.", name);
    }
    SDL_free(normalized);
    return result;
}
#endif

static void printUsage(const char *program) {
    printf("Usage: %s [--verbose] [--mrp package.mrp] [--module name.mr] [--screenshot image.bmp] [--capture-delay ms] [--right-softkey-after ms] [--click-right-after ms] [--sound-off-after ms]\n", program);
}

int main(int argc, char *args[]) {
    const char *mrpPath = NULL;
    const char *moduleName = NULL;
    const char *screenshotPath = NULL;
    uint32_t captureDelay = 1200;
    int32_t rightSoftkeyAfter = -1;
    int32_t clickRightAfter = -1;
    int32_t soundOffAfter = -1;
    bool verbose = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(args[i], "--verbose") == 0) {
            verbose = true;
        } else if (strcmp(args[i], "--mrp") == 0 && i + 1 < argc) {
            mrpPath = args[++i];
        } else if (strcmp(args[i], "--module") == 0 && i + 1 < argc) {
            moduleName = args[++i];
        } else if (strcmp(args[i], "--screenshot") == 0 && i + 1 < argc) {
            screenshotPath = args[++i];
        } else if (strcmp(args[i], "--capture-delay") == 0 && i + 1 < argc) {
            char *end = NULL;
            long delay = strtol(args[++i], &end, 10);
            if (end == args[i] || *end != '\0' || delay < 0 || delay > 60000) {
                printUsage(args[0]);
                return EXIT_FAILURE;
            }
            captureDelay = (uint32_t)delay;
        } else if (strcmp(args[i], "--right-softkey-after") == 0 && i + 1 < argc) {
            char *end = NULL;
            long delay = strtol(args[++i], &end, 10);
            if (end == args[i] || *end != '\0' || delay < 0 || delay > 60000) {
                printUsage(args[0]);
                return EXIT_FAILURE;
            }
            rightSoftkeyAfter = (int32_t)delay;
        } else if (strcmp(args[i], "--click-right-after") == 0 && i + 1 < argc) {
            char *end = NULL;
            long delay = strtol(args[++i], &end, 10);
            if (end == args[i] || *end != '\0' || delay < 0 || delay > 60000) {
                printUsage(args[0]);
                return EXIT_FAILURE;
            }
            clickRightAfter = (int32_t)delay;
        } else if (strcmp(args[i], "--sound-off-after") == 0 && i + 1 < argc) {
            char *end = NULL;
            long delay = strtol(args[++i], &end, 10);
            if (end == args[i] || *end != '\0' || delay < 0 || delay > 60000) {
                printUsage(args[0]);
                return EXIT_FAILURE;
            }
            soundOffAfter = (int32_t)delay;
        } else {
            printUsage(args[0]);
            return EXIT_FAILURE;
        }
    }
#ifdef __EMSCRIPTEN__
    if (screenshotPath != NULL && mrpPath == NULL) {
        fprintf(stderr, "--screenshot requires --mrp.\n");
        printUsage(args[0]);
        return EXIT_FAILURE;
    }
#else
    if (mrpPath == NULL) mrpPath = "dsm_gm.mrp";
#endif
    if (rightSoftkeyAfter >= 0 && screenshotPath == NULL) {
        fprintf(stderr, "--right-softkey-after requires --screenshot.\n");
        printUsage(args[0]);
        return EXIT_FAILURE;
    }
    if (clickRightAfter >= 0 && screenshotPath == NULL) {
        fprintf(stderr, "--click-right-after requires --screenshot.\n");
        printUsage(args[0]);
        return EXIT_FAILURE;
    }
    if (soundOffAfter >= 0 && screenshotPath == NULL) {
        fprintf(stderr, "--sound-off-after requires --screenshot.\n");
        return EXIT_FAILURE;
    }
    if ((rightSoftkeyAfter >= 0) + (clickRightAfter >= 0) + (soundOffAfter >= 0) > 1) {
        fprintf(stderr, "Choose one screenshot input action.\n");
        printUsage(args[0]);
        return EXIT_FAILURE;
    }

    SDL_LogSetPriority(SDL_LOG_CATEGORY_APPLICATION, verbose ? SDL_LOG_PRIORITY_DEBUG : SDL_LOG_PRIORITY_INFO);
    bridgeSetGuestTerminalLogging(verbose || screenshotPath != NULL);
    SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION,
                 "memory: code=0x%X+0x%X stack=0x%X+0x%X heap=0x%X+0x%X total=0x%X",
                 CODE_ADDRESS, CODE_SIZE, STACK_ADDRESS, STACK_SIZE,
                 MEMORY_MANAGER_ADDRESS, MEMORY_MANAGER_SIZE, TOTAL_MEMORY);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) < 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL could not initialize: %s", SDL_GetError());
        return -1;
    }

    Uint32 windowFlags = screenshotPath ? SDL_WINDOW_HIDDEN : 0;
#ifndef __linux__
    if (screenshotPath == NULL) windowFlags |= SDL_WINDOW_OPENGL;
#endif
    window = SDL_CreateWindow("mrpx", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, SCREEN_WIDTH, SCREEN_HEIGHT, windowFlags);
    if (window == NULL) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Window could not be created: %s", SDL_GetError());
        SDL_Quit();
        return -1;
    }

    if (!initDisplay(window)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not initialize Guest display: %s", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return EXIT_FAILURE;
    }

    if (setRuntimeDirectory() != 0) {
        SDL_DestroyWindow(window);
        SDL_Quit();
        return EXIT_FAILURE;
    }
#ifndef __EMSCRIPTEN__
    interactive = screenshotPath == NULL;
    SDL_EventState(SDL_DROPFILE, interactive ? SDL_ENABLE : SDL_DISABLE);
#endif
    if (initRuntimeDispatch() != MR_SUCCESS) {
        SDL_DestroyWindow(window);
        SDL_Quit();
        return EXIT_FAILURE;
    }
    if (screenshotPath == NULL && !initControls(window)) {
        fprintf(stderr, "Could not initialize keypad and Console controls: %s\n", SDL_GetError());
        shutdownRuntime();
        return EXIT_FAILURE;
    }
#ifdef __EMSCRIPTEN__
    int startResult = startVmrp(mrpPath, moduleName);
#else
    int startResult = openNativePackage(mrpPath, moduleName);
#endif
    if (startResult != MR_SUCCESS) {
        fprintf(stderr, "Could not start application: %s\n", SDL_GetError());
        shutdownRuntime();
        return EXIT_FAILURE;
    }

    if (screenshotPath != NULL) {
        if (soundOffAfter >= 0) {
            if (!waitForCapture((uint32_t)soundOffAfter)) {
                shutdownRuntime();
                return EXIT_FAILURE;
            }
            const int keys[] = { MR_KEY_DOWN, MR_KEY_SOFTLEFT };
            for (size_t key = 0; key < sizeof(keys) / sizeof(keys[0]); key++) {
                event(MR_KEY_PRESS, keys[key], 0);
                if (!waitForCapture(50)) {
                    shutdownRuntime();
                    return EXIT_FAILURE;
                }
                event(MR_KEY_RELEASE, keys[key], 0);
                if (!waitForCapture(150)) {
                    shutdownRuntime();
                    return EXIT_FAILURE;
                }
            }
            printf("Selected sound off for screenshot capture.\n");
        }
        if (rightSoftkeyAfter >= 0) {
            if (!waitForCapture((uint32_t)rightSoftkeyAfter)) {
                shutdownRuntime();
                return EXIT_FAILURE;
            }
            event(MR_KEY_PRESS, MR_KEY_SOFTRIGHT, 0);
            if (!waitForCapture(50)) {
                shutdownRuntime();
                return EXIT_FAILURE;
            }
            event(MR_KEY_RELEASE, MR_KEY_SOFTRIGHT, 0);
            printf("Sent right softkey for screenshot capture.\n");
        }
        if (clickRightAfter >= 0) {
            if (!waitForCapture((uint32_t)clickRightAfter)) {
                shutdownRuntime();
                return EXIT_FAILURE;
            }
            event(MR_MOUSE_DOWN, SCREEN_WIDTH - 10, SCREEN_HEIGHT - 10);
            if (!waitForCapture(50)) {
                shutdownRuntime();
                return EXIT_FAILURE;
            }
            event(MR_MOUSE_UP, SCREEN_WIDTH - 10, SCREEN_HEIGHT - 10);
            printf("Clicked lower-right softkey for screenshot capture.\n");
        }
        if (!waitForCapture(captureDelay)) {
            shutdownRuntime();
            return EXIT_FAILURE;
        }
        SDL_Surface *surface = SDL_GetWindowSurface(window);
        if (surface == NULL || SDL_SaveBMP(surface, screenshotPath) != 0) {
            fprintf(stderr, "Could not save screenshot '%s': %s\n", screenshotPath, SDL_GetError());
            shutdownRuntime();
            return EXIT_FAILURE;
        }
        printf("Saved screenshot: %s\n", screenshotPath);
        shutdownRuntime();
        return EXIT_SUCCESS;
    }

#if defined(__EMSCRIPTEN__)
    emscripten_set_main_loop(loop, 0, 1);
#else
    loop();
    bool failed = runtimeDispatchFailed() || controlsHaveFailed();
    shutdownRuntime();
    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
#endif
    return 0;
}
