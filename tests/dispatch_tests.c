#include <assert.h>
#include <stdatomic.h>
#include <stdio.h>

#include "vmrp/dispatch.h"
#include "vmrp/bridge.h"

static SDL_threadID mainThread;
static int timerCount;
static int repeatCount;
static int callbackCount;
static _Atomic uint64_t networkGeneration = 1;

uint64_t getNetworkGeneration(void) {
    return atomic_load(&networkGeneration);
}

int32_t timer(void) {
    assert(SDL_ThreadID() == mainThread);
    timerCount++;
    if (--repeatCount > 0) {
        assert(timerStart(1) == MR_SUCCESS);
    }
    return MR_SUCCESS;
}

int32_t networkCallback(uint32_t addr, int32_t result, uint32_t userDataAddr) {
    assert(SDL_ThreadID() == mainThread);
    assert(addr == 0x80008);
    assert(result == MR_FAILED);
    assert(userDataAddr == 0x280000);
    callbackCount++;
    return MR_SUCCESS;
}

static SDL_Event waitRuntimeEvent(void) {
    Uint32 start = SDL_GetTicks();
    SDL_Event event;
    while (SDL_GetTicks() - start < 5000) {
        if (SDL_WaitEventTimeout(&event, 50) && event.type >= SDL_USEREVENT) {
            return event;
        }
    }
    assert(!"Timed out waiting for runtime event");
    return (SDL_Event){0};
}

static void testTimerDeliveryAndCancellation(void) {
    assert(timerStart(1) == MR_SUCCESS);
    SDL_Event stale = waitRuntimeEvent();
    assert(timerCount == 0);
    assert(timerStop() == MR_SUCCESS);
    assert(dispatchRuntimeEvent(&stale));
    assert(timerCount == 0);

    assert(timerStart(1) == MR_SUCCESS);
    stale = waitRuntimeEvent();
    assert(timerStart(1) == MR_SUCCESS);
    assert(dispatchRuntimeEvent(&stale));
    assert(timerCount == 0);
    SDL_Event current = waitRuntimeEvent();
    assert(dispatchRuntimeEvent(&current));
    assert(timerCount == 1);

    repeatCount = 3;
    assert(timerStart(1) == MR_SUCCESS);
    for (int i = 0; i < 3; i++) {
        current = waitRuntimeEvent();
        assert(dispatchRuntimeEvent(&current));
    }
    assert(timerCount == 4);
}

static int postFromWorker(void *arg) {
    uint64_t generation = *(uint64_t *)arg;
    assert(SDL_ThreadID() != mainThread);
    return queueNetworkCallback(generation, getNetworkGeneration(), 0x80008, MR_FAILED, 0x280000);
}

static void testNetworkDeliveryAndCancellation(void) {
    uint64_t generation = getRuntimeGeneration();
    SDL_Thread *worker = SDL_CreateThread(postFromWorker, "callback-test", &generation);
    assert(worker != NULL);
    int result;
    SDL_WaitThread(worker, &result);
    assert(result == MR_SUCCESS);
    assert(callbackCount == 0);
    SDL_Event event = waitRuntimeEvent();
    assert(dispatchRuntimeEvent(&event));
    assert(callbackCount == 1);

    assert(queueNetworkCallback(generation, getNetworkGeneration(), 0x80008, MR_FAILED, 0x280000) == MR_SUCCESS);
    atomic_fetch_add(&networkGeneration, 1);
    event = waitRuntimeEvent();
    assert(dispatchRuntimeEvent(&event));
    assert(callbackCount == 1);

    assert(queueNetworkCallback(generation, getNetworkGeneration(), 0x80008, MR_FAILED, 0x280000) == MR_SUCCESS);
    stopRuntimeDispatch();
    SDL_Event pending;
    assert(SDL_PeepEvents(&pending, 1, SDL_PEEKEVENT, SDL_USEREVENT, SDL_LASTEVENT) == 0);
    assert(initRuntimeDispatch() == MR_SUCCESS);
    assert(getRuntimeGeneration() != generation);
    assert(queueNetworkCallback(generation, getNetworkGeneration(), 0x80008, MR_FAILED, 0x280000) == MR_FAILED);
    assert(callbackCount == 1);
}

static int rejectRuntimeEvent(void *arg, SDL_Event *event) {
    return event->type < SDL_USEREVENT;
}

static void testQueueFailure(void) {
    SDL_SetEventFilter(rejectRuntimeEvent, NULL);
    assert(queueNetworkCallback(getRuntimeGeneration(), getNetworkGeneration(),
                                0x80008, MR_FAILED, 0x280000) == MR_FAILED);
    assert(runtimeDispatchFailed());
    SDL_SetEventFilter(NULL, NULL);
    stopRuntimeDispatch();
    assert(initRuntimeDispatch() == MR_SUCCESS);
    assert(!runtimeDispatchFailed());

    SDL_SetEventFilter(rejectRuntimeEvent, NULL);
    assert(timerStart(1) == MR_SUCCESS);
    Uint32 start = SDL_GetTicks();
    while (!runtimeDispatchFailed() && SDL_GetTicks() - start < 5000) {
        SDL_Delay(1);
    }
    assert(runtimeDispatchFailed());
    assert(timerCount == 4);
    SDL_SetEventFilter(NULL, NULL);
    stopRuntimeDispatch();
}

int main(void) {
    SDL_SetMainReady();
    assert(SDL_Init(SDL_INIT_TIMER | SDL_INIT_EVENTS) == 0);
    mainThread = SDL_ThreadID();
    assert(initRuntimeDispatch() == MR_SUCCESS);
    testTimerDeliveryAndCancellation();
    testNetworkDeliveryAndCancellation();
    testQueueFailure();
    stopRuntimeDispatch();
    SDL_Quit();
    assert(queueNetworkCallback(1, getNetworkGeneration(), 0x80008, MR_FAILED, 0x280000) == MR_FAILED);
    puts("Main-thread dispatch regression tests passed.");
    return 0;
}
