#ifndef VMRP_DISPLAY_H
#define VMRP_DISPLAY_H

#include <stdbool.h>
#include <stdint.h>
#include <SDL2/SDL.h>

bool initDisplay(SDL_Window *screen);
bool clearDisplay(void);
bool redrawDisplay(void);
bool drawDisplayBitmap(const uint16_t *bitmap, int x, int y, int width, int height);
bool rotateDisplay(void);
void displayPoint(int x, int y, int *guestX, int *guestY);
int displayKey(int key);

#endif
