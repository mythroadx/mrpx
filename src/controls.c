#if defined(_WIN32) && !defined(_WIN32_WINNT)
#define _WIN32_WINNT 0x0600
#endif
#include "vmrp/controls.h"
#include "vmrp/edit.h"
#include "vmrp/vmrp.h"
#include "vmrp/display.h"
#ifdef _WIN32
#include "vmrp/capture.h"
#endif

static Uint32 phoneType = (Uint32)-1;
static bool phoneFailed;
static uint32_t forwardedKeys;
static int forwardedKeyCodes[MR_KEY_NONE];

EM_PORT_API(int) queuePhoneKey(int pressed, int key) {
    if (phoneType == (Uint32)-1 || (pressed != 0 && pressed != 1) ||
        key < 0 || key >= MR_KEY_NONE) {
        consoleMessage("Phone keypad is not ready or the key is invalid.");
        return MR_FAILED;
    }
    SDL_Event ev;
    SDL_zero(ev);
    ev.type = phoneType;
    ev.user.code = pressed ? MR_KEY_PRESS : MR_KEY_RELEASE;
    ev.user.data1 = (void *)(intptr_t)key;
    int queued = SDL_PushEvent(&ev);
    if (queued != 1) {
        phoneFailed = true;
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not queue phone key: %s",
                     queued == 0 ? "event rejected by filter" : SDL_GetError());
        consoleMessage("Phone input queue failed.");
        return MR_FAILED;
    }
    return MR_SUCCESS;
}

static void deliverPhoneKey(int type, int key) {
    uint32_t mask = UINT32_C(1) << key;
    if (type == MR_KEY_PRESS) {
        if (editWaiting() || (forwardedKeys & mask)) return;
        forwardedKeys |= mask;
        forwardedKeyCodes[key] = displayKey(key);
    } else {
        if (!(forwardedKeys & mask)) return;
        forwardedKeys &= ~mask;
    }
    event(type, forwardedKeyCodes[key], 0);
}

static bool handlePhoneEvent(const SDL_Event *ev) {
    if (ev->type != phoneType) return false;
    deliverPhoneKey(ev->user.code, (int)(intptr_t)ev->user.data1);
    return true;
}

static void drainPhoneEvents(void) {
    if (phoneType != (Uint32)-1) SDL_FlushEvent(phoneType);
    phoneType = (Uint32)-1;
    forwardedKeys = 0;
}

#if !defined(__EMSCRIPTEN__) && (defined(_WIN32) || defined(VMRP_GTK))
enum { CONTROL_PRESS, CONTROL_RELEASE, CONTROL_SUBMIT, CONTROL_CANCEL, CONTROL_ROTATE,
       CONTROL_PHOTO, CONTROL_RECORD };
static Uint32 controlType = (Uint32)-1;
static bool controlFailed;
static bool closing;
static SDL_Window *guestWindow;
static void repositionControls(void);
#ifdef _WIN32
static void takePhoto(void);
static void toggleRecording(void);
#endif

static void queuePackageOpen(const char *path) {
    SDL_Event ev;
    SDL_zero(ev);
    ev.type = SDL_DROPFILE;
    ev.drop.file = SDL_strdup(path);
    if (ev.drop.file == NULL || SDL_PushEvent(&ev) != 1) {
        SDL_free(ev.drop.file);
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not queue MRP selection: %s", SDL_GetError());
        consoleMessage("Could not queue MRP selection.");
    }
}

static void queueControl(int code, intptr_t value) {
    if (closing) {
        if (code == CONTROL_SUBMIT) SDL_free((void *)value);
        return;
    }
    SDL_Event ev;
    SDL_zero(ev);
    ev.type = controlType;
    ev.user.code = code;
    ev.user.data1 = (void *)value;
    ev.user.data2 = (void *)(uintptr_t)editGeneration();
    if (SDL_PushEvent(&ev) != 1) {
        if (code == CONTROL_SUBMIT) SDL_free((void *)value);
        controlFailed = true;
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not queue control input: %s", SDL_GetError());
        consoleMessage("Input queue failed; restart the emulator.");
    }
}

bool handleControlEvent(const SDL_Event *ev) {
    if (handlePhoneEvent(ev)) return true;
    if (ev->type != controlType) return false;
    if (ev->user.code == CONTROL_PRESS || ev->user.code == CONTROL_RELEASE) {
        deliverPhoneKey(ev->user.code == CONTROL_PRESS ? MR_KEY_PRESS : MR_KEY_RELEASE,
                        (int)(intptr_t)ev->user.data1);
    } else if (ev->user.code == CONTROL_ROTATE) {
        if (!rotateDisplay()) {
            controlFailed = true;
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not rotate display: %s", SDL_GetError());
            consoleMessage("Could not rotate display.");
        } else {
            repositionControls();
            SDL_RaiseWindow(guestWindow);
        }
#ifdef _WIN32
    } else if (ev->user.code == CONTROL_PHOTO) {
        takePhoto();
    } else if (ev->user.code == CONTROL_RECORD) {
        toggleRecording();
#endif
    } else if ((uint32_t)(uintptr_t)ev->user.data2 != editGeneration()) {
        consoleMessage("Discarded input for an old edit request.");
    } else if (ev->user.code == CONTROL_SUBMIT) {
        submitEditText(ev->user.data1);
    } else if (ev->user.code == CONTROL_CANCEL) {
        cancelEdit();
    }
    if (ev->user.code == CONTROL_SUBMIT) SDL_free(ev->user.data1);
    return true;
}

bool controlsHaveFailed(void) { return controlFailed || phoneFailed; }

static void drainControlEvents(void) {
    drainPhoneEvents();
    SDL_Event ev;
    if (controlType != (Uint32)-1) {
        while (SDL_PeepEvents(&ev, 1, SDL_GETEVENT, controlType, controlType) == 1) {
            if (ev.user.code == CONTROL_SUBMIT) SDL_free(ev.user.data1);
        }
    }
}
#endif

#if defined(__EMSCRIPTEN__) || (!defined(_WIN32) && !defined(VMRP_GTK))
void pumpControls(void) {}
#endif

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

EM_JS(void, jsConsoleGuestLog, (const char *text), {
    var end = text;
    while (HEAPU8[end]) end++;
    var message = new TextDecoder('gbk').decode(HEAPU8.subarray(text, end));
    console.log(message);
    vmrpConsoleLog(message);
});
EM_JS(void, jsConsoleMessage, (const char *text), {
    vmrpConsoleLog(UTF8ToString(text));
});
EM_JS(void, jsControlsEditChanged, (const char *title, const char *text, int type, int maxSize), {
    vmrpEditChanged(title ? UTF8ToString(title) : null,
                    text ? UTF8ToString(text) : "", type, maxSize);
});
EM_JS(void, controlsReady, (int ready), { vmrpControlsReady(!!ready); });

void consoleGuestLog(const char *text) { jsConsoleGuestLog(text); }
void consoleMessage(const char *text) { jsConsoleMessage(text); }
void controlsEditChanged(const char *title, const char *text, int type, int maxSize) {
    jsControlsEditChanged(title, text, type, maxSize);
}

bool initControls(SDL_Window *screen) {
    phoneFailed = false;
    phoneType = SDL_RegisterEvents(1);
    if (phoneType == (Uint32)-1) return false;
    controlsReady(1);
    return true;
}
void destroyControls(void) { controlsReady(0); drainPhoneEvents(); }
void toggleControls(void) {}
bool handleControlEvent(const SDL_Event *ev) { return handlePhoneEvent(ev); }
bool controlsHaveFailed(void) { return phoneFailed; }

#elif defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <SDL2/SDL_syswm.h>

enum { INPUT_ID = 101, SEND_ID, CANCEL_ID, CLEAR_ID, COPY_ID, OPEN_ID, ROTATE_ID,
       PHOTO_ID, RECORD_ID };
static HWND panel, logBox, inputBox, passwordBox, prompt, sendButton, cancelButton;
static HWND recordButton;
static HWND heldButton;
static WNDPROC buttonProc;
static HFONT iconFont;
static wchar_t pendingLog[8192];
static size_t pendingLogLength;

void openPackageDialog(void) {
    wchar_t *path = SDL_calloc(32768, sizeof(*path));
    if (path == NULL) { consoleMessage("Could not allocate file selection."); return; }
    OPENFILENAMEW dialog = {0};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = panel;
    dialog.lpstrFilter = L"MRP applications (*.mrp)\0*.mrp\0\0";
    dialog.lpstrFile = path;
    dialog.nMaxFile = 32768;
    dialog.lpstrInitialDir = L"mythroad";
    dialog.lpstrTitle = L"Open MRP from the mythroad folder";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
    if (GetOpenFileNameW(&dialog)) {
        int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path, -1, NULL, 0, NULL, NULL);
        char *utf8 = bytes > 0 ? SDL_malloc((size_t)bytes) : NULL;
        if (utf8 != NULL && WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path, -1, utf8, bytes, NULL, NULL)) {
            queuePackageOpen(utf8);
        } else {
            consoleMessage("Could not encode selected MRP path.");
        }
        SDL_free(utf8);
    } else if (CommDlgExtendedError() != 0) {
        consoleMessage("Could not open the MRP file dialog.");
    }
    SDL_free(path);
}

static wchar_t *wideText(const char *text, UINT encoding) {
    int count = MultiByteToWideChar(encoding, 0, text, -1, NULL, 0);
    wchar_t *wide = count > 0 ? SDL_malloc((size_t)count * sizeof(*wide)) : NULL;
    if (wide == NULL || !MultiByteToWideChar(encoding, 0, text, -1, wide, count)) {
        SDL_free(wide);
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not convert Console text (Win32 %lu).", GetLastError());
        return NULL;
    }
    return wide;
}

static void writeLog(const wchar_t *text) {
    if (logBox == NULL) return;
    bool visible = IsWindowVisible(logBox);
    if (visible) SendMessageW(logBox, WM_SETREDRAW, FALSE, 0);
    if (GetWindowTextLengthW(logBox) > 60000) {
        SendMessageW(logBox, EM_SETSEL, 0, 30000);
        SendMessageW(logBox, EM_REPLACESEL, FALSE, (LPARAM)L"");
    }
    SendMessageW(logBox, EM_SETSEL, (WPARAM)-1, (LPARAM)-1);
    SendMessageW(logBox, EM_REPLACESEL, FALSE, (LPARAM)text);
    SendMessageW(logBox, EM_SCROLLCARET, 0, 0);
    if (visible) {
        SendMessageW(logBox, WM_SETREDRAW, TRUE, 0);
        RedrawWindow(logBox, NULL, NULL, RDW_INVALIDATE | RDW_ERASE);
    }
}

static void flushLog(void) {
    if (pendingLogLength == 0) return;
    writeLog(pendingLog);
    pendingLogLength = 0;
}

static void appendLog(const wchar_t *text) {
    if (logBox == NULL) return;
    size_t length = wcslen(text);
    if (length > SDL_arraysize(pendingLog) - 3) {
        flushLog();
        writeLog(text);
        writeLog(L"\r\n");
        return;
    }
    if (pendingLogLength + length + 3 > SDL_arraysize(pendingLog)) flushLog();
    memcpy(pendingLog + pendingLogLength, text, length * sizeof(*text));
    pendingLogLength += length;
    pendingLog[pendingLogLength++] = L'\r';
    pendingLog[pendingLogLength++] = L'\n';
    pendingLog[pendingLogLength] = L'\0';
}

void consoleMessage(const char *text) {
    if (panel == NULL) return;
    wchar_t *wide = wideText(text, CP_UTF8);
    if (wide != NULL) { appendLog(wide); SDL_free(wide); flushLog(); }
}

void consoleGuestLog(const char *text) {
    if (panel == NULL) return;
    wchar_t *wide = wideText(text, 936);
    if (wide != NULL) { appendLog(wide); SDL_free(wide); }
}

static void captureError(const char *action) {
    char message[384];
    SDL_snprintf(message, sizeof(message), "%s: %s", action, SDL_GetError());
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", message);
    consoleMessage(message);
    if (SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Capture error", message, guestWindow) < 0)
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not show capture error: %s", SDL_GetError());
}

static void setRecordingButton(bool recording) {
    if (recordButton && !SetWindowTextW(recordButton, recording ? L"Stop Rec" : L"Record"))
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not update recording button (Win32 %lu).",
                     (unsigned long)GetLastError());
}

static void takePhoto(void) {
    char path[VMRP_CAPTURE_PATH_SIZE], message[VMRP_CAPTURE_PATH_SIZE + 32];
    if (!capturePhoto(guestWindow, path, sizeof(path))) {
        captureError("Screenshot failed");
        return;
    }
    SDL_snprintf(message, sizeof(message), "Screenshot saved: %s", path);
    consoleMessage(message);
    SDL_RaiseWindow(guestWindow);
}

static void toggleRecording(void) {
    char path[VMRP_CAPTURE_PATH_SIZE], message[VMRP_CAPTURE_PATH_SIZE + 32];
    bool wasRecording = captureIsRecording();
    bool success = wasRecording ? captureStop(path, sizeof(path)) :
                                  captureStart(guestWindow, path, sizeof(path));
    setRecordingButton(captureIsRecording());
    if (!success) {
        captureError(wasRecording ? "Recording failed" : "Could not start recording");
        return;
    }
    SDL_snprintf(message, sizeof(message), wasRecording ? "Recording saved: %s" :
                                                         "Recording started: %s", path);
    consoleMessage(message);
    SDL_RaiseWindow(guestWindow);
}

// Guest callbacks can emit hundreds of lines; update the native edit control in batches.
void pumpControls(void) {
    flushLog();
    if (!captureUpdate(guestWindow)) {
        setRecordingButton(false);
        captureError("Recording failed");
    }
}

static LRESULT CALLBACK keyButtonProc(HWND button, UINT message, WPARAM wParam, LPARAM lParam) {
    bool keyDown = message == WM_KEYDOWN && (wParam == VK_SPACE || wParam == VK_RETURN) &&
                   !(lParam & (1L << 30));
    bool keyUp = message == WM_KEYUP && (wParam == VK_SPACE || wParam == VK_RETURN);
    if ((message == WM_LBUTTONDOWN || keyDown) && heldButton == NULL && !editWaiting()) {
        heldButton = button;
        queueControl(CONTROL_PRESS, GetWindowLongPtrW(button, GWLP_USERDATA));
    }
    if ((message == WM_LBUTTONUP || keyUp || message == WM_CAPTURECHANGED || message == WM_CANCELMODE) &&
        heldButton == button) {
        heldButton = NULL;
        queueControl(CONTROL_RELEASE, GetWindowLongPtrW(button, GWLP_USERDATA));
        if (message == WM_LBUTTONUP) SDL_RaiseWindow(guestWindow);
    }
    return CallWindowProcW(buttonProc, button, message, wParam, lParam);
}

static void queueText(void) {
    HWND box = IsWindowVisible(passwordBox) ? passwordBox : inputBox;
    int count = GetWindowTextLengthW(box) + 1;
    wchar_t *wide = SDL_malloc((size_t)count * sizeof(*wide));
    if (wide == NULL) { consoleMessage("Could not allocate input text."); return; }
    GetWindowTextW(box, wide, count);
    int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, -1, NULL, 0, NULL, NULL);
    char *utf8 = bytes > 0 ? SDL_malloc((size_t)bytes) : NULL;
    if (utf8 == NULL || !WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, -1, utf8, bytes, NULL, NULL)) {
        SDL_free(utf8);
        consoleMessage("Could not encode input as UTF-8.");
    } else {
        queueControl(CONTROL_SUBMIT, (intptr_t)utf8);
    }
    SDL_free(wide);
}

static void layoutControls(void) {
    if (sendButton == NULL) return;
    RECT client;
    GetClientRect(panel, &client);
    int bottom = client.bottom - 40;
    int inputHeight = client.bottom >= 640 ? 95 : 64;
    int inputTop = bottom - 10 - inputHeight;
    int promptTop = inputTop - 40;
    int clearTop = promptTop - 40;
    MoveWindow(logBox, 10, 317, client.right - 20, clearTop - 325, TRUE);
    MoveWindow(GetDlgItem(panel, CLEAR_ID), 10, clearTop, 80, 28, TRUE);
    MoveWindow(GetDlgItem(panel, COPY_ID), 100, clearTop, 90, 28, TRUE);
    MoveWindow(GetDlgItem(panel, OPEN_ID), 220, clearTop, 150, 28, TRUE);
    MoveWindow(prompt, 10, promptTop, client.right - 20, 36, TRUE);
    MoveWindow(inputBox, 10, inputTop, client.right - 20, inputHeight, TRUE);
    MoveWindow(passwordBox, 10, inputTop, client.right - 20, 28, TRUE);
    MoveWindow(sendButton, 10, bottom, 120, 30, TRUE);
    MoveWindow(cancelButton, 140, bottom, 120, 30, TRUE);
}

static LRESULT CALLBACK panelProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_SIZE) { layoutControls(); return 0; }
    if (message == WM_CLOSE || (message == WM_ACTIVATE && LOWORD(wParam) == WA_INACTIVE)) {
        if (heldButton != NULL) SendMessageW(heldButton, WM_CANCELMODE, 0, 0);
        if (message == WM_CLOSE) { ShowWindow(hwnd, SW_HIDE); return 0; }
    }
    if (message == WM_COMMAND) {
        switch (LOWORD(wParam)) {
            case ROTATE_ID: queueControl(CONTROL_ROTATE, 0); return 0;
            case PHOTO_ID: queueControl(CONTROL_PHOTO, 0); return 0;
            case RECORD_ID: queueControl(CONTROL_RECORD, 0); return 0;
            case OPEN_ID: openPackageDialog(); return 0;
            case SEND_ID: queueText(); return 0;
            case CANCEL_ID: queueControl(CONTROL_CANCEL, 0); return 0;
            case CLEAR_ID:
                pendingLogLength = 0;
                SetWindowTextW(logBox, L"");
                return 0;
            case COPY_ID:
                flushLog();
                SendMessageW(logBox, EM_SETSEL, 0, (LPARAM)-1);
                SendMessageW(logBox, WM_COPY, 0, 0);
                return 0;
        }
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

static HWND widget(const wchar_t *kind, const wchar_t *text, DWORD style,
                   int x, int y, int width, int height, int id) {
    HWND child = CreateWindowExW(0, kind, text, WS_CHILD | WS_VISIBLE | style,
                                 x, y, width, height, panel, (HMENU)(intptr_t)id, GetModuleHandleW(NULL), NULL);
    if (child == NULL) {
        controlFailed = true;
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not create control (Win32 %lu).", GetLastError());
    } else {
        SendMessageW(child, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
    }
    return child;
}

static bool panelPosition(HWND owner, int *left, int *top, int *height) {
    int x, y, width, screenHeight;
    SDL_GetWindowPosition(guestWindow, &x, &y);
    SDL_GetWindowSize(guestWindow, &width, &screenHeight);
    MONITORINFO monitor = {.cbSize = sizeof(monitor)};
    if (!GetMonitorInfoW(MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST), &monitor)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not locate controls monitor (Win32 %lu).", GetLastError());
        return false;
    }
    RECT work = monitor.rcWork;
    *height = SDL_min(740, work.bottom - work.top);
    *left = x + width + 20;
    if (*left + 416 > work.right) *left = x - 436;
    *left = SDL_max(work.left, SDL_min(*left, work.right - 416));
    *top = SDL_max(work.top, SDL_min(y, work.bottom - *height));
    return true;
}

static void repositionControls(void) {
    int left, top, height;
    if (panelPosition(GetWindow(panel, GW_OWNER), &left, &top, &height) &&
        !SetWindowPos(panel, NULL, left, top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not reposition controls (Win32 %lu).", GetLastError());
    }
}

bool initControls(SDL_Window *screen) {
    guestWindow = screen;
    closing = false;
    controlFailed = false;
    phoneFailed = false;
    phoneType = SDL_RegisterEvents(1);
    controlType = SDL_RegisterEvents(1);
    if (controlType == (Uint32)-1 || phoneType == (Uint32)-1) return false;
    HINSTANCE instance = GetModuleHandleW(NULL);
    WNDCLASSW cls = {0};
    cls.lpfnWndProc = panelProc;
    cls.hInstance = instance;
    cls.lpszClassName = L"VMRPControls";
    cls.hCursor = LoadCursor(NULL, IDC_ARROW);
    cls.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    if (!RegisterClassW(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not register controls (Win32 %lu).", GetLastError());
        return false;
    }
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(screen, &info)) return false;
    int left, y, height;
    if (!panelPosition(info.info.win.window, &left, &y, &height)) return false;
    panel = CreateWindowExW(WS_EX_TOOLWINDOW, cls.lpszClassName, L"mrpx - Keypad & Console",
                            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, left, y,
                            416, height, info.info.win.window, NULL, instance, NULL);
    if (panel == NULL) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not create controls (Win32 %lu).", GetLastError());
        return false;
    }
    LOGFONTW font;
    if (GetObjectW(GetStockObject(DEFAULT_GUI_FONT), sizeof(font), &font) == 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not read the keypad font.");
        destroyControls();
        return false;
    }
    font.lfHeight = MulDiv(font.lfHeight, 5, 3);
    font.lfCharSet = DEFAULT_CHARSET;
    lstrcpynW(font.lfFaceName, L"Segoe UI Symbol", LF_FACESIZE);
    iconFont = CreateFontIndirectW(&font);
    if (iconFont == NULL) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not create the keypad icon font.");
        destroyControls();
        return false;
    }
    static const struct { const wchar_t *label; int key; } keys[] = {
        {L"\x25e7", MR_KEY_SOFTLEFT}, {L"\x2191", MR_KEY_UP}, {L"\x25e8", MR_KEY_SOFTRIGHT},
        {L"\x2190", MR_KEY_LEFT}, {L"OK", MR_KEY_SELECT}, {L"\x2192", MR_KEY_RIGHT},
        {L"\x260e", MR_KEY_SEND}, {L"\x2193", MR_KEY_DOWN}, {L"\x260e\x00d7", MR_KEY_POWER},
        {L"1", MR_KEY_1}, {L"2", MR_KEY_2}, {L"3", MR_KEY_3},
        {L"4", MR_KEY_4}, {L"5", MR_KEY_5}, {L"6", MR_KEY_6},
        {L"7", MR_KEY_7}, {L"8", MR_KEY_8}, {L"9", MR_KEY_9},
        {L"*", MR_KEY_STAR}, {L"0", MR_KEY_0}, {L"#", MR_KEY_POUND},
    };
    for (size_t i = 0; i < SDL_arraysize(keys); i++) {
        HWND button = widget(L"BUTTON", keys[i].label, BS_PUSHBUTTON,
                             10 + (i % 3) * 130, 10 + (i / 3) * 35, 120, 30, 200 + (int)i);
        if (button == NULL) continue;
        if (i < 9 && keys[i].key != MR_KEY_SELECT)
            SendMessageW(button, WM_SETFONT, (WPARAM)iconFont, TRUE);
        SetWindowLongPtrW(button, GWLP_USERDATA, keys[i].key);
        WNDPROC previous = (WNDPROC)SetWindowLongPtrW(button, GWLP_WNDPROC, (LONG_PTR)keyButtonProc);
        if (previous == NULL) {
            controlFailed = true;
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not subclass phone key (Win32 %lu).", GetLastError());
        }
        else buttonProc = previous;
    }
    widget(L"BUTTON", L"Photo", BS_PUSHBUTTON, 10, 255, 120, 26, PHOTO_ID);
    recordButton = widget(L"BUTTON", L"Record", BS_PUSHBUTTON, 140, 255, 120, 26, RECORD_ID);
    widget(L"BUTTON", L"Rotate 90\x00b0", BS_PUSHBUTTON, 270, 255, 120, 26, ROTATE_ID);
    widget(L"STATIC", L"Console (F1 hides/shows this panel)", 0, 10, 292, 250, 20, 0);
    logBox = widget(L"EDIT", L"", WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
                    10, 317, 380, 150, 0);
    SendMessageW(logBox, EM_SETLIMITTEXT, 131072, 0);
    widget(L"BUTTON", L"Clear", BS_PUSHBUTTON, 10, 475, 80, 28, CLEAR_ID);
    widget(L"BUTTON", L"Copy log", BS_PUSHBUTTON, 100, 475, 90, 28, COPY_ID);
    widget(L"BUTTON", L"Open MRP (Ctrl+O)", BS_PUSHBUTTON, 220, 475, 150, 28, OPEN_ID);
    prompt = widget(L"STATIC", L"Application is not waiting for input.", 0, 10, 515, 380, 36, 0);
    inputBox = widget(L"EDIT", L"", WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
                      10, 555, 380, 95, INPUT_ID);
    passwordBox = widget(L"EDIT", L"", WS_BORDER | ES_PASSWORD | ES_AUTOHSCROLL,
                         10, 555, 380, 28, INPUT_ID);
    SendMessageW(inputBox, EM_SETLIMITTEXT, 0, 0);
    SendMessageW(passwordBox, EM_SETLIMITTEXT, 0, 0);
    sendButton = widget(L"BUTTON", L"Submit input", BS_PUSHBUTTON, 10, 660, 120, 30, SEND_ID);
    cancelButton = widget(L"BUTTON", L"Cancel input", BS_PUSHBUTTON, 140, 660, 120, 30, CANCEL_ID);
    layoutControls();
    controlsEditChanged(NULL, NULL, 0, 0);
    if (controlFailed) { destroyControls(); return false; }
    ShowWindow(panel, SW_SHOWNOACTIVATE);
    return true;
}

void controlsEditChanged(const char *title, const char *text, int type, int maxSize) {
    if (panel == NULL) return;
    bool active = title != NULL;
    ShowWindow(passwordBox, active && type == MR_EDIT_PASSWORD ? SW_SHOW : SW_HIDE);
    ShowWindow(inputBox, !active || type != MR_EDIT_PASSWORD ? SW_SHOW : SW_HIDE);
    EnableWindow(inputBox, active);
    EnableWindow(passwordBox, active);
    EnableWindow(sendButton, active);
    EnableWindow(cancelButton, active);
    for (int i = 0; i < 21; i++) EnableWindow(GetDlgItem(panel, 200 + i), !active);
    SetWindowTextW(inputBox, L"");
    SetWindowTextW(passwordBox, L"");
    if (!active) {
        SetWindowTextW(prompt, L"Application is not waiting for input.");
        return;
    }
    wchar_t *wide = wideText(title, CP_UTF8);
    if (wide != NULL) { SetWindowTextW(prompt, wide); SDL_free(wide); }
    char message[100];
    SDL_snprintf(message, sizeof(message), "Text input requested (limit: %d UCS-2 characters).", maxSize);
    consoleMessage(message);
    wide = wideText(text, CP_UTF8);
    HWND target = type == MR_EDIT_PASSWORD ? passwordBox : inputBox;
    if (wide != NULL) { SetWindowTextW(target, wide); SDL_free(wide); }
    ShowWindow(panel, SW_SHOW);
    SetFocus(target);
}

void toggleControls(void) {
    if (panel == NULL) return;
    if (IsWindowVisible(panel)) SendMessageW(panel, WM_CLOSE, 0, 0);
    else ShowWindow(panel, SW_SHOW);
}

void destroyControls(void) {
    closing = true;
    if (captureIsRecording()) {
        char path[VMRP_CAPTURE_PATH_SIZE], message[VMRP_CAPTURE_PATH_SIZE + 32];
        if (captureStop(path, sizeof(path))) {
            SDL_snprintf(message, sizeof(message), "Recording saved: %s", path);
            consoleMessage(message);
            SDL_Log("%s", message);
        } else {
            captureError("Could not save recording");
        }
    }
    if (panel != NULL) DestroyWindow(panel);
    if (iconFont != NULL) DeleteObject(iconFont);
    iconFont = NULL;
    pendingLogLength = 0;
    panel = logBox = inputBox = passwordBox = prompt = sendButton = cancelButton = NULL;
    recordButton = NULL;
    heldButton = NULL;
    drainControlEvents();
}

#elif defined(VMRP_GTK)
#include <gtk/gtk.h>

static GtkWidget *panel, *logBox, *inputBox, *passwordBox, *prompt, *sendButton, *cancelButton;
static GtkWidget *phoneButtons[21], *heldButton;
static GtkFileDialog *packageChooser;
static GCancellable *packageChoice;

static void packageChosen(GObject *dialog, GAsyncResult *result, gpointer data) {
    GCancellable *choice = data;
    GError *error = NULL;
    GFile *file = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(dialog), result, &error);
    if (file != NULL && !g_cancellable_is_cancelled(choice)) {
        char *path = g_file_get_path(file);
        if (path != NULL) queuePackageOpen(path);
        else consoleMessage("Could not resolve selected MRP path.");
        g_free(path);
    } else if (error != NULL && !g_error_matches(error, GTK_DIALOG_ERROR, GTK_DIALOG_ERROR_DISMISSED) &&
               !g_error_matches(error, GTK_DIALOG_ERROR, GTK_DIALOG_ERROR_CANCELLED) &&
               !g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
        consoleMessage(error->message);
    }
    g_clear_error(&error);
    g_clear_object(&file);
    if (dialog == G_OBJECT(packageChooser)) {
        g_clear_object(&packageChooser);
        g_clear_object(&packageChoice);
    }
    g_object_unref(choice);
}

void openPackageDialog(void) {
    if (packageChooser != NULL) return;
    packageChooser = gtk_file_dialog_new();
    gtk_file_dialog_set_title(packageChooser, "Open MRP from the mythroad folder");
    gtk_file_dialog_set_modal(packageChooser, TRUE);
    GtkFileFilter *filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "MRP applications");
    gtk_file_filter_add_pattern(filter, "*.mrp");
    GListStore *filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
    g_list_store_append(filters, filter);
    gtk_file_dialog_set_filters(packageChooser, G_LIST_MODEL(filters));
    g_object_unref(filters);
    g_object_unref(filter);
    GFile *folder = g_file_new_for_path("mythroad");
    gtk_file_dialog_set_initial_folder(packageChooser, folder);
    g_object_unref(folder);
    packageChoice = g_cancellable_new();
    gtk_file_dialog_open(packageChooser, GTK_WINDOW(panel), packageChoice, packageChosen,
                         g_object_ref(packageChoice));
}

static void gtkOpenPackage(GtkButton *button, gpointer unused) { openPackageDialog(); }

void pumpControls(void) {
    if (panel == NULL) return;
    while (g_main_context_pending(NULL)) g_main_context_iteration(NULL, FALSE);
}

void consoleMessage(const char *text) {
    if (panel == NULL) return;
    if (!g_utf8_validate(text, -1, NULL)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Console message is not valid UTF-8.");
        return;
    }
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(logBox));
    GtkTextIter start, end;
    if (gtk_text_buffer_get_char_count(buffer) > 60000) {
        gtk_text_buffer_get_start_iter(buffer, &start);
        gtk_text_buffer_get_iter_at_offset(buffer, &end, 30000);
        gtk_text_buffer_delete(buffer, &start, &end);
    }
    gtk_text_buffer_get_end_iter(buffer, &end);
    gtk_text_buffer_insert(buffer, &end, text, -1);
    gtk_text_buffer_insert(buffer, &end, "\n", 1);
    gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(logBox), &end, 0, FALSE, 0, 1);
}

void consoleGuestLog(const char *text) {
    if (panel == NULL) return;
    GError *error = NULL;
    char *utf8 = g_convert(text, -1, "UTF-8", "GBK", NULL, NULL, &error);
    if (utf8 == NULL) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not decode Guest Console text: %s", error->message);
        g_error_free(error);
        return;
    }
    consoleMessage(utf8);
    g_free(utf8);
}

static void releaseHeldButton(void) {
    if (heldButton == NULL) return;
    int key = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(heldButton), "vmrp-key"));
    gtk_widget_unset_state_flags(heldButton, GTK_STATE_FLAG_ACTIVE);
    heldButton = NULL;
    queueControl(CONTROL_RELEASE, key);
}

static void gtkPhonePressed(GtkGestureClick *gesture, int count, double x, double y, gpointer data) {
    if (heldButton != NULL || editWaiting()) return;
    if (gesture != NULL) gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
    heldButton = GTK_WIDGET(data);
    gtk_widget_set_state_flags(heldButton, GTK_STATE_FLAG_ACTIVE, FALSE);
    queueControl(CONTROL_PRESS, GPOINTER_TO_INT(g_object_get_data(G_OBJECT(heldButton), "vmrp-key")));
}

static void gtkPhoneReleased(GtkGestureClick *gesture, int count, double x, double y, gpointer data) {
    releaseHeldButton();
}

static void gtkPhoneCancelled(GtkGesture *gesture, GdkEventSequence *sequence, gpointer data) {
    releaseHeldButton();
}

static void gtkPhoneStopped(GtkGestureClick *gesture, gpointer data) { releaseHeldButton(); }
static void gtkFocusLeft(GtkEventControllerFocus *controller, gpointer data) { releaseHeldButton(); }

static gboolean gtkPhoneKeyPressed(GtkEventControllerKey *controller, guint keyval, guint keycode,
                                   GdkModifierType state, gpointer data) {
    if (keyval != GDK_KEY_space && keyval != GDK_KEY_Return && keyval != GDK_KEY_KP_Enter) return FALSE;
    gtkPhonePressed(NULL, 1, 0, 0, data);
    return TRUE;
}

static void gtkPhoneKeyReleased(GtkEventControllerKey *controller, guint keyval, guint keycode,
                               GdkModifierType state, gpointer data) {
    if (keyval == GDK_KEY_space || keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter) releaseHeldButton();
}

static gboolean gtkPanelClosed(GtkWindow *window, gpointer data) {
    releaseHeldButton();
    gtk_widget_set_visible(GTK_WIDGET(window), FALSE);
    return TRUE;
}

static void gtkSubmit(GtkButton *button, gpointer data) {
    char *text;
    if (gtk_widget_get_visible(passwordBox)) {
        text = g_strdup(gtk_editable_get_text(GTK_EDITABLE(passwordBox)));
    } else {
        GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(inputBox));
        GtkTextIter start, end;
        gtk_text_buffer_get_bounds(buffer, &start, &end);
        text = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
    }
    char *queued = SDL_strdup(text);
    g_free(text);
    if (queued == NULL) {
        consoleMessage("Could not allocate input text.");
        return;
    }
    queueControl(CONTROL_SUBMIT, (intptr_t)queued);
}

static void gtkCancel(GtkButton *button, gpointer data) { queueControl(CONTROL_CANCEL, 0); }
static void gtkRotate(GtkButton *button, gpointer data) { queueControl(CONTROL_ROTATE, 0); }
static void repositionControls(void) {}

static void gtkClearLog(GtkButton *button, gpointer data) {
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(logBox)), "", 0);
}

static void gtkCopyLog(GtkButton *button, gpointer data) {
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(logBox));
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(buffer, &start, &end);
    char *text = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
    gdk_clipboard_set_text(gtk_widget_get_clipboard(logBox), text);
    g_free(text);
}

static gboolean gtkInputKeyPressed(GtkEventControllerKey *controller, guint keyval, guint keycode,
                                   GdkModifierType state, gpointer data) {
    if ((state & GDK_CONTROL_MASK) && (keyval == GDK_KEY_o || keyval == GDK_KEY_O)) {
        openPackageDialog();
        return TRUE;
    }
    if (editWaiting() && (state & GDK_CONTROL_MASK) &&
        (keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter)) {
        gtkSubmit(NULL, NULL);
        return TRUE;
    }
    return FALSE;
}

static GtkWidget *gtkTextArea(bool editable, int height) {
    GtkWidget *view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(view), editable);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(view), editable);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view), GTK_WRAP_WORD_CHAR);
    gtk_widget_set_size_request(view, -1, height);
    return view;
}

static GtkWidget *gtkScrolled(GtkWidget *child) {
    GtkWidget *scrolled = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), child);
    gtk_widget_set_vexpand(scrolled, TRUE);
    return scrolled;
}

bool initControls(SDL_Window *screen) {
    guestWindow = screen;
    if (!gtk_init_check()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "GTK4 controls require a graphical display.");
        return false;
    }
    closing = controlFailed = phoneFailed = false;
    phoneType = SDL_RegisterEvents(1);
    controlType = SDL_RegisterEvents(1);
    if (phoneType == (Uint32)-1 || controlType == (Uint32)-1) return false;
    panel = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(panel), "mrpx - Keypad & Console");
    gtk_window_set_default_size(GTK_WINDOW(panel), 420, 740);
    g_signal_connect(panel, "close-request", G_CALLBACK(gtkPanelClosed), NULL);
    GtkWidget *body = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(body, 12);
    gtk_widget_set_margin_bottom(body, 12);
    gtk_widget_set_margin_start(body, 12);
    gtk_widget_set_margin_end(body, 12);
    gtk_window_set_child(GTK_WINDOW(panel), gtkScrolled(body));
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_column_homogeneous(GTK_GRID(grid), TRUE);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 6);
    gtk_box_append(GTK_BOX(body), grid);
    static const struct { const char *label; int key; } keys[] = {
        {"\xe2\x97\xa7", MR_KEY_SOFTLEFT}, {"\xe2\x86\x91", MR_KEY_UP}, {"\xe2\x97\xa8", MR_KEY_SOFTRIGHT},
        {"\xe2\x86\x90", MR_KEY_LEFT}, {"OK", MR_KEY_SELECT}, {"\xe2\x86\x92", MR_KEY_RIGHT},
        {"\xe2\x98\x8e", MR_KEY_SEND}, {"\xe2\x86\x93", MR_KEY_DOWN}, {"\xe2\x98\x8e\xc3\x97", MR_KEY_POWER},
        {"1", MR_KEY_1}, {"2", MR_KEY_2}, {"3", MR_KEY_3},
        {"4", MR_KEY_4}, {"5", MR_KEY_5}, {"6", MR_KEY_6},
        {"7", MR_KEY_7}, {"8", MR_KEY_8}, {"9", MR_KEY_9},
        {"*", MR_KEY_STAR}, {"0", MR_KEY_0}, {"#", MR_KEY_POUND},
    };
    for (size_t i = 0; i < SDL_arraysize(keys); i++) {
        GtkWidget *button = phoneButtons[i] = gtk_button_new_with_label(keys[i].label);
        gtk_widget_set_size_request(button, -1, 44);
        g_object_set_data(G_OBJECT(button), "vmrp-key", GINT_TO_POINTER(keys[i].key));
        if (i < 9 && keys[i].key != MR_KEY_SELECT) {
            PangoAttrList *attributes = pango_attr_list_new();
            pango_attr_list_insert(attributes, pango_attr_scale_new(1.5));
            gtk_label_set_attributes(GTK_LABEL(gtk_button_get_child(GTK_BUTTON(button))), attributes);
            pango_attr_list_unref(attributes);
        }
        if (keys[i].key == MR_KEY_SELECT) gtk_widget_add_css_class(button, "suggested-action");
        if (keys[i].key == MR_KEY_POWER) gtk_widget_add_css_class(button, "destructive-action");
        gtk_grid_attach(GTK_GRID(grid), button, (int)(i % 3), (int)(i / 3), 1, 1);
        GtkGesture *click = gtk_gesture_click_new();
        gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), 1);
        gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(click), GTK_PHASE_CAPTURE);
        g_signal_connect(click, "pressed", G_CALLBACK(gtkPhonePressed), button);
        g_signal_connect(click, "released", G_CALLBACK(gtkPhoneReleased), button);
        g_signal_connect(click, "cancel", G_CALLBACK(gtkPhoneCancelled), button);
        g_signal_connect(click, "stopped", G_CALLBACK(gtkPhoneStopped), button);
        gtk_widget_add_controller(button, GTK_EVENT_CONTROLLER(click));
        GtkEventController *keyboard = gtk_event_controller_key_new();
        gtk_event_controller_set_propagation_phase(keyboard, GTK_PHASE_CAPTURE);
        g_signal_connect(keyboard, "key-pressed", G_CALLBACK(gtkPhoneKeyPressed), button);
        g_signal_connect(keyboard, "key-released", G_CALLBACK(gtkPhoneKeyReleased), button);
        gtk_widget_add_controller(button, keyboard);
        GtkEventController *focus = gtk_event_controller_focus_new();
        g_signal_connect(focus, "leave", G_CALLBACK(gtkFocusLeft), NULL);
        gtk_widget_add_controller(button, focus);
    }
    GtkWidget *rotate = gtk_button_new_with_label("Rotate 90\xc2\xb0");
    gtk_widget_set_size_request(rotate, -1, 36);
    g_signal_connect(rotate, "clicked", G_CALLBACK(gtkRotate), NULL);
    gtk_grid_attach(GTK_GRID(grid), rotate, 0, 7, 3, 1);
    GtkEventController *focus = gtk_event_controller_focus_new();
    g_signal_connect(focus, "leave", G_CALLBACK(gtkFocusLeft), NULL);
    gtk_widget_add_controller(panel, focus);
    GtkEventController *keyboard = gtk_event_controller_key_new();
    g_signal_connect(keyboard, "key-pressed", G_CALLBACK(gtkInputKeyPressed), NULL);
    gtk_widget_add_controller(panel, keyboard);
    gtk_box_append(GTK_BOX(body), gtk_label_new("Console (F1 hides/shows this panel)"));
    logBox = gtkTextArea(false, 180);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(logBox), TRUE);
    gtk_box_append(GTK_BOX(body), gtkScrolled(logBox));
    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *clear = gtk_button_new_with_label("Clear");
    GtkWidget *copy = gtk_button_new_with_label("Copy log");
    GtkWidget *open = gtk_button_new_with_label("Open MRP (Ctrl+O)");
    g_signal_connect(clear, "clicked", G_CALLBACK(gtkClearLog), NULL);
    g_signal_connect(copy, "clicked", G_CALLBACK(gtkCopyLog), NULL);
    g_signal_connect(open, "clicked", G_CALLBACK(gtkOpenPackage), NULL);
    gtk_box_append(GTK_BOX(actions), clear);
    gtk_box_append(GTK_BOX(actions), copy);
    gtk_box_append(GTK_BOX(actions), open);
    gtk_box_append(GTK_BOX(body), actions);
    prompt = gtk_label_new(NULL);
    gtk_label_set_wrap(GTK_LABEL(prompt), TRUE);
    gtk_box_append(GTK_BOX(body), prompt);
    inputBox = gtkTextArea(true, 90);
    gtk_box_append(GTK_BOX(body), inputBox);
    passwordBox = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(passwordBox), FALSE);
    gtk_box_append(GTK_BOX(body), passwordBox);
    actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    sendButton = gtk_button_new_with_label("Submit input");
    cancelButton = gtk_button_new_with_label("Cancel input");
    g_signal_connect(sendButton, "clicked", G_CALLBACK(gtkSubmit), NULL);
    g_signal_connect(cancelButton, "clicked", G_CALLBACK(gtkCancel), NULL);
    gtk_box_append(GTK_BOX(actions), sendButton);
    gtk_box_append(GTK_BOX(actions), cancelButton);
    gtk_box_append(GTK_BOX(body), actions);
    controlsEditChanged(NULL, NULL, 0, 0);
    gtk_window_present(GTK_WINDOW(panel));
    return true;
}

void controlsEditChanged(const char *title, const char *text, int type, int maxSize) {
    if (panel == NULL) return;
    releaseHeldButton();
    bool active = title != NULL;
    bool password = active && type == MR_EDIT_PASSWORD;
    gtk_widget_set_visible(passwordBox, password);
    gtk_widget_set_visible(inputBox, !password);
    gtk_widget_set_sensitive(inputBox, active);
    gtk_widget_set_sensitive(passwordBox, active);
    gtk_widget_set_sensitive(sendButton, active);
    gtk_widget_set_sensitive(cancelButton, active);
    for (size_t i = 0; i < SDL_arraysize(phoneButtons); i++) gtk_widget_set_sensitive(phoneButtons[i], !active);
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(inputBox)), "", 0);
    gtk_editable_set_text(GTK_EDITABLE(passwordBox), "");
    gtk_label_set_text(GTK_LABEL(prompt), active ? title : "Application is not waiting for input.");
    if (!active) return;
    if (password) gtk_editable_set_text(GTK_EDITABLE(passwordBox), text);
    else gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(inputBox)), text, -1);
    char message[100];
    SDL_snprintf(message, sizeof(message), "Text input requested (limit: %d UCS-2 characters).", maxSize);
    consoleMessage(message);
    gtk_window_present(GTK_WINDOW(panel));
    gtk_widget_grab_focus(password ? passwordBox : inputBox);
}

void toggleControls(void) {
    if (panel == NULL) return;
    if (gtk_widget_get_visible(panel)) gtkPanelClosed(GTK_WINDOW(panel), NULL);
    else gtk_window_present(GTK_WINDOW(panel));
}

void destroyControls(void) {
    closing = true;
    heldButton = NULL;
    if (packageChooser != NULL) {
        g_cancellable_cancel(packageChoice);
        g_clear_object(&packageChoice);
        g_clear_object(&packageChooser);
    }
    if (panel != NULL) gtk_window_destroy(GTK_WINDOW(panel));
    panel = logBox = inputBox = passwordBox = prompt = sendButton = cancelButton = NULL;
    drainControlEvents();
}

#else
bool initControls(SDL_Window *screen) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Interactive controls require Windows or WebAssembly.");
    return false;
}
void destroyControls(void) {}
void toggleControls(void) {}
void openPackageDialog(void) { consoleMessage("File selection requires Windows or GTK4."); }
bool handleControlEvent(const SDL_Event *ev) { return false; }
bool controlsHaveFailed(void) { return phoneFailed; }
void consoleGuestLog(const char *text) {}
void consoleMessage(const char *text) { SDL_Log("%s", text); }
void controlsEditChanged(const char *title, const char *text, int type, int maxSize) {}
#endif
