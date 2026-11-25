#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wincodec.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include <vmrp/capture.h>

#define CAPTURE_FPS 30
#define CAPTURE_PATH_SIZE VMRP_CAPTURE_PATH_SIZE

typedef struct MediaFunctions {
    HMODULE platform;
    HMODULE writer;
    HRESULT (WINAPI *startup)(ULONG, DWORD);
    HRESULT (WINAPI *shutdown)(void);
    HRESULT (WINAPI *createFile)(MF_FILE_ACCESSMODE, MF_FILE_OPENMODE,
                                 MF_FILE_FLAGS, LPCWSTR, IMFByteStream **);
    HRESULT (WINAPI *createWriter)(LPCWSTR, IMFByteStream *, IMFAttributes *,
                                   IMFSinkWriter **);
    HRESULT (WINAPI *createMediaType)(IMFMediaType **);
    HRESULT (WINAPI *createBuffer)(DWORD, IMFMediaBuffer **);
    HRESULT (WINAPI *createSample)(IMFSample **);
} MediaFunctions;

typedef struct Recorder {
    SDL_Thread *thread;
    SDL_mutex *mutex;
    SDL_cond *condition;
    SDL_Surface *frame;
    Uint8 *latest;
    size_t frameBytes;
    wchar_t filePath[CAPTURE_PATH_SIZE];
    char relativePath[CAPTURE_PATH_SIZE];
    char error[256];
    Uint64 startTime;
    Uint64 stopTime;
    Uint64 lastUpdate;
    bool ready;
    bool stop;
    bool finished;
    bool failed;
} Recorder;

static Recorder recorder;
static unsigned int captureSequence;

static bool capturePath(const wchar_t *prefix, const wchar_t *extension,
                        wchar_t *widePath, char *utf8Path, size_t utf8Size) {
    if (!CreateDirectoryW(L"captures", NULL)) {
        DWORD error = GetLastError();
        if (error != ERROR_ALREADY_EXISTS) {
            SDL_SetError("Cannot create captures directory (Windows error %lu).",
                         (unsigned long)error);
            return false;
        }
    }

    DWORD attributes = GetFileAttributesW(L"captures");
    if (attributes == INVALID_FILE_ATTRIBUTES ||
        !(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        SDL_SetError("The captures path is not a directory.");
        return false;
    }

    SYSTEMTIME time;
    GetLocalTime(&time);
    int length = swprintf(widePath, CAPTURE_PATH_SIZE,
                          L"captures\\%ls-%04u%02u%02u-%02u%02u%02u-%03u-%lu-%u.%ls",
                          prefix, time.wYear, time.wMonth, time.wDay,
                          time.wHour, time.wMinute, time.wSecond, time.wMilliseconds,
                          (unsigned long)GetCurrentProcessId(), captureSequence++, extension);
    if (length < 0 || length >= CAPTURE_PATH_SIZE ||
        WideCharToMultiByte(CP_UTF8, 0, widePath, -1, utf8Path, (int)utf8Size,
                            NULL, NULL) == 0) {
        SDL_SetError("Capture filename is too long.");
        return false;
    }
    return true;
}

static bool copyPath(char *destination, size_t size, const char *path) {
    if (!destination || !size || SDL_strlcpy(destination, path, size) >= size) {
        SDL_SetError("Capture filename buffer is too small.");
        return false;
    }
    return true;
}

static void removeIncomplete(const wchar_t *path) {
    if (!DeleteFileW(path)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Cannot remove incomplete capture (Windows error %lu).",
                     (unsigned long)GetLastError());
    }
}

bool capturePhoto(SDL_Window *window, char *path, size_t pathSize) {
    wchar_t filePath[CAPTURE_PATH_SIZE];
    char relativePath[CAPTURE_PATH_SIZE];
    SDL_Surface *surface = NULL;
    IWICImagingFactory *factory = NULL;
    IWICBitmapEncoder *encoder = NULL;
    IWICBitmapFrameEncode *frame = NULL;
    IStream *stream = NULL;
    HGLOBAL memory = NULL;
    void *pixels = NULL;
    HANDLE file = INVALID_HANDLE_VALUE;
    bool created = false;
    bool success = false;
    bool uninitialize = false;
    const char *step = "initialize COM";
    HRESULT result = S_OK;

    if (!window || !path || !pathSize) {
        SDL_SetError("Invalid screenshot destination.");
        return false;
    }
    if (!capturePath(L"photo", L"png", filePath, relativePath, sizeof(relativePath)) ||
        !copyPath(path, pathSize, relativePath))
        return false;

    surface = SDL_GetWindowSurface(window);
    if (!surface) return false;
    surface = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_ARGB8888, 0);
    if (!surface) return false;
    if ((uint64_t)surface->pitch * surface->h > UINT32_MAX) {
        SDL_FreeSurface(surface);
        SDL_SetError("Screenshot dimensions are too large.");
        return false;
    }

    result = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (result == RPC_E_CHANGED_MODE) {
        result = S_OK;
    } else if (SUCCEEDED(result)) {
        uninitialize = true;
    }
    if (FAILED(result)) goto cleanup;

#define PHOTO_STEP(description, expression) do { \
    step = (description); result = (expression); if (FAILED(result)) goto cleanup; \
} while (0)
    PHOTO_STEP("create PNG encoder",
               CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
                                &IID_IWICImagingFactory, (void **)&factory));
    PHOTO_STEP("create PNG stream", CreateStreamOnHGlobal(NULL, TRUE, &stream));
    PHOTO_STEP("create PNG encoder",
               IWICImagingFactory_CreateEncoder(factory, &GUID_ContainerFormatPng,
                                                 NULL, &encoder));
    PHOTO_STEP("initialize PNG encoder",
               IWICBitmapEncoder_Initialize(encoder, stream, WICBitmapEncoderNoCache));
    PHOTO_STEP("create PNG frame",
               IWICBitmapEncoder_CreateNewFrame(encoder, &frame, NULL));
    PHOTO_STEP("initialize PNG frame", IWICBitmapFrameEncode_Initialize(frame, NULL));
    PHOTO_STEP("set PNG size",
               IWICBitmapFrameEncode_SetSize(frame, (UINT)surface->w, (UINT)surface->h));
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    PHOTO_STEP("set PNG pixel format", IWICBitmapFrameEncode_SetPixelFormat(frame, &format));
    if (!IsEqualGUID(&format, &GUID_WICPixelFormat32bppBGRA)) {
        step = "select PNG pixel format";
        result = E_FAIL;
        goto cleanup;
    }
    PHOTO_STEP("write PNG pixels",
               IWICBitmapFrameEncode_WritePixels(frame, (UINT)surface->h,
                                                 (UINT)surface->pitch,
                                                 (UINT)(surface->pitch * surface->h),
                                                 (BYTE *)surface->pixels));
    PHOTO_STEP("finish PNG frame", IWICBitmapFrameEncode_Commit(frame));
    PHOTO_STEP("finish PNG", IWICBitmapEncoder_Commit(encoder));

    STATSTG stat;
    PHOTO_STEP("inspect PNG data", IStream_Stat(stream, &stat, STATFLAG_NONAME));
    if (stat.cbSize.QuadPart == 0 || stat.cbSize.QuadPart > UINT32_MAX) {
        step = "inspect PNG size";
        result = E_FAIL;
        goto cleanup;
    }
    PHOTO_STEP("access PNG data", GetHGlobalFromStream(stream, &memory));
    pixels = GlobalLock(memory);
    if (!pixels) {
        step = "access PNG data";
        result = HRESULT_FROM_WIN32(GetLastError());
        if (SUCCEEDED(result)) result = E_FAIL;
        goto cleanup;
    }
    file = CreateFileW(filePath, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_NEW,
                       FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        step = "create PNG file";
        result = HRESULT_FROM_WIN32(GetLastError());
        goto cleanup;
    }
    created = true;
    DWORD written = 0;
    DWORD size = (DWORD)stat.cbSize.QuadPart;
    if (!WriteFile(file, pixels, size, &written, NULL) || written != size) {
        step = "write PNG file";
        result = HRESULT_FROM_WIN32(GetLastError());
        if (SUCCEEDED(result)) result = E_FAIL;
        goto cleanup;
    }
    if (!CloseHandle(file)) {
        file = INVALID_HANDLE_VALUE;
        step = "close PNG file";
        result = HRESULT_FROM_WIN32(GetLastError());
        goto cleanup;
    }
    file = INVALID_HANDLE_VALUE;
    success = true;

cleanup:
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    if (pixels) GlobalUnlock(memory);
    if (frame) IWICBitmapFrameEncode_Release(frame);
    if (encoder) IWICBitmapEncoder_Release(encoder);
    if (stream) IStream_Release(stream);
    if (factory) IWICImagingFactory_Release(factory);
    if (uninitialize) CoUninitialize();
    if (surface) SDL_FreeSurface(surface);
    if (!success) {
        if (created) removeIncomplete(filePath);
        if (FAILED(result))
            SDL_SetError("Cannot %s (0x%08lx).", step, (unsigned long)(uint32_t)result);
    }
    return success;
#undef PHOTO_STEP
}

static HMODULE loadSystemLibrary(const wchar_t *name) {
    wchar_t path[MAX_PATH];
    UINT length = GetSystemDirectoryW(path, SDL_arraysize(path));
    size_t nameLength = wcslen(name);
    if (!length || length >= SDL_arraysize(path) ||
        length + 1 + nameLength >= SDL_arraysize(path)) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return NULL;
    }
    path[length++] = L'\\';
    memcpy(path + length, name, (nameLength + 1) * sizeof(wchar_t));
    return LoadLibraryExW(path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
}

static bool loadMedia(MediaFunctions *media, char *error, size_t errorSize) {
    media->platform = loadSystemLibrary(L"mfplat.dll");
    if (!media->platform) {
        SDL_snprintf(error, errorSize,
                     "Windows Media Foundation is unavailable (Windows error %lu).",
                     (unsigned long)GetLastError());
        return false;
    }
    media->writer = loadSystemLibrary(L"mfreadwrite.dll");
    if (!media->writer) {
        SDL_snprintf(error, errorSize,
                     "Windows media writer is unavailable (Windows error %lu).",
                     (unsigned long)GetLastError());
        return false;
    }
#define LOAD_MEDIA(member, library, name) do { \
    media->member = (void *)GetProcAddress(media->library, (name)); \
    if (!media->member) { \
        SDL_snprintf(error, errorSize, "Windows media function %s is unavailable.", (name)); \
        return false; \
    } \
} while (0)
    LOAD_MEDIA(startup, platform, "MFStartup");
    LOAD_MEDIA(shutdown, platform, "MFShutdown");
    LOAD_MEDIA(createFile, platform, "MFCreateFile");
    LOAD_MEDIA(createWriter, writer, "MFCreateSinkWriterFromURL");
    LOAD_MEDIA(createMediaType, platform, "MFCreateMediaType");
    LOAD_MEDIA(createBuffer, platform, "MFCreateMemoryBuffer");
    LOAD_MEDIA(createSample, platform, "MFCreateSample");
#undef LOAD_MEDIA
    return true;
}

static HRESULT createVideoWriter(MediaFunctions *media, Recorder *recording,
                                 IMFByteStream **file, IMFSinkWriter **writer,
                                 DWORD *stream, const char **step, bool *created) {
    IMFMediaType *output = NULL;
    IMFMediaType *input = NULL;
    HRESULT result;
    UINT64 size = ((UINT64)(UINT)recording->frame->w << 32) | (UINT)recording->frame->h;
    UINT64 rate = ((UINT64)CAPTURE_FPS << 32) | 1;
    UINT64 squarePixels = ((UINT64)1 << 32) | 1;

#define VIDEO_STEP(description, expression) do { \
    *step = (description); result = (expression); if (FAILED(result)) goto cleanup; \
} while (0)
    VIDEO_STEP("create MP4 file",
               media->createFile(MF_ACCESSMODE_WRITE, MF_OPENMODE_FAIL_IF_EXIST,
                                 MF_FILEFLAGS_NONE, recording->filePath, file));
    *created = true;
    VIDEO_STEP("create MP4 writer",
               media->createWriter(recording->filePath, *file, NULL, writer));
    VIDEO_STEP("create H.264 format", media->createMediaType(&output));
    VIDEO_STEP("set H.264 video type",
               IMFMediaType_SetGUID(output, &MF_MT_MAJOR_TYPE, &MFMediaType_Video));
    VIDEO_STEP("set H.264 codec",
               IMFMediaType_SetGUID(output, &MF_MT_SUBTYPE, &MFVideoFormat_H264));
    VIDEO_STEP("set H.264 bitrate", IMFMediaType_SetUINT32(output, &MF_MT_AVG_BITRATE, 1000000));
    VIDEO_STEP("set H.264 interlace mode",
               IMFMediaType_SetUINT32(output, &MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive));
    VIDEO_STEP("set H.264 size", IMFMediaType_SetUINT64(output, &MF_MT_FRAME_SIZE, size));
    VIDEO_STEP("set H.264 frame rate", IMFMediaType_SetUINT64(output, &MF_MT_FRAME_RATE, rate));
    VIDEO_STEP("set H.264 pixel aspect",
               IMFMediaType_SetUINT64(output, &MF_MT_PIXEL_ASPECT_RATIO, squarePixels));
    VIDEO_STEP("add H.264 stream", IMFSinkWriter_AddStream(*writer, output, stream));
    VIDEO_STEP("create input video format", media->createMediaType(&input));
    VIDEO_STEP("set input video type",
               IMFMediaType_SetGUID(input, &MF_MT_MAJOR_TYPE, &MFMediaType_Video));
    VIDEO_STEP("set input pixel format",
               IMFMediaType_SetGUID(input, &MF_MT_SUBTYPE, &MFVideoFormat_NV12));
    VIDEO_STEP("set input interlace mode",
               IMFMediaType_SetUINT32(input, &MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive));
    VIDEO_STEP("set input size", IMFMediaType_SetUINT64(input, &MF_MT_FRAME_SIZE, size));
    VIDEO_STEP("set input frame rate", IMFMediaType_SetUINT64(input, &MF_MT_FRAME_RATE, rate));
    VIDEO_STEP("set input pixel aspect",
               IMFMediaType_SetUINT64(input, &MF_MT_PIXEL_ASPECT_RATIO, squarePixels));
    VIDEO_STEP("configure video encoder",
               IMFSinkWriter_SetInputMediaType(*writer, *stream, input, NULL));
    VIDEO_STEP("start video encoder", IMFSinkWriter_BeginWriting(*writer));
cleanup:
    if (input) IMFMediaType_Release(input);
    if (output) IMFMediaType_Release(output);
    return result;
#undef VIDEO_STEP
}

static Uint8 clampColor(int value) {
    if (value < 0) return 0;
    if (value > 255) return 255;
    return (Uint8)value;
}

static void convertFrameToNV12(const Uint8 *pixels, int pitch, int width, int height,
                               Uint8 *output) {
    Uint8 *chroma = output + width * height;
    for (int y = 0; y < height; y += 2) {
        for (int x = 0; x < width; x += 2) {
            int red = 0, green = 0, blue = 0;
            for (int dy = 0; dy < 2; dy++) {
                for (int dx = 0; dx < 2; dx++) {
                    const Uint8 *pixel = pixels + (y + dy) * pitch + (x + dx) * 4;
                    int b = pixel[0], g = pixel[1], r = pixel[2];
                    output[(y + dy) * width + x + dx] =
                        clampColor(((66 * r + 129 * g + 25 * b + 128) >> 8) + 16);
                    red += r;
                    green += g;
                    blue += b;
                }
            }
            red /= 4;
            green /= 4;
            blue /= 4;
            chroma[(y / 2) * width + x] =
                clampColor(((-38 * red - 74 * green + 112 * blue + 128) >> 8) + 128);
            chroma[(y / 2) * width + x + 1] =
                clampColor(((112 * red - 94 * green - 18 * blue + 128) >> 8) + 128);
        }
    }
}

static HRESULT writeVideoFrame(MediaFunctions *media, IMFSinkWriter *writer, DWORD stream,
                               const Recorder *recording, const Uint8 *pixels, Uint64 index,
                               const char **step) {
    IMFMediaBuffer *buffer = NULL;
    IMFSample *sample = NULL;
    BYTE *data = NULL;
    int width = recording->frame->w;
    int height = recording->frame->h;
    DWORD size = (DWORD)((uint64_t)width * height * 3 / 2);
    HRESULT result;

#define FRAME_STEP(description, expression) do { \
    *step = (description); result = (expression); if (FAILED(result)) goto cleanup; \
} while (0)
    FRAME_STEP("allocate video frame", media->createBuffer(size, &buffer));
    FRAME_STEP("lock video frame", IMFMediaBuffer_Lock(buffer, &data, NULL, NULL));
    convertFrameToNV12(pixels, recording->frame->pitch, width, height, data);
    FRAME_STEP("unlock video frame", IMFMediaBuffer_Unlock(buffer));
    data = NULL;
    FRAME_STEP("set video frame size", IMFMediaBuffer_SetCurrentLength(buffer, size));
    FRAME_STEP("create video sample", media->createSample(&sample));
    FRAME_STEP("attach video frame", IMFSample_AddBuffer(sample, buffer));
    LONGLONG timestamp = (LONGLONG)(index * 10000000 / CAPTURE_FPS);
    LONGLONG end = (LONGLONG)((index + 1) * 10000000 / CAPTURE_FPS);
    FRAME_STEP("set video timestamp", IMFSample_SetSampleTime(sample, timestamp));
    FRAME_STEP("set video duration", IMFSample_SetSampleDuration(sample, end - timestamp));
    FRAME_STEP("encode video frame", IMFSinkWriter_WriteSample(writer, stream, sample));
cleanup:
    if (data) IMFMediaBuffer_Unlock(buffer);
    if (sample) IMFSample_Release(sample);
    if (buffer) IMFMediaBuffer_Release(buffer);
    return result;
#undef FRAME_STEP
}

static int recordingThread(void *context) {
    Recorder *recording = context;
    MediaFunctions media = {0};
    IMFByteStream *file = NULL;
    IMFSinkWriter *writer = NULL;
    Uint8 *pixels = NULL;
    DWORD stream = 0;
    HRESULT result = S_OK;
    const char *step = "initialize video recording";
    char error[sizeof(recording->error)] = {0};
    bool comInitialized = false;
    bool mediaStarted = false;
    bool created = false;
    bool finalized = false;

    result = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(result)) goto cleanup;
    comInitialized = true;
    if (!loadMedia(&media, error, sizeof(error))) goto cleanup;
    step = "initialize Windows Media Foundation";
    result = media.startup(MF_VERSION, MFSTARTUP_FULL);
    if (FAILED(result)) goto cleanup;
    mediaStarted = true;
    result = createVideoWriter(&media, recording, &file, &writer, &stream, &step, &created);
    if (FAILED(result)) goto cleanup;
    pixels = SDL_malloc(recording->frameBytes);
    if (!pixels) {
        SDL_strlcpy(error, "Cannot allocate a video frame.", sizeof(error));
        goto cleanup;
    }

    SDL_LockMutex(recording->mutex);
    recording->startTime = SDL_GetTicks64();
    recording->ready = true;
    SDL_CondSignal(recording->condition);
    SDL_UnlockMutex(recording->mutex);

    for (Uint64 index = 0;; index++) {
        SDL_LockMutex(recording->mutex);
        Uint64 due = recording->startTime + index * 1000 / CAPTURE_FPS;
        Uint64 now = SDL_GetTicks64();
        while (!recording->stop && now < due) {
            int waitResult = SDL_CondWaitTimeout(recording->condition, recording->mutex,
                                                 (Uint32)(due - now));
            if (waitResult < 0) {
                SDL_snprintf(error, sizeof(error), "Video timer failed: %s", SDL_GetError());
                break;
            }
            now = SDL_GetTicks64();
        }
        bool stop = recording->stop && index > 0 && due >= recording->stopTime;
        if (!stop && !error[0]) memcpy(pixels, recording->latest, recording->frameBytes);
        SDL_UnlockMutex(recording->mutex);
        if (error[0] || stop) break;
        result = writeVideoFrame(&media, writer, stream, recording, pixels, index, &step);
        if (FAILED(result)) goto cleanup;
    }
    if (!error[0]) {
        step = "finalize MP4";
        result = IMFSinkWriter_Finalize(writer);
        if (SUCCEEDED(result)) finalized = true;
    }

cleanup:
    if (FAILED(result) && !error[0])
        SDL_snprintf(error, sizeof(error), "Cannot %s (0x%08lx).",
                     step, (unsigned long)(uint32_t)result);
    if (writer) IMFSinkWriter_Release(writer);
    if (file) IMFByteStream_Release(file);
    if (mediaStarted) {
        HRESULT shutdownResult = media.shutdown();
        if (FAILED(shutdownResult) && !error[0])
            SDL_snprintf(error, sizeof(error), "Cannot shut down media encoder (0x%08lx).",
                         (unsigned long)(uint32_t)shutdownResult);
    }
    if (media.writer) FreeLibrary(media.writer);
    if (media.platform) FreeLibrary(media.platform);
    if (comInitialized) CoUninitialize();
    if (pixels) SDL_free(pixels);
    if (error[0] && created && !finalized) removeIncomplete(recording->filePath);

    SDL_LockMutex(recording->mutex);
    recording->failed = error[0] != '\0';
    SDL_strlcpy(recording->error, error, sizeof(recording->error));
    recording->finished = true;
    recording->ready = true;
    SDL_CondSignal(recording->condition);
    SDL_UnlockMutex(recording->mutex);
    return 0;
}

static void freeRecorder(Recorder *recording) {
    if (recording->frame) SDL_FreeSurface(recording->frame);
    if (recording->latest) SDL_free(recording->latest);
    if (recording->condition) SDL_DestroyCond(recording->condition);
    if (recording->mutex) SDL_DestroyMutex(recording->mutex);
    SDL_zero(*recording);
}

static bool updateFrame(SDL_Window *window, Recorder *recording) {
    SDL_Surface *source = SDL_GetWindowSurface(window);
    if (!source) return false;
    if (source->w <= 0 || source->h <= 0) {
        SDL_SetError("Game frame has invalid dimensions.");
        return false;
    }
    int width = recording->frame->w;
    int height = recording->frame->h;
    SDL_Rect destination;
    if ((int64_t)width * source->h <= (int64_t)height * source->w) {
        destination.w = width;
        destination.h = (int)((int64_t)source->h * width / source->w);
    } else {
        destination.h = height;
        destination.w = (int)((int64_t)source->w * height / source->h);
    }
    if (destination.w < 1) destination.w = 1;
    if (destination.h < 1) destination.h = 1;
    destination.x = (width - destination.w) / 2;
    destination.y = (height - destination.h) / 2;
    if (SDL_FillRect(recording->frame, NULL, 0) < 0 ||
        SDL_BlitScaled(source, NULL, recording->frame, &destination) < 0)
        return false;
    SDL_LockMutex(recording->mutex);
    memcpy(recording->latest, recording->frame->pixels, recording->frameBytes);
    SDL_UnlockMutex(recording->mutex);
    recording->lastUpdate = SDL_GetTicks64();
    return true;
}

bool captureStart(SDL_Window *window, char *path, size_t pathSize) {
    if (!window || !path || !pathSize) {
        SDL_SetError("Invalid recording destination.");
        return false;
    }
    if (recorder.thread) {
        SDL_SetError("A recording is already in progress.");
        return false;
    }
    if (!capturePath(L"video", L"mp4", recorder.filePath, recorder.relativePath,
                     sizeof(recorder.relativePath)) ||
        !copyPath(path, pathSize, recorder.relativePath))
        return false;
    SDL_Surface *source = SDL_GetWindowSurface(window);
    if (!source) return false;
    if (source->w <= 0 || source->h <= 0 || (source->w & 1) || (source->h & 1) ||
        (int64_t)source->w * source->h * 3 / 2 > UINT32_MAX) {
        SDL_SetError("Video dimensions must be positive, even, and supported by the encoder.");
        return false;
    }
    recorder.mutex = SDL_CreateMutex();
    recorder.condition = SDL_CreateCond();
    recorder.frame = SDL_CreateRGBSurfaceWithFormat(0, source->w, source->h, 32,
                                                    SDL_PIXELFORMAT_RGB888);
    if (!recorder.mutex || !recorder.condition || !recorder.frame) goto failure;
    recorder.frameBytes = (size_t)recorder.frame->pitch * recorder.frame->h;
    recorder.latest = SDL_malloc(recorder.frameBytes);
    if (!recorder.latest) {
        SDL_OutOfMemory();
        goto failure;
    }
    if (!updateFrame(window, &recorder)) goto failure;
    recorder.thread = SDL_CreateThread(recordingThread, "video encoder", &recorder);
    if (!recorder.thread) goto failure;

    SDL_LockMutex(recorder.mutex);
    while (!recorder.ready) {
        if (SDL_CondWait(recorder.condition, recorder.mutex) < 0) {
            recorder.stop = true;
            recorder.stopTime = SDL_GetTicks64();
            SDL_CondSignal(recorder.condition);
            SDL_UnlockMutex(recorder.mutex);
            SDL_WaitThread(recorder.thread, NULL);
            goto failure;
        }
    }
    bool failed = recorder.failed;
    char error[sizeof(recorder.error)];
    SDL_strlcpy(error, recorder.error, sizeof(error));
    SDL_UnlockMutex(recorder.mutex);
    if (failed) {
        SDL_WaitThread(recorder.thread, NULL);
        SDL_SetError("%s", error);
        goto failure;
    }
    return true;

failure: {
    char error[256];
    SDL_strlcpy(error, SDL_GetError(), sizeof(error));
    freeRecorder(&recorder);
    SDL_SetError("%s", error);
    return false;
}
}

bool captureIsRecording(void) {
    return recorder.thread != NULL;
}

bool captureStop(char *path, size_t pathSize) {
    if (!recorder.thread) {
        SDL_SetError("No recording is in progress.");
        return false;
    }
    SDL_LockMutex(recorder.mutex);
    recorder.stop = true;
    recorder.stopTime = SDL_GetTicks64();
    SDL_CondSignal(recorder.condition);
    SDL_UnlockMutex(recorder.mutex);
    SDL_WaitThread(recorder.thread, NULL);
    bool success = !recorder.failed;
    char error[sizeof(recorder.error)];
    SDL_strlcpy(error, recorder.error, sizeof(error));
    if (success && path && !copyPath(path, pathSize, recorder.relativePath)) {
        success = false;
        SDL_strlcpy(error, SDL_GetError(), sizeof(error));
    }
    freeRecorder(&recorder);
    if (!success) SDL_SetError("%s", error);
    return success;
}

bool captureUpdate(SDL_Window *window) {
    if (!recorder.thread) return true;
    SDL_LockMutex(recorder.mutex);
    bool finished = recorder.finished;
    SDL_UnlockMutex(recorder.mutex);
    if (finished) return captureStop(NULL, 0);
    if (SDL_GetTicks64() - recorder.lastUpdate < 30) return true;
    if (updateFrame(window, &recorder)) return true;
    char error[256];
    SDL_strlcpy(error, SDL_GetError(), sizeof(error));
    if (!captureStop(NULL, 0))
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Cannot finish recording: %s", SDL_GetError());
    SDL_SetError("Recording stopped because the game frame could not be captured: %s", error);
    return false;
}
