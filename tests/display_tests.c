#include <assert.h>
#include <stdio.h>
#include "vmrp/display.h"
#include "vmrp/vmrp.h"

static void checkPixel(SDL_Window *window, int x, int y, uint16_t color) {
    SDL_Surface *surface = SDL_GetWindowSurface(window);
    assert(surface != NULL);
    const uint8_t *pixel = (uint8_t *)surface->pixels + y * surface->pitch +
                           x * surface->format->BytesPerPixel;
    uint32_t rgb;
    assert(SDL_ConvertPixels(1, 1, surface->format->format, pixel, surface->pitch,
                             SDL_PIXELFORMAT_RGB888, &rgb, sizeof(rgb)) == 0);
    assert(rgb == ((PIXEL565R(color) << 16) | (PIXEL565G(color) << 8) | PIXEL565B(color)));
}

int main(void) {
    assert(SDL_setenv("SDL_VIDEODRIVER", "dummy", 1) == 0);
    assert(SDL_Init(SDL_INIT_VIDEO) == 0);
    SDL_Window *window = SDL_CreateWindow("display test", 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT,
                                          SDL_WINDOW_HIDDEN);
    assert(window != NULL && initDisplay(window));
    uint16_t bitmap[SCREEN_WIDTH * SCREEN_HEIGHT] = {0};
    const int guestPoints[][2] = {{0, 0}, {239, 0}, {239, 319}, {0, 319}, {17, 29}};
    const int screenPoints[][5][2] = {
        {{0, 0}, {239, 0}, {239, 319}, {0, 319}, {17, 29}},
        {{319, 0}, {319, 239}, {0, 239}, {0, 0}, {290, 17}},
        {{239, 319}, {0, 319}, {0, 0}, {239, 0}, {222, 290}},
        {{0, 239}, {0, 0}, {319, 0}, {319, 239}, {29, 222}}
    };
    uint16_t colors[] = {0xf800, 0x07e0, 0x001f, 0xffff, 0x1234};
    const int directions[] = {MR_KEY_UP, MR_KEY_RIGHT, MR_KEY_DOWN, MR_KEY_LEFT};
    const int expectedKeys[][4] = {
        {MR_KEY_UP, MR_KEY_RIGHT, MR_KEY_DOWN, MR_KEY_LEFT},
        {MR_KEY_LEFT, MR_KEY_UP, MR_KEY_RIGHT, MR_KEY_DOWN},
        {MR_KEY_DOWN, MR_KEY_LEFT, MR_KEY_UP, MR_KEY_RIGHT},
        {MR_KEY_RIGHT, MR_KEY_DOWN, MR_KEY_LEFT, MR_KEY_UP}
    };
    for (size_t i = 0; i < SDL_arraysize(guestPoints); i++)
        bitmap[guestPoints[i][1] * SCREEN_WIDTH + guestPoints[i][0]] = colors[i];
    assert(drawDisplayBitmap(bitmap, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT));
    for (int turn = 0; turn < 4; turn++) {
        int width, height;
        SDL_GetWindowSize(window, &width, &height);
        assert(width == (turn % 2 ? 320 : 240));
        assert(height == (turn % 2 ? 240 : 320));
        for (size_t i = 0; i < SDL_arraysize(guestPoints); i++) {
            int x = screenPoints[turn][i][0], y = screenPoints[turn][i][1];
            checkPixel(window, x, y, colors[i]);
            int guestX, guestY;
            displayPoint(x, y, &guestX, &guestY);
            assert(guestX == guestPoints[i][0] && guestY == guestPoints[i][1]);
        }
        for (size_t i = 0; i < SDL_arraysize(directions); i++)
            assert(displayKey(directions[i]) == expectedKeys[turn][i]);
        assert(displayKey(MR_KEY_2) == MR_KEY_2);
        assert(displayKey(MR_KEY_SOFTLEFT) == MR_KEY_SOFTLEFT);
        assert(displayKey(MR_KEY_SELECT) == MR_KEY_SELECT);

        colors[4] += 0x1111;
        bitmap[29 * SCREEN_WIDTH + 17] = colors[4];
        assert(drawDisplayBitmap(bitmap, 17, 29, 1, 1));
        checkPixel(window, screenPoints[turn][4][0], screenPoints[turn][4][1], colors[4]);
        checkPixel(window, screenPoints[turn][0][0], screenPoints[turn][0][1], colors[0]);
        assert(rotateDisplay());
    }
    checkPixel(window, 17, 29, colors[4]);
    bitmap[0] = 0xabcd;
    assert(drawDisplayBitmap(bitmap, -3, -5, 4, 6));
    checkPixel(window, 0, 0, bitmap[0]);
    checkPixel(window, 239, 319, colors[2]);
    assert(clearDisplay());
    checkPixel(window, 0, 0, 0);
    checkPixel(window, 239, 319, 0);
    SDL_DestroyWindow(window);
    SDL_Quit();
    puts("Display rotation, repaint and input mapping tests passed.");
    return 0;
}
