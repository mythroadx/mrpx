#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <time.h>

#include "vmrp/network.h"
#include "vmrp/posix_sockets.h"
#include "vmrp/dispatch.h"

static pthread_mutex_t gate = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed = PTHREAD_COND_INITIALIZER;
static bool connectionStarted;
static bool allowConnection;
static bool failThreadCreation;
static bool useRealConnect;
static int connectionResult;
static int closeCount;
static int workerCount;
static SOCKET_T lastClosedSocket;
static bool lookupStarted;
static bool allowLookup;
static int callbackCount;
static int32_t callbackResult;
static SDL_threadID mainThread;

static struct timespec deadline(void) {
    struct timespec result;
    assert(clock_gettime(CLOCK_REALTIME, &result) == 0);
    result.tv_sec += 5;
    return result;
}

static int testConnect(SOCKET_T sock, const struct sockaddr *address, SOCKET_LENGTH_T len) {
    if (useRealConnect) {
        return connect(sock, address, len);
    }
    assert(pthread_mutex_lock(&gate) == 0);
    connectionStarted = true;
    assert(pthread_cond_broadcast(&changed) == 0);
    struct timespec until = deadline();
    while (!allowConnection) {
        assert(pthread_cond_timedwait(&changed, &gate, &until) == 0);
    }
    int result = connectionResult;
    assert(pthread_mutex_unlock(&gate) == 0);
    if (result != 0) {
#ifdef WIN_PLAT
        WSASetLastError(WSAECONNREFUSED);
#else
        errno = ECONNREFUSED;
#endif
    }
    return result;
}

static int testCloseSocket(SOCKET_T sock) {
    int result = CLOSE_SOCKET(sock);
    assert(result == 0);
    assert(pthread_mutex_lock(&gate) == 0);
    closeCount++;
    lastClosedSocket = sock;
    assert(pthread_cond_broadcast(&changed) == 0);
    assert(pthread_mutex_unlock(&gate) == 0);
    return result;
}

static int testGetAddrInfo(const char *name, const char *service,
                          const struct addrinfo *hints, struct addrinfo **result) {
    assert(pthread_mutex_lock(&gate) == 0);
    lookupStarted = true;
    assert(pthread_cond_broadcast(&changed) == 0);
    struct timespec until = deadline();
    while (!allowLookup) {
        assert(pthread_cond_timedwait(&changed, &gate, &until) == 0);
    }
    assert(strcmp(name, "localhost") == 0);
    assert(pthread_mutex_unlock(&gate) == 0);
    return getaddrinfo(name, service, hints, result);
}

static void *countedWorker(void *arg);
typedef struct {
    void *(*run)(void *);
    void *arg;
} testWorker;

static int testThreadCreate(pthread_t *thread, const pthread_attr_t *attr,
                            void *(*run)(void *), void *arg) {
    int detachState;
    assert(pthread_attr_getdetachstate(attr, &detachState) == 0);
    assert(detachState == PTHREAD_CREATE_DETACHED);
    if (failThreadCreation) {
        return EAGAIN;
    }
    testWorker *worker = malloc(sizeof(*worker));
    assert(worker != NULL);
    worker->run = run;
    worker->arg = arg;
    assert(pthread_mutex_lock(&gate) == 0);
    workerCount++;
    assert(pthread_mutex_unlock(&gate) == 0);
    int result = pthread_create(thread, attr, countedWorker, worker);
    if (result != 0) {
        assert(pthread_mutex_lock(&gate) == 0);
        workerCount--;
        assert(pthread_mutex_unlock(&gate) == 0);
        free(worker);
    }
    return result;
}

static void *countedWorker(void *arg) {
    testWorker *worker = arg;
    worker->run(worker->arg);
    free(worker);
    assert(pthread_mutex_lock(&gate) == 0);
    workerCount--;
    assert(pthread_cond_broadcast(&changed) == 0);
    assert(pthread_mutex_unlock(&gate) == 0);
    return NULL;
}

// Gate the native call to reproduce close-during-connect without external networking.
#define connect testConnect
#ifdef WIN_PLAT
#define closesocket testCloseSocket
#else
#define close testCloseSocket
#endif
#define pthread_create testThreadCreate
#define getaddrinfo testGetAddrInfo
#include "../src/network.c"
#undef connect
#undef closesocket
#undef close
#undef pthread_create
#undef getaddrinfo

int32_t networkCallback(uint32_t addr, int32_t result, uint32_t userDataAddr) {
    assert(SDL_ThreadID() == mainThread);
    assert(addr == 0x80008);
    assert(userDataAddr == 0x280000);
    callbackCount++;
    callbackResult = result;
    return MR_SUCCESS;
}

int32_t timer(void) {
    assert(!"Unexpected timer callback in network tests");
    return MR_FAILED;
}

static void prepareConnection(int result) {
    assert(pthread_mutex_lock(&gate) == 0);
    assert(workerCount == 0);
    connectionStarted = false;
    allowConnection = false;
    connectionResult = result;
    closeCount = 0;
    lookupStarted = false;
    allowLookup = false;
    assert(pthread_mutex_unlock(&gate) == 0);
}

static void waitForConnection(void) {
    assert(pthread_mutex_lock(&gate) == 0);
    struct timespec until = deadline();
    while (!connectionStarted) {
        assert(pthread_cond_timedwait(&changed, &gate, &until) == 0);
    }
    assert(pthread_mutex_unlock(&gate) == 0);
}

static void finishConnection(void) {
    assert(pthread_mutex_lock(&gate) == 0);
    allowConnection = true;
    allowLookup = true;
    assert(pthread_cond_broadcast(&changed) == 0);
    struct timespec until = deadline();
    while (workerCount != 0) {
        assert(pthread_cond_timedwait(&changed, &gate, &until) == 0);
    }
    assert(pthread_mutex_unlock(&gate) == 0);
}

static void testCloseDuringConnect(void) {
    prepareConnection(0);
    int32_t handle = my_socket(MR_SOCK_STREAM, MR_IPPROTO_TCP);
    assert(handle > 0);
    assert(my_connect(handle, 0x7F000001, 1234, MR_SOCKET_NONBLOCK) == MR_WAITING);
    waitForConnection();
    assert(my_getSocketState(handle) == MR_WAITING);
    assert(my_closeSocket(handle) == MR_SUCCESS);
    assert(my_getSocketState(handle) == MR_FAILED);
    assert(pthread_mutex_lock(&gate) == 0);
    assert(closeCount == 0);
    assert(pthread_mutex_unlock(&gate) == 0);
    finishConnection();
    assert(closeCount == 1);
    assert(my_closeSocket(handle) == MR_FAILED);
}

static void testConnectionResult(int result) {
    prepareConnection(result);
    int32_t handle = my_socket(MR_SOCK_STREAM, MR_IPPROTO_TCP);
    assert(handle > 0);
    assert(my_connect(handle, 0x7F000001, 1234, MR_SOCKET_NONBLOCK) == MR_WAITING);
    waitForConnection();
    finishConnection();
    assert(my_getSocketState(handle) == (result == 0 ? MR_SUCCESS : MR_FAILED));
    assert(closeCount == 0);
    assert(my_closeSocket(handle) == MR_SUCCESS);
    assert(closeCount == 1);
}

static void testThreadCreationFailure(void) {
    prepareConnection(0);
    failThreadCreation = true;
    int32_t handle = my_socket(MR_SOCK_STREAM, MR_IPPROTO_TCP);
    assert(handle > 0);
    assert(my_connect(handle, 0x7F000001, 1234, MR_SOCKET_NONBLOCK) == MR_FAILED);
    failThreadCreation = false;
    assert(my_getSocketState(handle) == MR_FAILED);
    assert(workerCount == 0);
    assert(my_closeSocket(handle) == MR_SUCCESS);
    assert(closeCount == 1);
}

static void testCloseNetworkDuringConnect(void) {
    prepareConnection(0);
    int32_t handle = my_socket(MR_SOCK_STREAM, MR_IPPROTO_TCP);
    assert(handle > 0);
    SOCKET_T nativeSocket = getSocketData(handle)->s;
    assert(my_connect(handle, 0x7F000001, 1234, MR_SOCKET_NONBLOCK) == MR_WAITING);
    waitForConnection();
    assert(my_closeNetwork() == MR_SUCCESS);
    assert(my_getSocketState(handle) == MR_FAILED);
    int type;
    SOCKET_LENGTH_T len = sizeof(type);
    assert(getsockopt(nativeSocket, SOL_SOCKET, SO_TYPE, (char *)&type, &len) == 0);
    finishConnection();
    assert(closeCount == 1);
    assert(lastClosedSocket == nativeSocket);
}

static void testCMWAPState(void) {
    prepareConnection(-1);
    int32_t handle = my_socket(MR_SOCK_STREAM, MR_IPPROTO_TCP);
    assert(handle > 0);
    assert(my_connect(handle, 0x0A0000AC, 80, MR_SOCKET_NONBLOCK) == MR_SUCCESS);
    assert(my_getSocketState(handle) == MR_SUCCESS);
    assert(my_connect(handle, 0x7F000001, 1234, MR_SOCKET_NONBLOCK) == MR_WAITING);
    waitForConnection();
    finishConnection();
    assert(my_getSocketState(handle) == MR_SUCCESS);
    assert(my_send(handle, "test", 4) == MR_FAILED);
    assert(my_closeSocket(handle) == MR_SUCCESS);
    assert(closeCount == 1);
}

static void testLoopbackConnection(void) {
    prepareConnection(0);
    useRealConnect = true;
    SOCKET_T listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    assert(listener != SOCKET_INVALID);
    struct sockaddr_in address = {0};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(bind(listener, (struct sockaddr *)&address, sizeof(address)) == 0);
    SOCKET_LENGTH_T len = sizeof(address);
    assert(getsockname(listener, (struct sockaddr *)&address, &len) == 0);
    assert(listen(listener, 1) == 0);
    int32_t handle = my_socket(MR_SOCK_STREAM, MR_IPPROTO_TCP);
    assert(handle > 0);
    assert(my_connect(handle, 0x7F000001, ntohs(address.sin_port), MR_SOCKET_NONBLOCK) == MR_WAITING);
    finishConnection();
    assert(my_getSocketState(handle) == MR_SUCCESS);
    SOCKET_T peer = accept(listener, NULL, NULL);
    assert(peer != SOCKET_INVALID);
#ifdef WIN_PLAT
    DWORD timeout = 5000;
    assert(setsockopt(peer, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof(timeout)) == 0);
#else
    struct timeval timeout = {.tv_sec = 5};
    assert(setsockopt(peer, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0);
#endif
    assert(my_send(handle, "ping", 4) == 4);
    char buffer[4];
    assert(recv(peer, buffer, sizeof(buffer), MSG_WAITALL) == 4);
    assert(memcmp(buffer, "ping", 4) == 0);
    assert(send(peer, "pong", 4, 0) == 4);
    int received = 0;
    Uint64 until = SDL_GetTicks64() + 5000;
    while (received < 4 && SDL_GetTicks64() < until) {
        int count = my_recv(handle, buffer + received, 4 - received);
        assert(count >= 0);
        received += count;
    }
    assert(received == 4);
    assert(memcmp(buffer, "pong", 4) == 0);
#ifdef __linux__
    assert(shutdown(peer, SHUTDOWN_BIDIRECTIONAL) == 0);
    assert(CLOSE_SOCKET(peer) == 0);
    peer = SOCKET_INVALID;
    int sent;
    until = SDL_GetTicks64() + 5000;
    do {
        sent = my_send(handle, "x", 1);
        if (sent == MR_FAILED) break;
        SDL_Delay(1);
    } while (SDL_GetTicks64() < until);
    assert(sent == MR_FAILED);
#endif
    assert(my_closeSocket(handle) == MR_SUCCESS);
    assert(closeCount == 1);
    if (peer != SOCKET_INVALID) assert(CLOSE_SOCKET(peer) == 0);
    assert(CLOSE_SOCKET(listener) == 0);
    useRealConnect = false;
}

static void pumpCallbacks(void) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        dispatchRuntimeEvent(&event);
    }
    assert(!runtimeDispatchFailed());
}

static void startLookup(void) {
    char hostname[] = "localhost";
    assert(my_getHostByName(NULL, hostname, 0x80008, 0x280000) == MR_WAITING);
    assert(pthread_mutex_lock(&gate) == 0);
    struct timespec until = deadline();
    while (!lookupStarted) {
        assert(pthread_cond_timedwait(&changed, &gate, &until) == 0);
    }
    memset(hostname, 'x', sizeof(hostname) - 1);
    assert(pthread_mutex_unlock(&gate) == 0);
}

static void testDeferredCallbacks(void) {
    prepareConnection(0);
    assert(my_initNetwork(NULL, 0x80008, "cmnet", 0x280000) == MR_WAITING);
    assert(callbackCount == 0);
    pumpCallbacks();
    assert(callbackCount == 1);
    assert(callbackResult == MR_SUCCESS);

    startLookup();
    finishConnection();
    assert(callbackCount == 1);
    pumpCallbacks();
    assert(callbackCount == 2);
    assert(callbackResult == 0x7F000001);

    prepareConnection(0);
    failThreadCreation = true;
    assert(my_getHostByName(NULL, "localhost", 0x80008, 0x280000) == MR_FAILED);
    failThreadCreation = false;
    pumpCallbacks();
    assert(callbackCount == 2);

    prepareConnection(0);
    startLookup();
    assert(my_closeNetwork() == MR_SUCCESS);
    finishConnection();
    pumpCallbacks();
    assert(callbackCount == 2);

    assert(my_initNetwork(NULL, 0, "cmnet", 0) == MR_SUCCESS);
    prepareConnection(0);
    startLookup();
    stopRuntimeDispatch();
    assert(my_closeNetwork() == MR_SUCCESS);
    SDL_Quit();
    finishConnection();
    assert(callbackCount == 2);
}

static void testBoundedCMWAPParsing(void) {
    char source[] = {'G', 'E', 'T', 'X'};
    char line[8];
    my_readLine(source, 3, line, sizeof(line));
    assert(strcmp(line, "GET") == 0);
    char host[8];
    uint16_t port;
    assert(parseHostPort("http://abc:8080/path", host, sizeof(host), &port) == 0);
    assert(strcmp(host, "abc") == 0 && port == 8080);
    assert(parseHostPort("http://abc/path:123", host, sizeof(host), &port) == 0);
    assert(port == 80);
    assert(parseHostPort("http://abcdefgh/", host, sizeof(host), &port) == MR_FAILED);
    assert(parseHostPort("http://abc:123456/", host, sizeof(host), &port) == MR_FAILED);
    assert(parseHostPort("http://abc:65536/", host, sizeof(host), &port) == MR_FAILED);
}

int main(void) {
    SDL_SetMainReady();
    assert(SDL_Init(SDL_INIT_TIMER | SDL_INIT_EVENTS) == 0);
    mainThread = SDL_ThreadID();
    assert(initRuntimeDispatch() == MR_SUCCESS);
    testBoundedCMWAPParsing();
    assert(my_initNetwork(NULL, 0, "cmnet", 0) == MR_SUCCESS);
    for (int i = 0; i < 20; i++) {
        testCloseDuringConnect();
    }
    testConnectionResult(0);
    testConnectionResult(-1);
    testThreadCreationFailure();
    testLoopbackConnection();
    testCloseNetworkDuringConnect();
    assert(my_initNetwork(NULL, 0, "cmwap", 0) == MR_SUCCESS);
    testCMWAPState();
    assert(my_closeNetwork() == MR_SUCCESS);
    testDeferredCallbacks();
    puts("Network regression tests passed.");
    return 0;
}
