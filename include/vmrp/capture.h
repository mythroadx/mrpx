#ifndef VMRP_CAPTURE_H
#define VMRP_CAPTURE_H

#include <stdbool.h>
#include <stddef.h>
#include <SDL2/SDL.h>

#ifdef _WIN32
#define VMRP_CAPTURE_PATH_SIZE 192
bool capturePhoto(SDL_Window *window, char *path, size_t pathSize);
bool captureStart(SDL_Window *window, char *path, size_t pathSize);
bool captureUpdate(SDL_Window *window);
bool captureStop(char *path, size_t pathSize);
bool captureIsRecording(void);
#endif

#endif
