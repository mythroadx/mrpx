#ifndef VMRP_DISPATCH_H
#define VMRP_DISPATCH_H

#include <stdbool.h>
#include <stdint.h>
#include <SDL2/SDL.h>

int32_t initRuntimeDispatch(void);
void stopRuntimeDispatch(void);
bool runtimeDispatchFailed(void);
bool dispatchRuntimeEvent(const SDL_Event *event);
uint64_t getRuntimeGeneration(void);
int32_t queueNetworkCallback(uint64_t runtimeGeneration, uint64_t networkGeneration,
                             uint32_t callbackAddr, int32_t result, uint32_t userDataAddr);

#endif
