#include <assert.h>
#include "vmrp/bridge.h"
#include "vmrp/memory.h"
#include "../src/controls.c"

static int eventCount;
static int codes[20], parameters[20];
void *my_mallocExt(uint32 length) { return malloc(length); }
void my_freeExt(void *memory) { free(memory); }
int wstrlen(char *text) {
    int bytes = 0;
    while (text[bytes] || text[bytes + 1]) bytes += 2;
    return bytes;
}
int32_t event(int32_t code, int32_t p0, int32_t p1) {
    assert(eventCount < 20 && p1 == 0);
    codes[eventCount] = code;
    parameters[eventCount++] = p0;
    return MR_SUCCESS;
}
static void pump(void) {
    pumpControls();
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) handleControlEvent(&ev);
}
static int rejectEvent(void *unused, SDL_Event *ev) {
    return ev->type != phoneType;
}

static void clickRotate(void) {
#ifdef VMRP_GTK
    gtkRotate(NULL, NULL);
#else
    SendMessageW(panel, WM_COMMAND, ROTATE_ID, 0);
#endif
    pump();
}

#ifdef _WIN32
static void removeTestCaptures(const wchar_t *pattern, int expected) {
    WIN32_FIND_DATAW entry;
    HANDLE search = FindFirstFileW(pattern, &entry);
    assert(search != INVALID_HANDLE_VALUE);
    wchar_t paths[3][MAX_PATH];
    int count = 0;
    do {
        assert(!(entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY));
        assert(entry.nFileSizeHigh || entry.nFileSizeLow);
        assert(count < (int)SDL_arraysize(paths));
        assert(swprintf(paths[count++], MAX_PATH, L"captures\\%ls", entry.cFileName) > 0);
    } while (FindNextFileW(search, &entry));
    assert(GetLastError() == ERROR_NO_MORE_FILES);
    assert(FindClose(search));
    assert(count == expected);
    for (int i = 0; i < count; i++) assert(DeleteFileW(paths[i]));
}
#endif

int main(int argc, char **argv) {
    assert(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) == 0);
    Uint32 flags = SDL_WINDOW_HIDDEN;
#ifdef _WIN32
    flags |= SDL_WINDOW_OPENGL;
#endif
    SDL_Window *screen = SDL_CreateWindow("controls test", 0, 0, 240, 320, flags);
    assert(screen != NULL && initDisplay(screen) && initControls(screen));
    assert(SDL_GetWindowSurface(screen) != NULL);
    const int iconIndexes[] = {0, 1, 2, 3, 5, 6, 7, 8};
    const int iconKeys[] = {MR_KEY_SOFTLEFT, MR_KEY_UP, MR_KEY_SOFTRIGHT, MR_KEY_LEFT,
                            MR_KEY_RIGHT, MR_KEY_SEND, MR_KEY_DOWN, MR_KEY_POWER};
#ifdef VMRP_GTK
    const char *icons[] = {"\xe2\x97\xa7", "\xe2\x86\x91", "\xe2\x97\xa8", "\xe2\x86\x90",
                           "\xe2\x86\x92", "\xe2\x98\x8e", "\xe2\x86\x93", "\xe2\x98\x8e\xc3\x97"};
#else
    const wchar_t *icons[] = {L"\x25e7", L"\x2191", L"\x25e8", L"\x2190",
                              L"\x2192", L"\x260e", L"\x2193", L"\x260e\x00d7"};
#endif
    for (size_t i = 0; i < SDL_arraysize(iconIndexes); i++) {
#ifdef VMRP_GTK
        GtkWidget *button = phoneButtons[iconIndexes[i]];
        assert(strcmp(gtk_button_get_label(GTK_BUTTON(button)), icons[i]) == 0);
        gtkPhonePressed(NULL, 1, 5, 5, button);
        gtkPhoneReleased(NULL, 1, 5, 5, button);
#else
        HWND button = GetDlgItem(panel, 200 + iconIndexes[i]);
        wchar_t label[4];
        assert(GetWindowTextW(button, label, SDL_arraysize(label)) == (int)wcslen(icons[i]));
        assert(wcscmp(label, icons[i]) == 0);
        SendMessageW(button, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(5, 5));
        SendMessageW(button, WM_LBUTTONUP, 0, MAKELPARAM(5, 5));
#endif
        pump();
        assert(eventCount == 2 && codes[0] == MR_KEY_PRESS && codes[1] == MR_KEY_RELEASE);
        assert(parameters[0] == iconKeys[i] && parameters[1] == iconKeys[i]);
        eventCount = 0;
    }
#ifdef VMRP_GTK
    Uint64 until = SDL_GetTicks64() + 5000;
    while (gtk_widget_get_height(sendButton) == 0 && SDL_GetTicks64() < until) {
        pump();
        SDL_Delay(10);
    }
    graphene_rect_t submitBounds;
    assert(gtk_widget_compute_bounds(sendButton, panel, &submitBounds));
    assert(submitBounds.size.height >= 30);
    GtkWidget *key = phoneButtons[3];
    gtkPhonePressed(NULL, 1, 5, 5, key);
    gtkFocusLeft(NULL, NULL);
    gtkPhoneCancelled(NULL, NULL, key);
#else
    RECT client, submit;
    GetClientRect(panel, &client);
    GetWindowRect(sendButton, &submit);
    MapWindowPoints(NULL, panel, (POINT *)&submit, 2);
    assert(submit.bottom <= client.bottom);
    RECT photoBounds, recordBounds, rotateBounds, logBounds;
    GetWindowRect(GetDlgItem(panel, PHOTO_ID), &photoBounds);
    GetWindowRect(recordButton, &recordBounds);
    GetWindowRect(GetDlgItem(panel, ROTATE_ID), &rotateBounds);
    GetWindowRect(logBox, &logBounds);
    assert(photoBounds.right <= recordBounds.left && recordBounds.right <= rotateBounds.left);
    assert(photoBounds.bottom < logBounds.top);
    HWND key = GetDlgItem(panel, 203);
    SendMessageW(key, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(5, 5));
    SendMessageW(key, WM_CANCELMODE, 0, 0);
    SendMessageW(key, WM_CAPTURECHANGED, 0, 0);
#endif
    pump();
    assert(eventCount == 2);
    assert(codes[0] == MR_KEY_PRESS && codes[1] == MR_KEY_RELEASE);
    assert(parameters[0] == MR_KEY_LEFT && parameters[1] == MR_KEY_LEFT);
    assert(queuePhoneKey(1, MR_KEY_SEND) == MR_SUCCESS);
    assert(queuePhoneKey(0, MR_KEY_SEND) == MR_SUCCESS);
    pump();
    assert(eventCount == 4 && parameters[2] == MR_KEY_SEND && parameters[3] == MR_KEY_SEND);

    consoleGuestLog("Guest: \xc4\xe3\xba\xc3");
    pump();
#ifdef VMRP_GTK
    GtkTextIter start, end;
    GtkTextBuffer *logBuffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(logBox));
    gtk_text_buffer_get_bounds(logBuffer, &start, &end);
    char *log = gtk_text_buffer_get_text(logBuffer, &start, &end, FALSE);
    assert(strstr(log, "Guest: \xe4\xbd\xa0\xe5\xa5\xbd") != NULL);
    g_free(log);
#else
    wchar_t log[1000];
    GetWindowTextW(logBox, log, 1000);
    assert(wcsstr(log, L"Guest: \x4f60\x597d") != NULL);

    SendMessageW(panel, WM_COMMAND, CLEAR_ID, 0);
    wchar_t expected[12000], actual[12000];
    size_t length = 0;
    for (int i = 0; i < 1000; i++) {
        char line[32];
        SDL_snprintf(line, sizeof(line), "burst-%03d", i);
        consoleGuestLog(line);
        length += swprintf(expected + length, SDL_arraysize(expected) - length,
                           L"burst-%03d\r\n", i);
        if (i == 99) assert(GetWindowTextLengthW(logBox) == 0);
    }
    pump();
    assert(GetWindowTextW(logBox, actual, SDL_arraysize(actual)) == (int)length);
    assert(wcscmp(actual, expected) == 0);
    consoleGuestLog("pending before Clear");
    SendMessageW(panel, WM_COMMAND, CLEAR_ID, 0);
    pump();
    assert(GetWindowTextLengthW(logBox) == 0);

    char oversized[8200];
    memset(oversized, 'x', sizeof(oversized) - 1);
    oversized[sizeof(oversized) - 1] = '\0';
    consoleGuestLog(oversized);
    consoleGuestLog("tail");
    pump();
    assert(GetWindowTextW(logBox, actual, SDL_arraysize(actual)) == sizeof(oversized) - 1 + 8);
    for (size_t i = 0; i < sizeof(oversized) - 1; i++) assert(actual[i] == L'x');
    assert(wcscmp(actual + sizeof(oversized) - 1, L"\r\ntail\r\n") == 0);
    SendMessageW(panel, WM_COMMAND, CLEAR_ID, 0);
#endif
    const char title[] = {0, 'T', 0, 0};
    const char initial[] = {0, 0};
    assert(editCreate(title, initial, MR_EDIT_ANY, 2) == 1234);
#ifdef VMRP_GTK
    assert(!gtk_widget_get_sensitive(key) && gtk_widget_get_sensitive(sendButton));
#else
    assert(!IsWindowEnabled(key) && IsWindowEnabled(sendButton));
#endif
    assert(queuePhoneKey(1, MR_KEY_1) == MR_SUCCESS);
    assert(queuePhoneKey(0, MR_KEY_1) == MR_SUCCESS);
    pump();
    assert(eventCount == 4);
#ifdef VMRP_GTK
    GtkTextBuffer *inputBuffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(inputBox));
    gtk_text_buffer_set_text(inputBuffer, "\xe4\xb8\xad" "A", -1);
    g_signal_emit_by_name(sendButton, "clicked");
#else
    SetWindowTextW(inputBox, L"\x4e2d" L"A");
    SendMessageW(panel, WM_COMMAND, SEND_ID, 0);
#endif
    assert(editWaiting());
    pump();
    assert(eventCount == 5 && codes[4] == MR_DIALOG_EVENT && parameters[4] == MR_DIALOG_KEY_OK);
    assert(memcmp(editGetText(1234), "\x4e\x2d\0A\0", 6) == 0);
    assert(!editWaiting());
#ifdef VMRP_GTK
    assert(gtk_widget_get_sensitive(key));
#else
    assert(IsWindowEnabled(key));
#endif

    assert(editCreate(title, initial, MR_EDIT_ANY, 2) == 1234);
#ifdef VMRP_GTK
    gtk_text_buffer_set_text(inputBuffer, "abc", -1);
    g_signal_emit_by_name(sendButton, "clicked");
#else
    SetWindowTextW(inputBox, L"abc");
    SendMessageW(panel, WM_COMMAND, SEND_ID, 0);
#endif
    pump();
    assert(editWaiting() && eventCount == 5);
#ifdef VMRP_GTK
    gtk_text_buffer_set_text(inputBuffer, "ab", -1);
    g_signal_emit_by_name(sendButton, "clicked");
#else
    SetWindowTextW(inputBox, L"ab");
    SendMessageW(panel, WM_COMMAND, SEND_ID, 0);
#endif
    editRelease(1234);
    assert(editCreate(title, initial, MR_EDIT_PASSWORD, 10) == 1234);
    pump();
    assert(editWaiting() && eventCount == 5);
#ifdef VMRP_GTK
    assert(gtk_widget_get_visible(passwordBox) && !gtk_entry_get_visibility(GTK_ENTRY(passwordBox)));
    g_signal_emit_by_name(cancelButton, "clicked");
#else
    assert((GetWindowLongPtrW(passwordBox, GWL_STYLE) & ES_PASSWORD) != 0);
    SendMessageW(panel, WM_COMMAND, CANCEL_ID, 0);
#endif
    pump();
    assert(!editWaiting() && eventCount == 6 && parameters[5] == MR_DIALOG_KEY_CANCEL);
#ifdef VMRP_GTK
    assert(gtk_editable_get_text(GTK_EDITABLE(passwordBox))[0] == '\0');
#else
    assert(GetWindowTextLengthW(passwordBox) == 0);
#endif
    const char selected[] = "mythroad/example.mrp";
    queuePackageOpen(selected);
    SDL_Event selection;
    assert(SDL_PeepEvents(&selection, 1, SDL_GETEVENT, SDL_DROPFILE, SDL_DROPFILE) == 1);
    assert(strcmp(selection.drop.file, selected) == 0 && selection.drop.file != selected);
    SDL_free(selection.drop.file);
    clickRotate();
    int width, height;
    SDL_GetWindowSize(screen, &width, &height);
    assert(width == 320 && height == 240 && eventCount == 6);
    assert(queuePhoneKey(1, MR_KEY_UP) == MR_SUCCESS);
    pump();
    assert(eventCount == 7 && parameters[6] == MR_KEY_LEFT);
    clickRotate();
    assert(queuePhoneKey(0, MR_KEY_UP) == MR_SUCCESS);
    assert(queuePhoneKey(1, MR_KEY_UP) == MR_SUCCESS);
    assert(queuePhoneKey(0, MR_KEY_UP) == MR_SUCCESS);
    pump();
    assert(eventCount == 10);
    assert(codes[7] == MR_KEY_RELEASE && parameters[7] == MR_KEY_LEFT);
    assert(codes[8] == MR_KEY_PRESS && parameters[8] == MR_KEY_DOWN);
    assert(codes[9] == MR_KEY_RELEASE && parameters[9] == MR_KEY_DOWN);
    clickRotate();
    clickRotate();
    SDL_GetWindowSize(screen, &width, &height);
    assert(width == 240 && height == 320 && eventCount == 10);
    SDL_SetEventFilter(rejectEvent, NULL);
    assert(queuePhoneKey(1, MR_KEY_1) == MR_FAILED && controlsHaveFailed());
    SDL_SetEventFilter(NULL, NULL);
    editRelease(1234);
#ifdef _WIN32
    if (argc > 1 && strcmp(argv[1], "--capture") == 0) {
        wchar_t original[MAX_PATH], temporary[MAX_PATH], directory[MAX_PATH], buttonText[32];
        assert(GetCurrentDirectoryW(SDL_arraysize(original), original) > 0);
        assert(GetTempPathW(SDL_arraysize(temporary), temporary) > 0);
        assert(GetTempFileNameW(temporary, L"vmr", 0, directory));
        assert(DeleteFileW(directory) && CreateDirectoryW(directory, NULL));
        assert(SetCurrentDirectoryW(directory));
        SendMessageW(panel, WM_COMMAND, PHOTO_ID, 0);
        assert(GetFileAttributesW(L"captures") == INVALID_FILE_ATTRIBUTES);
        pump();
        SendMessageW(panel, WM_COMMAND, RECORD_ID, 0);
        assert(!captureIsRecording());
        pump();
        assert(captureIsRecording());
        assert(GetWindowTextW(recordButton, buttonText, SDL_arraysize(buttonText)) > 0);
        assert(wcscmp(buttonText, L"Stop Rec") == 0);
        SDL_Delay(180);
        clickRotate();
        SDL_Delay(180);
        pump();
        SendMessageW(panel, WM_COMMAND, RECORD_ID, 0);
        pump();
        assert(!captureIsRecording());
        assert(GetWindowTextW(recordButton, buttonText, SDL_arraysize(buttonText)) > 0);
        assert(wcscmp(buttonText, L"Record") == 0);
        SendMessageW(panel, WM_COMMAND, RECORD_ID, 0);
        pump();
        assert(captureIsRecording());
        SDL_Delay(180);
        destroyControls();
        assert(!captureIsRecording());
        removeTestCaptures(L"captures\\photo-*.png", 1);
        removeTestCaptures(L"captures\\video-*.mp4", 2);
        assert(RemoveDirectoryW(L"captures"));
        assert(SetCurrentDirectoryW(original));
        assert(RemoveDirectoryW(directory));
    } else {
        destroyControls();
    }
#else
    (void)argc;
    (void)argv;
    destroyControls();
#endif
    SDL_DestroyWindow(screen);
    SDL_Quit();
    puts("Native keypad and Console tests passed");
    return 0;
}
