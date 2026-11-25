#include "vmrp/display.h"
#include "vmrp/vmrp.h"

static SDL_Window *displayWindow;
static unsigned rotation;
static uint32_t pixels[SCREEN_WIDTH * SCREEN_HEIGHT];
static uint32_t rotatedPixels[SCREEN_WIDTH * SCREEN_HEIGHT];

void displayPoint(int x, int y, int *guestX, int *guestY) {
    switch (rotation) {
        case 1: *guestX = y; *guestY = SCREEN_HEIGHT - 1 - x; break;
        case 2: *guestX = SCREEN_WIDTH - 1 - x; *guestY = SCREEN_HEIGHT - 1 - y; break;
        case 3: *guestX = SCREEN_WIDTH - 1 - y; *guestY = x; break;
        default: *guestX = x; *guestY = y; break;
    }
}

int displayKey(int key) {
    const int directions[] = {MR_KEY_UP, MR_KEY_RIGHT, MR_KEY_DOWN, MR_KEY_LEFT};
    for (unsigned i = 0; i < SDL_arraysize(directions); i++) {
        if (key == directions[i]) return directions[(i + 4 - rotation) % 4];
    }
    return key;
}

bool redrawDisplay(void) {
    int width = rotation % 2 ? SCREEN_HEIGHT : SCREEN_WIDTH;
    int height = rotation % 2 ? SCREEN_WIDTH : SCREEN_HEIGHT;
    SDL_Surface *surface = SDL_GetWindowSurface(displayWindow);
    if (surface == NULL) return false;
    if (surface->w != width || surface->h != height) {
        SDL_SetError("Unexpected display surface size: %dx%d (expected %dx%d).",
                     surface->w, surface->h, width, height);
        return false;
    }
    const uint32_t *source = pixels;
    if (rotation != 0) {
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                int guestX, guestY;
                displayPoint(x, y, &guestX, &guestY);
                rotatedPixels[y * width + x] = pixels[guestY * SCREEN_WIDTH + guestX];
            }
        }
        source = rotatedPixels;
    }
    if (SDL_MUSTLOCK(surface) && SDL_LockSurface(surface) != 0) return false;
    int result = SDL_ConvertPixels(width, height, SDL_PIXELFORMAT_RGB888, source, width * 4,
                                   surface->format->format, surface->pixels, surface->pitch);
    if (SDL_MUSTLOCK(surface)) SDL_UnlockSurface(surface);
    return result == 0 && SDL_UpdateWindowSurface(displayWindow) == 0;
}

bool clearDisplay(void) {
    SDL_memset(pixels, 0, sizeof(pixels));
    return redrawDisplay();
}

bool initDisplay(SDL_Window *screen) {
    displayWindow = screen;
    rotation = 0;
    return clearDisplay();
}

bool drawDisplayBitmap(const uint16_t *bitmap, int x, int y, int width, int height) {
    int left = SDL_max(0, x), top = SDL_max(0, y);
    int64_t right = SDL_min((int64_t)x + width, SCREEN_WIDTH);
    int64_t bottom = SDL_min((int64_t)y + height, SCREEN_HEIGHT);
    for (int row = top; row < bottom; row++) {
        for (int col = left; col < right; col++) {
            size_t index = row * SCREEN_WIDTH + col;
            uint16_t color = bitmap[index];
            pixels[index] = (PIXEL565R(color) << 16) | (PIXEL565G(color) << 8) | PIXEL565B(color);
        }
    }
    return redrawDisplay();
}

bool rotateDisplay(void) {
    rotation = (rotation + 1) % 4;
    SDL_SetWindowSize(displayWindow, rotation % 2 ? SCREEN_HEIGHT : SCREEN_WIDTH,
                      rotation % 2 ? SCREEN_WIDTH : SCREEN_HEIGHT);
    return redrawDisplay();
}
