#define WIN32_LEAN_AND_MEAN
#include <stddef.h>
#include <stdint.h>
#include <corecrt_memory.h>
#include <corecrt_wstring.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include <windowsx.h>

#include "mr.h"

#define WIDTH 240
#define HEIGHT 320
#define MAX_SOURCE_SIZE (1024 * 1024)

typedef struct {
    const char *data;
    size_t length;
} Reader;

typedef struct {
    mrp_State *vm;
    HWND window;
    HDC canvas;
    HBITMAP bitmap;
    HGDIOBJ old_bitmap;
    UINT_PTR timer;
    UINT_PTR next_timer;
    char timer_callback[128];
    int failed;
    int exited;
} App;

static App app;

static const char *read_source(mrp_State *vm, void *data, size_t *size) {
    Reader *reader = (Reader *)data;
    (void)vm;
    *size = reader->length;
    reader->length = 0;
    return reader->data;
}

static int script_error(mrp_State *vm, const char *message) {
    mrp_pushstring(vm, message);
    return mrp_error(vm);
}

static int integer_arg(mrp_State *vm, int index, int *value) {
    if (mrp_type(vm, index) != MRP_TNUMBER) {
        script_error(vm, "expected an integer argument");
        return 0;
    }
    *value = (int)mrp_tonumber(vm, index);
    return 1;
}

static int color_args(mrp_State *vm, int index, COLORREF *color) {
    int r, g, b;
    if (!integer_arg(vm, index, &r) || !integer_arg(vm, index + 1, &g) ||
        !integer_arg(vm, index + 2, &b)) return 0;
    if (r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255) {
        script_error(vm, "color components must be between 0 and 255");
        return 0;
    }
    *color = RGB(r, g, b);
    return 1;
}

static int api_print(mrp_State *vm) {
    int count = mrp_gettop(vm);
    for (int i = 1; i <= count; i++) {
        const char *s = mrp_tostring(vm, i);
        if (i > 1 && fputc('\t', stdout) == EOF) return script_error(vm, "could not write output");
        if (s) {
            size_t length = mrp_strlen(vm, i);
            if (fwrite(s, 1, length, stdout) != length) return script_error(vm, "could not write output");
        } else if (fputs(mrp_typename(vm, mrp_type(vm, i)), stdout) == EOF) {
            return script_error(vm, "could not write output");
        }
    }
    if (fputc('\n', stdout) == EOF || fflush(stdout)) return script_error(vm, "could not write output");
    return 0;
}

static int api_str(mrp_State *vm) {
    const char *s;
    if (mrp_gettop(vm) != 1) return script_error(vm, "_str expects one argument");
    s = mrp_tostring(vm, 1);
    if (!s) return script_error(vm, "_str expects a string or number");
    mrp_pushlstring(vm, s, mrp_strlen(vm, 1));
    return 1;
}

static int api_clear(mrp_State *vm) {
    COLORREF color;
    RECT rect = {0, 0, WIDTH, HEIGHT};
    HBRUSH brush;
    if (mrp_gettop(vm) != 3) return script_error(vm, "_clearScr expects three arguments");
    if (!color_args(vm, 1, &color)) return 0;
    brush = CreateSolidBrush(color);
    if (!brush) return script_error(vm, "could not create screen brush");
    if (!FillRect(app.canvas, &rect, brush)) {
        DeleteObject(brush);
        return script_error(vm, "could not clear screen");
    }
    DeleteObject(brush);
    return 0;
}

static int coordinate_arg(mrp_State *vm, int index, int *value) {
    if (!integer_arg(vm, index, value)) return 0;
    if (*value < INT16_MIN || *value > INT16_MAX) {
        script_error(vm, "drawing coordinates and dimensions must fit in int16");
        return 0;
    }
    return 1;
}

static int draw_pixel(int x, int y, COLORREF color) {
    if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT) return 1;
    return SetPixelV(app.canvas, x, y, color) != 0;
}

static int api_draw_rect(mrp_State *vm) {
    int x, y, w, h;
    RECT rect;
    COLORREF color;
    HBRUSH brush;
    BOOL ok;
    if (mrp_gettop(vm) != 7) return script_error(vm, "_drawRect expects x, y, w, h, r, g, b");
    if (!coordinate_arg(vm, 1, &x) || !coordinate_arg(vm, 2, &y) ||
        !coordinate_arg(vm, 3, &w) || !coordinate_arg(vm, 4, &h) ||
        !color_args(vm, 5, &color)) return 0;
    if (w <= 0 || h <= 0 || x >= WIDTH || y >= HEIGHT || x + w <= 0 || y + h <= 0)
        return 0;
    rect.left = x < 0 ? 0 : x;
    rect.top = y < 0 ? 0 : y;
    rect.right = x + w > WIDTH ? WIDTH : x + w;
    rect.bottom = y + h > HEIGHT ? HEIGHT : y + h;
    brush = CreateSolidBrush(color);
    if (!brush) return script_error(vm, "could not create rectangle brush");
    ok = FillRect(app.canvas, &rect, brush);
    DeleteObject(brush);
    if (!ok) return script_error(vm, "could not draw rectangle");
    return 0;
}

static int api_draw_point(mrp_State *vm) {
    int x, y;
    COLORREF color;
    if (mrp_gettop(vm) != 5) return script_error(vm, "_drawPoint expects x, y, r, g, b");
    if (!coordinate_arg(vm, 1, &x) || !coordinate_arg(vm, 2, &y) ||
        !color_args(vm, 3, &color)) return 0;
    if (!draw_pixel(x, y, color)) return script_error(vm, "could not draw point");
    return 0;
}

static int api_draw_line(mrp_State *vm) {
    int x1, y1, x2, y2, x, y, dx, dy, c1, c2, err, swap = 0, temp;
    COLORREF color;
    if (mrp_gettop(vm) != 7) return script_error(vm, "_drawLine expects x1, y1, x2, y2, r, g, b");
    if (!coordinate_arg(vm, 1, &x1) || !coordinate_arg(vm, 2, &y1) ||
        !coordinate_arg(vm, 3, &x2) || !coordinate_arg(vm, 4, &y2) ||
        !color_args(vm, 5, &color)) return 0;
    dx = x2 - x1;
    dy = y2 - y1;
    if (abs(dx) < abs(dy)) {
        swap = 1;
        temp = x1; x1 = y1; y1 = temp;
        temp = x2; x2 = y2; y2 = temp;
    }
    if (x1 > x2) {
        temp = x1; x1 = x2; x2 = temp;
        temp = y1; y1 = y2; y2 = temp;
    }
    dx = x2 - x1;
    c1 = (y2 - y1) * 2;
    dy = 1;
    if (c1 < 0) { c1 = -c1; dy = -1; }
    err = c1 - dx;
    c2 = err - dx;
    x = x1;
    y = y1;
    while (x <= x2) {
        if (!draw_pixel(swap ? y : x, swap ? x : y, color))
            return script_error(vm, "could not draw line");
        x++;
        if (err < 0) err += c1;
        else { y += dy; err += c2; }
    }
    return 0;
}

static int api_draw_text(mrp_State *vm) {
    const char *text;
    wchar_t *wide;
    int count, x, y;
    COLORREF color;
    BOOL ok;
    if (mrp_gettop(vm) != 6 || mrp_type(vm, 1) != MRP_TSTRING)
        return script_error(vm, "_drawText expects text, x, y, r, g, b");
    text = mrp_tostring(vm, 1);
    if (memchr(text, 0, mrp_strlen(vm, 1))) return script_error(vm, "_drawText does not support NUL in text");
    if (!integer_arg(vm, 2, &x) || !integer_arg(vm, 3, &y) ||
        !color_args(vm, 4, &color)) return 0;
    count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, NULL, 0);
    if (!count) return script_error(vm, "_drawText received invalid UTF-8");
    wide = (wchar_t *)malloc((size_t)count * sizeof(*wide));
    if (!wide) return script_error(vm, "out of memory drawing text");
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, wide, count)) {
        free(wide);
        return script_error(vm, "could not convert text");
    }
    if (!SetBkMode(app.canvas, TRANSPARENT) || SetTextColor(app.canvas, color) == CLR_INVALID) {
        free(wide);
        return script_error(vm, "could not configure text drawing");
    }
    ok = TextOutW(app.canvas, x, y, wide, count - 1);
    free(wide);
    if (!ok) return script_error(vm, "could not draw text");
    return 0;
}

static int api_display(mrp_State *vm) {
    int value;
    if (mrp_gettop(vm) != 4) return script_error(vm, "_dispUp expects four arguments");
    for (int i = 1; i <= 4; i++) {
        if (!integer_arg(vm, i, &value)) return 0;
    }
    if (!InvalidateRect(app.window, NULL, FALSE)) return script_error(vm, "could not update screen");
    return 0;
}

static int api_timer_start(mrp_State *vm) {
    int unused, ms;
    const char *callback;
    size_t length;
    UINT_PTR timer;
    if (mrp_gettop(vm) != 3) return script_error(vm, "_timerStart expects three arguments");
    if (!integer_arg(vm, 1, &unused) || !integer_arg(vm, 2, &ms)) return 0;
    if (ms < 1 || ms > 65535 || mrp_type(vm, 3) != MRP_TSTRING)
        return script_error(vm, "_timerStart expects milliseconds (1..65535) and a callback name");
    callback = mrp_tostring(vm, 3);
    length = strlen(callback);
    if (length != mrp_strlen(vm, 3) || length == 0 || length >= sizeof(app.timer_callback))
        return script_error(vm, "invalid timer callback name");
    if (app.next_timer == UINTPTR_MAX) return script_error(vm, "timer identifier exhausted");
    timer = SetTimer(app.window, ++app.next_timer, (UINT)ms, NULL);
    if (!timer) return script_error(vm, "could not start timer");
    if (app.timer) KillTimer(app.window, app.timer);
    app.timer = timer;
    memcpy(app.timer_callback, callback, length + 1);
    return 0;
}

static int api_exit(mrp_State *vm) {
    if (mrp_gettop(vm) != 0) return script_error(vm, "_exit expects no arguments");
    app.exited = 1;
    PostQuitMessage(0);
    return 0;
}

static int report_vm_error(mrp_State *vm, const char *stage) {
    const char *error = mrp_tostring(vm, -1);
    fprintf(stderr, "%s: %s\n", stage, error ? error : "(non-string error)");
    mrp_pop(vm, 1);
    app.failed = 1;
    return 0;
}

static int send_event(int code, int p0, int p1) {
    mrp_getglobal(app.vm, "dealevent");
    if (!mrp_isfunction(app.vm, -1)) {
        mrp_pop(app.vm, 1);
        return 1;
    }
    mrp_pushnumber(app.vm, code);
    mrp_pushnumber(app.vm, p0);
    mrp_pushnumber(app.vm, p1);
    if (mrp_pcall(app.vm, 3, 0, 0)) return report_vm_error(app.vm, "event failed");
    return 1;
}

static int run_timer(void) {
    mrp_getglobal(app.vm, app.timer_callback);
    if (!mrp_isfunction(app.vm, -1)) {
        fprintf(stderr, "timer callback not found: %s\n", app.timer_callback);
        mrp_pop(app.vm, 1);
        app.failed = 1;
        return 0;
    }
    if (mrp_pcall(app.vm, 0, 0, 0)) return report_vm_error(app.vm, "timer failed");
    return 1;
}

static int key_code(WPARAM key) {
    if (key >= '0' && key <= '9') return (int)(key - '0');
    switch (key) {
        case VK_UP: return MR_KEY_UP;
        case VK_DOWN: return MR_KEY_DOWN;
        case VK_LEFT: return MR_KEY_LEFT;
        case VK_RIGHT: return MR_KEY_RIGHT;
        case VK_F1: return MR_KEY_SOFTLEFT;
        case VK_F2: return MR_KEY_SOFTRIGHT;
        case VK_RETURN: return MR_KEY_SELECT;
        case 'A': return MR_KEY_A;
        case 'B': return MR_KEY_B;
        default: return -1;
    }
}

static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case WM_PAINT: {
            PAINTSTRUCT paint;
            HDC dc = BeginPaint(window, &paint);
            if (!dc || (app.canvas && !BitBlt(dc, 0, 0, WIDTH, HEIGHT, app.canvas, 0, 0, SRCCOPY))) {
                fprintf(stderr, "could not paint window: %lu\n", GetLastError());
                app.failed = 1;
                PostQuitMessage(1);
            }
            EndPaint(window, &paint);
            return 0;
        }
        case WM_KEYDOWN:
        case WM_KEYUP: {
            int key = key_code(wparam);
            if (app.vm && key >= 0 && !send_event(message == WM_KEYDOWN ? MR_KEY_PRESS : MR_KEY_RELEASE, key, 0))
                PostQuitMessage(1);
            return 0;
        }
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
            if (app.vm && !send_event(message == WM_LBUTTONDOWN ? MR_MOUSE_DOWN : MR_MOUSE_UP,
                                      GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)))
                PostQuitMessage(1);
            return 0;
        case WM_TIMER:
            if (app.vm && app.timer && wparam == app.timer) {
                KillTimer(window, app.timer);
                app.timer = 0;
                if (!run_timer()) PostQuitMessage(1);
            }
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(window, message, wparam, lparam);
    }
}

static int create_display(void) {
    WNDCLASSW cls = {0};
    BITMAPINFO info = {0};
    RECT rect = {0, 0, WIDTH, HEIGHT};
    void *pixels;
    DWORD style = WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    cls.lpfnWndProc = window_proc;
    cls.hInstance = GetModuleHandleW(NULL);
    cls.lpszClassName = L"MythroadNative";
    cls.hCursor = LoadCursorW(NULL, MAKEINTRESOURCEW(32512));
    if (!RegisterClassW(&cls) || !AdjustWindowRect(&rect, style, FALSE)) return 0;
    app.window = CreateWindowExW(0, cls.lpszClassName, L"Mythroad Native", style,
                                 CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left,
                                 rect.bottom - rect.top, NULL, NULL, cls.hInstance, NULL);
    if (!app.window) return 0;
    app.canvas = CreateCompatibleDC(NULL);
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = WIDTH;
    info.bmiHeader.biHeight = -HEIGHT;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    app.bitmap = CreateDIBSection(app.canvas, &info, DIB_RGB_COLORS, &pixels, NULL, 0);
    if (!app.canvas || !app.bitmap || !pixels) return 0;
    app.old_bitmap = SelectObject(app.canvas, app.bitmap);
    if (!app.old_bitmap || app.old_bitmap == HGDI_ERROR) return 0;
    memset(pixels, 0, WIDTH * HEIGHT * 4);
    return 1;
}

static void destroy_app(void) {
    if (app.timer && app.window) KillTimer(app.window, app.timer);
    if (app.vm) {
        mrp_close(app.vm);
        app.vm = NULL;
    }
    if (app.old_bitmap && app.old_bitmap != HGDI_ERROR) SelectObject(app.canvas, app.old_bitmap);
    if (app.bitmap) DeleteObject(app.bitmap);
    if (app.canvas) DeleteDC(app.canvas);
    if (app.window && IsWindow(app.window)) DestroyWindow(app.window);
}

static char *load_file(const wchar_t *path, size_t *length) {
    FILE *file = _wfopen(path, L"rb");
    long size;
    char *data;
    if (!file) {
        perror("cannot open source");
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) || (size = ftell(file)) < 0 ||
        size > MAX_SOURCE_SIZE || fseek(file, 0, SEEK_SET)) {
        fprintf(stderr, "source size invalid or exceeds 1 MiB\n");
        fclose(file);
        return NULL;
    }
    data = (char *)malloc((size_t)size + 1);
    if (!data) {
        fprintf(stderr, "out of memory reading source\n");
        fclose(file);
        return NULL;
    }
    if (fread(data, 1, (size_t)size, file) != (size_t)size) {
        fprintf(stderr, "cannot read source\n");
        free(data);
        fclose(file);
        return NULL;
    }
    if (fclose(file)) {
        fprintf(stderr, "cannot close source\n");
        free(data);
        return NULL;
    }
    if (memchr(data, 0, (size_t)size) || (size && (unsigned char)data[0] == 0x1b)) {
        fprintf(stderr, "only source text is supported; bytecode and EXT are not supported\n");
        free(data);
        return NULL;
    }
    data[size] = '\0';
    *length = (size_t)size;
    return data;
}

static int parse_int(const wchar_t *text, int *result) {
    wchar_t *end;
    long value;
    errno = 0;
    value = wcstol(text, &end, 10);
    if (errno || text == end || *end || value < INT_MIN || value > INT_MAX) return 0;
    *result = (int)value;
    return 1;
}

int wmain(int argc, wchar_t **argv) {
    int check = 0, has_event = 0, code = 0, p0 = 0, p1 = 0, index = 1, result = 1;
    size_t length;
    char *source;
    Reader reader;
    MSG message;
    int status;
    if (index < argc && wcscmp(argv[index], L"--check") == 0) {
        check = 1;
        index++;
        if (index < argc && wcscmp(argv[index], L"--event") == 0) {
            if (index + 3 >= argc || !parse_int(argv[index + 1], &code) ||
                !parse_int(argv[index + 2], &p0) || !parse_int(argv[index + 3], &p1)) {
                fprintf(stderr, "invalid --event arguments\n");
                return 1;
            }
            has_event = 1;
            index += 4;
        }
    }
    if (index + 1 != argc) {
        fprintf(stderr, "usage: mythroad-native [--check [--event code p0 p1]] source.mr\n");
        return 1;
    }
    source = load_file(argv[index], &length);
    if (!source) return 1;
    if (!create_display()) {
        fprintf(stderr, "could not initialize Windows display: %lu\n", GetLastError());
        free(source);
        destroy_app();
        return 1;
    }
    mr_tm_init();
    app.vm = mrp_open();
    if (!app.vm) {
        fprintf(stderr, "could not initialize Mythroad VM\n");
        free(source);
        destroy_app();
        return 1;
    }
    mrp_register(app.vm, "print", api_print);
    mrp_register(app.vm, "_str", api_str);
    mrp_register(app.vm, "_clearScr", api_clear);
    mrp_register(app.vm, "_drawRect", api_draw_rect);
    mrp_register(app.vm, "_drawLine", api_draw_line);
    mrp_register(app.vm, "_drawPoint", api_draw_point);
    mrp_register(app.vm, "_drawText", api_draw_text);
    mrp_register(app.vm, "_dispUp", api_display);
    mrp_register(app.vm, "_timerStart", api_timer_start);
    mrp_register(app.vm, "_exit", api_exit);
    reader.data = source;
    reader.length = length;
    status = mrp_load(app.vm, read_source, &reader, "@application.mr");
    free(source);
    if (status) {
        report_vm_error(app.vm, "compile failed");
    } else if (mrp_pcall(app.vm, 0, 0, 0)) {
        report_vm_error(app.vm, "startup failed");
    } else if (check) {
        if (!has_event || send_event(code, p0, p1)) result = 0;
    } else if (app.exited) {
        result = 0;
    } else {
        ShowWindow(app.window, SW_SHOW);
        UpdateWindow(app.window);
        while ((status = GetMessageW(&message, NULL, 0, 0)) > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (status < 0) fprintf(stderr, "Windows message loop failed: %lu\n", GetLastError());
        else if (!app.failed && message.wParam == 0) result = 0;
    }
    destroy_app();
    return result;
}
