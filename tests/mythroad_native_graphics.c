#define wmain native_app_wmain
#include "..\tools\mythroad_native.c"
#undef wmain

static int expect_pixel(int x, int y, COLORREF expected) {
    COLORREF actual = GetPixel(app.canvas, x, y);
    if (actual == expected) return 1;
    fprintf(stderr, "pixel (%d,%d): expected %06lx, got %06lx\n",
            x, y, (unsigned long)expected, (unsigned long)actual);
    return 0;
}

int wmain(int argc, wchar_t **argv) {
    const char *source =
        "_clearScr(0,0,0)\n"
        "_drawRect(-2,-1,4,3,200,0,0)\n"
        "_drawRect(239,319,2,2,0,0,200)\n"
        "_drawRect(4,4,0,5,255,255,255)\n"
        "_drawRect(4,4,-3,5,255,255,255)\n"
        "_drawPoint(1,1,0,200,0)\n"
        "_drawPoint(-1,0,255,255,255)\n"
        "_drawLine(-2,4,2,4,200,200,0)\n"
        "_drawLine(8,8,6,6,0,200,200)\n"
        "_drawLine(10,10,10,10,200,0,200)\n"
        "_drawLine(12,13,11,10,100,150,200)\n";
    Reader reader = {source, strlen(source)};
    char *snake_source;
    size_t snake_length;
    int ok = 1;
    if (argc != 2) {
        fprintf(stderr, "usage: mythroad-native-graphics-tests snake.mr\n");
        return 1;
    }
    if (!create_display()) {
        fprintf(stderr, "could not initialize test display\n");
        destroy_app();
        return 1;
    }
    mr_tm_init();
    app.vm = mrp_open();
    if (!app.vm) {
        fprintf(stderr, "could not initialize test VM\n");
        destroy_app();
        return 1;
    }
    mrp_register(app.vm, "_clearScr", api_clear);
    mrp_register(app.vm, "_drawRect", api_draw_rect);
    mrp_register(app.vm, "_drawLine", api_draw_line);
    mrp_register(app.vm, "_drawPoint", api_draw_point);
    if (mrp_load(app.vm, read_source, &reader, "@graphics-test.mr") ||
        mrp_pcall(app.vm, 0, 0, 0)) {
        report_vm_error(app.vm, "graphics test failed");
        destroy_app();
        return 1;
    }
    ok &= expect_pixel(0, 0, RGB(200, 0, 0));
    ok &= expect_pixel(1, 0, RGB(200, 0, 0));
    ok &= expect_pixel(2, 0, RGB(0, 0, 0));
    ok &= expect_pixel(1, 1, RGB(0, 200, 0));
    ok &= expect_pixel(0, 2, RGB(0, 0, 0));
    ok &= expect_pixel(239, 319, RGB(0, 0, 200));
    ok &= expect_pixel(0, 4, RGB(200, 200, 0));
    ok &= expect_pixel(2, 4, RGB(200, 200, 0));
    ok &= expect_pixel(3, 4, RGB(0, 0, 0));
    ok &= expect_pixel(6, 6, RGB(0, 200, 200));
    ok &= expect_pixel(7, 7, RGB(0, 200, 200));
    ok &= expect_pixel(8, 8, RGB(0, 200, 200));
    ok &= expect_pixel(10, 10, RGB(200, 0, 200));
    ok &= expect_pixel(11, 10, RGB(100, 150, 200));
    ok &= expect_pixel(12, 13, RGB(100, 150, 200));

    mrp_register(app.vm, "_str", api_str);
    mrp_register(app.vm, "_drawText", api_draw_text);
    mrp_register(app.vm, "_dispUp", api_display);
    mrp_register(app.vm, "_timerStart", api_timer_start);
    snake_source = load_file(argv[1], &snake_length);
    if (!snake_source) {
        destroy_app();
        return 1;
    }
    reader.data = snake_source;
    reader.length = snake_length;
    if (mrp_load(app.vm, read_source, &reader, "@snake.mr")) {
        free(snake_source);
        report_vm_error(app.vm, "snake compilation failed");
        destroy_app();
        return 1;
    }
    free(snake_source);
    if (mrp_pcall(app.vm, 0, 0, 0)) {
        report_vm_error(app.vm, "snake startup failed");
        destroy_app();
        return 1;
    }
    ok &= expect_pixel(30, 58, RGB(18, 31, 39));
    ok &= expect_pixel(67, 95, RGB(87, 220, 142));
    ok &= expect_pixel(50, 96, RGB(49, 160, 108));
    ok &= expect_pixel(105, 99, RGB(238, 82, 72));
    if (!send_event(MR_KEY_PRESS, MR_KEY_SELECT, 0) || !run_timer()) {
        destroy_app();
        return 1;
    }
    ok &= expect_pixel(85, 95, RGB(87, 220, 142));
    ok &= expect_pixel(68, 96, RGB(49, 160, 108));
    ok &= expect_pixel(32, 96, RGB(18, 31, 39));
    destroy_app();
    if (!ok) return 1;
    puts("PASS: drawing colors, clipping, line endpoints and snake redraw.");
    return 0;
}
