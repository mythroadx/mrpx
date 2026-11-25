#include "vmrp/dispatch.h"

#include <stdio.h>
#include <stdlib.h>

#include "vmrp/bridge.h"
#include "vmrp/network.h"
#include "vmrp/vmrp.h"

enum {
    TIMER_EVENT,
    NETWORK_EVENT
};

typedef struct {
    uint64_t runtimeGeneration;
    uint64_t networkGeneration;
    uint32_t callbackAddr;
    int32_t result;
    uint32_t userDataAddr;
} NetworkCallback;

static SDL_SpinLock dispatchLock;
static Uint32 runtimeEventType;
static SDL_threadID ownerThread;
static bool acceptingEvents;
static bool dispatchFailed;
static uint64_t runtimeGeneration;
static uint32_t timerGeneration;
static SDL_TimerID timeId;

static bool isOwnerThread(void) {
    if (ownerThread != SDL_ThreadID()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Runtime dispatch must run on the main thread.");
        return false;
    }
    return true;
}

static bool pushRuntimeEvent(SDL_Event *event) {
    int result = SDL_PushEvent(event);
    if (result != 1) {
        if (result == 0) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Runtime event was rejected by an SDL event filter.");
        } else {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not queue runtime event: %s", SDL_GetError());
        }
        dispatchFailed = true;
        return false;
    }
    return true;
}

int32_t initRuntimeDispatch(void) {
    SDL_AtomicLock(&dispatchLock);
    if (acceptingEvents) {
        SDL_AtomicUnlock(&dispatchLock);
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Runtime dispatch is already initialized.");
        return MR_FAILED;
    }
    Uint32 type = SDL_RegisterEvents(1);
    if (type == (Uint32)-1) {
        SDL_AtomicUnlock(&dispatchLock);
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not register runtime events: %s", SDL_GetError());
        return MR_FAILED;
    }
    runtimeEventType = type;
    ownerThread = SDL_ThreadID();
    runtimeGeneration++;
    acceptingEvents = true;
    dispatchFailed = false;
    SDL_AtomicUnlock(&dispatchLock);
    return MR_SUCCESS;
}

uint64_t getRuntimeGeneration(void) {
    SDL_AtomicLock(&dispatchLock);
    uint64_t generation = acceptingEvents ? runtimeGeneration : 0;
    SDL_AtomicUnlock(&dispatchLock);
    return generation;
}

bool runtimeDispatchFailed(void) {
    SDL_AtomicLock(&dispatchLock);
    bool failed = dispatchFailed;
    SDL_AtomicUnlock(&dispatchLock);
    return failed;
}

static Uint32 timerCb(Uint32 interval, void *param) {
    uint32_t generation = (uint32_t)(uintptr_t)param;
    SDL_AtomicLock(&dispatchLock);
    if (acceptingEvents && generation == timerGeneration) {
        SDL_Event event = {0};
        event.type = runtimeEventType;
        event.user.code = TIMER_EVENT;
        event.user.data1 = param;
        pushRuntimeEvent(&event);
    }
    SDL_AtomicUnlock(&dispatchLock);
    return 0;
}

int32_t timerStop(void) {
    if (!isOwnerThread()) {
        return MR_FAILED;
    }
    SDL_AtomicLock(&dispatchLock);
    timerGeneration++;
    SDL_AtomicUnlock(&dispatchLock);
    if (timeId != 0) {
        SDL_RemoveTimer(timeId);
        timeId = 0;
    }
    return MR_SUCCESS;
}

int32_t timerStart(uint16_t interval) {
    if (timerStop() != MR_SUCCESS) {
        return MR_FAILED;
    }
    SDL_AtomicLock(&dispatchLock);
    bool active = acceptingEvents;
    uint32_t generation = timerGeneration;
    SDL_AtomicUnlock(&dispatchLock);
    if (!active) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Cannot start timer on a stopped runtime.");
        return MR_FAILED;
    }
    timeId = SDL_AddTimer(interval, timerCb, (void *)(uintptr_t)generation);
    if (timeId == 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not start runtime timer: %s", SDL_GetError());
        return MR_FAILED;
    }
    return MR_SUCCESS;
}

int32_t queueNetworkCallback(uint64_t generation, uint64_t networkToken,
                             uint32_t callbackAddr, int32_t result, uint32_t userDataAddr) {
    // This lock also prevents SDL_PushEvent from racing with SDL_Quit.
    SDL_AtomicLock(&dispatchLock);
    if (!acceptingEvents || generation == 0 || generation != runtimeGeneration ||
        networkToken != getNetworkGeneration()) {
        SDL_AtomicUnlock(&dispatchLock);
        printf("Discarding network callback for a stopped runtime or closed network.\n");
        return MR_FAILED;
    }
    NetworkCallback *callback = malloc(sizeof(*callback));
    if (callback == NULL) {
        dispatchFailed = true;
        SDL_AtomicUnlock(&dispatchLock);
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not allocate network callback.");
        return MR_FAILED;
    }
    *callback = (NetworkCallback){generation, networkToken, callbackAddr, result, userDataAddr};
    SDL_Event event = {0};
    event.type = runtimeEventType;
    event.user.code = NETWORK_EVENT;
    event.user.data1 = callback;
    bool queued = pushRuntimeEvent(&event);
    SDL_AtomicUnlock(&dispatchLock);
    if (!queued) {
        free(callback);
        return MR_FAILED;
    }
    return MR_SUCCESS;
}

bool dispatchRuntimeEvent(const SDL_Event *event) {
    if (event->type != runtimeEventType) {
        return false;
    }
    if (!isOwnerThread()) {
        if (event->user.code == NETWORK_EVENT) {
            free(event->user.data1);
        }
        SDL_AtomicLock(&dispatchLock);
        dispatchFailed = true;
        SDL_AtomicUnlock(&dispatchLock);
        return true;
    }
    if (event->user.code == TIMER_EVENT) {
        SDL_AtomicLock(&dispatchLock);
        bool current = acceptingEvents &&
                       (uint32_t)(uintptr_t)event->user.data1 == timerGeneration;
        SDL_AtomicUnlock(&dispatchLock);
        if (current) {
            timeId = 0;
            timer();
        }
    } else if (event->user.code == NETWORK_EVENT) {
        NetworkCallback callback = *(NetworkCallback *)event->user.data1;
        free(event->user.data1);
        if (callback.runtimeGeneration == getRuntimeGeneration() &&
            callback.networkGeneration == getNetworkGeneration()) {
            networkCallback(callback.callbackAddr, callback.result, callback.userDataAddr);
        }
    } else {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Unknown runtime event: %d", event->user.code);
        SDL_AtomicLock(&dispatchLock);
        dispatchFailed = true;
        SDL_AtomicUnlock(&dispatchLock);
    }
    return true;
}

void stopRuntimeDispatch(void) {
    if (runtimeEventType == 0 || !isOwnerThread()) {
        return;
    }
    SDL_AtomicLock(&dispatchLock);
    acceptingEvents = false;
    runtimeGeneration++;
    SDL_AtomicUnlock(&dispatchLock);
    timerStop();
    SDL_Event event;
    int result;
    while ((result = SDL_PeepEvents(&event, 1, SDL_GETEVENT, runtimeEventType, runtimeEventType)) > 0) {
        if (event.user.code == NETWORK_EVENT) {
            free(event.user.data1);
        }
    }
    if (result < 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not drain runtime events: %s", SDL_GetError());
    }
}
