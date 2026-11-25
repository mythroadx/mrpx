#ifndef VMRP_CONTROLS_H
#define VMRP_CONTROLS_H

#include <stdbool.h>
#include <SDL2/SDL.h>

bool initControls(SDL_Window *screen);
void destroyControls(void);
void toggleControls(void);
void pumpControls(void);
bool handleControlEvent(const SDL_Event *event);
bool controlsHaveFailed(void);
#ifndef __EMSCRIPTEN__
void openPackageDialog(void);
#endif
int queuePhoneKey(int pressed, int key);
void consoleGuestLog(const char *text);
void consoleMessage(const char *text);
void controlsEditChanged(const char *title, const char *text, int type, int maxSize);

#endif
