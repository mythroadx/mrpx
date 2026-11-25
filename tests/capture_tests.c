#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wincodec.h>

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "vmrp/capture.h"

static void widePath(const char *path, wchar_t *wide, int count) {
    assert(MultiByteToWideChar(CP_UTF8, 0, path, -1, wide, count) > 0);
}

static void fill(SDL_Window *window, Uint8 topR, Uint8 topG, Uint8 topB,
                 Uint8 bottomR, Uint8 bottomG, Uint8 bottomB) {
    SDL_Surface *surface = SDL_GetWindowSurface(window);
    assert(surface != NULL);
    SDL_Rect top = {0, 0, surface->w, surface->h / 2};
    SDL_Rect bottom = {0, top.h, surface->w, surface->h - top.h};
    assert(SDL_FillRect(surface, &top, SDL_MapRGB(surface->format, topR, topG, topB)) == 0);
    assert(SDL_FillRect(surface, &bottom, SDL_MapRGB(surface->format, bottomR, bottomG, bottomB)) == 0);
    assert(SDL_UpdateWindowSurface(window) == 0);
}

static void checkPhoto(const char *path, UINT width, UINT height,
                       Uint8 topR, Uint8 topG, Uint8 topB,
                       Uint8 bottomR, Uint8 bottomG, Uint8 bottomB) {
    IWICImagingFactory *factory = NULL;
    IWICBitmapDecoder *decoder = NULL;
    IWICBitmapFrameDecode *frame = NULL;
    IWICFormatConverter *converter = NULL;
    wchar_t file[MAX_PATH];
    widePath(path, file, SDL_arraysize(file));
    assert(SUCCEEDED(CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
                                      &IID_IWICImagingFactory, (void **)&factory)));
    assert(SUCCEEDED(IWICImagingFactory_CreateDecoderFromFilename(
        factory, file, NULL, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder)));
    assert(SUCCEEDED(IWICBitmapDecoder_GetFrame(decoder, 0, &frame)));
    UINT actualWidth, actualHeight;
    assert(SUCCEEDED(IWICBitmapFrameDecode_GetSize(frame, &actualWidth, &actualHeight)));
    assert(actualWidth == width && actualHeight == height);
    assert(SUCCEEDED(IWICImagingFactory_CreateFormatConverter(factory, &converter)));
    assert(SUCCEEDED(IWICFormatConverter_Initialize(
        converter, (IWICBitmapSource *)frame, &GUID_WICPixelFormat32bppBGRA,
        WICBitmapDitherTypeNone, NULL, 0, WICBitmapPaletteTypeCustom)));
    size_t size = (size_t)width * height * 4;
    Uint8 *pixels = SDL_malloc(size);
    assert(pixels != NULL);
    assert(SUCCEEDED(IWICFormatConverter_CopyPixels(converter, NULL, width * 4,
                                                    (UINT)size, pixels)));
    const Uint8 *top = pixels + ((height / 4) * width + width / 2) * 4;
    const Uint8 *bottom = pixels + ((height * 3 / 4) * width + width / 2) * 4;
    assert(top[0] == topB && top[1] == topG && top[2] == topR && top[3] == 255);
    assert(bottom[0] == bottomB && bottom[1] == bottomG && bottom[2] == bottomR && bottom[3] == 255);
    SDL_free(pixels);
    IWICFormatConverter_Release(converter);
    IWICBitmapFrameDecode_Release(frame);
    IWICBitmapDecoder_Release(decoder);
    IWICImagingFactory_Release(factory);
}

typedef struct Color { Uint8 b, g, r; } Color;

static Color decodedPixel(const BYTE *data, LONG stride, UINT height, UINT x, UINT y) {
    const BYTE *top = stride < 0 ? data + (height - 1) * (size_t)(-stride) : data;
    const BYTE *pixel = top + (ptrdiff_t)y * stride + x * 4;
    Color color = {pixel[0], pixel[1], pixel[2]};
    return color;
}

static void checkVideo(const char *path, Uint64 elapsedMs) {
    IMFAttributes *attributes = NULL;
    IMFSourceReader *reader = NULL;
    IMFMediaType *desired = NULL, *actual = NULL;
    wchar_t file[MAX_PATH];
    widePath(path, file, SDL_arraysize(file));
    assert(SUCCEEDED(MFCreateAttributes(&attributes, 1)));
    assert(SUCCEEDED(IMFAttributes_SetUINT32(attributes, &MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE)));
    assert(SUCCEEDED(MFCreateSourceReaderFromURL(file, attributes, &reader)));
    assert(SUCCEEDED(MFCreateMediaType(&desired)));
    assert(SUCCEEDED(IMFMediaType_SetGUID(desired, &MF_MT_MAJOR_TYPE, &MFMediaType_Video)));
    assert(SUCCEEDED(IMFMediaType_SetGUID(desired, &MF_MT_SUBTYPE, &MFVideoFormat_RGB32)));
    assert(SUCCEEDED(IMFSourceReader_SetCurrentMediaType(reader, MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                                         NULL, desired)));
    assert(SUCCEEDED(IMFSourceReader_GetCurrentMediaType(reader, MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                                         &actual)));
    UINT64 dimensions;
    assert(SUCCEEDED(IMFMediaType_GetUINT64(actual, &MF_MT_FRAME_SIZE, &dimensions)));
    UINT width = (UINT)(dimensions >> 32), height = (UINT)dimensions;
    assert(width == 240 && height == 320);
    UINT32 rawStride;
    LONG stride;
    if (SUCCEEDED(IMFMediaType_GetUINT32(actual, &MF_MT_DEFAULT_STRIDE, &rawStride))) {
        stride = (LONG)rawStride;
    } else {
        assert(SUCCEEDED(MFGetStrideForBitmapInfoHeader(MFVideoFormat_RGB32.Data1, width, &stride)));
    }

    int count = 0;
    LONGLONG videoEnd = 0;
    Color firstTop = {0}, firstBottom = {0}, stillTop = {0};
    Color lastTop = {0}, lastCenter = {0}, lastBottom = {0};
    for (;;) {
        DWORD flags = 0;
        LONGLONG time = 0;
        IMFSample *sample = NULL;
        assert(SUCCEEDED(IMFSourceReader_ReadSample(reader, MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                                    0, NULL, &flags, &time, &sample)));
        assert(!(flags & MF_SOURCE_READERF_ERROR));
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
            if (sample) IMFSample_Release(sample);
            break;
        }
        if (!sample) continue;
        IMFMediaBuffer *buffer = NULL;
        BYTE *data = NULL;
        DWORD length;
        assert(SUCCEEDED(IMFSample_ConvertToContiguousBuffer(sample, &buffer)));
        assert(SUCCEEDED(IMFMediaBuffer_Lock(buffer, &data, NULL, &length)));
        assert(length >= (DWORD)((size_t)abs(stride) * height));
        Color top = decodedPixel(data, stride, height, width / 2, 20);
        Color center = decodedPixel(data, stride, height, width / 2, height / 2);
        Color bottom = decodedPixel(data, stride, height, width / 2, height - 20);
        if (count == 0) { firstTop = top; firstBottom = bottom; }
        if (count == 10) stillTop = top;
        lastTop = top;
        lastCenter = center;
        lastBottom = bottom;
        LONGLONG duration = 0;
        assert(SUCCEEDED(IMFSample_GetSampleDuration(sample, &duration)));
        videoEnd = time + duration;
        count++;
        IMFMediaBuffer_Unlock(buffer);
        IMFMediaBuffer_Release(buffer);
        IMFSample_Release(sample);
    }
    assert(count >= 30);
    assert(firstTop.r > 180 && firstTop.g < 70 && firstTop.b < 70);
    assert(firstBottom.b > 180 && firstBottom.r < 70 && firstBottom.g < 70);
    assert(stillTop.r > 180 && stillTop.g < 70 && stillTop.b < 70);
    assert(lastTop.r < 40 && lastTop.g < 40 && lastTop.b < 40);
    assert(lastBottom.r < 40 && lastBottom.g < 40 && lastBottom.b < 40);
    assert(lastCenter.g > 180 && lastCenter.r < 70 && lastCenter.b < 70);
    Uint64 durationMs = (Uint64)(videoEnd / 10000);
    assert(durationMs + 100 >= elapsedMs && durationMs <= elapsedMs + 100);
    printf("Decoded %d H.264 frames in %llu ms (wall time %llu ms).\n",
           count, (unsigned long long)durationMs, (unsigned long long)elapsedMs);
    IMFMediaType_Release(actual);
    IMFMediaType_Release(desired);
    IMFSourceReader_Release(reader);
    IMFAttributes_Release(attributes);
}

int main(void) {
    wchar_t original[MAX_PATH], temporary[MAX_PATH], directory[MAX_PATH];
    assert(GetCurrentDirectoryW(SDL_arraysize(original), original) > 0);
    assert(GetTempPathW(SDL_arraysize(temporary), temporary) > 0);
    assert(GetTempFileNameW(temporary, L"vmr", 0, directory) != 0);
    assert(DeleteFileW(directory));
    assert(CreateDirectoryW(directory, NULL));
    assert(SetCurrentDirectoryW(directory));
    assert(SUCCEEDED(CoInitializeEx(NULL, COINIT_MULTITHREADED)));
    assert(SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_FULL)));
    assert(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) == 0);
    SDL_Window *window = SDL_CreateWindow("capture test", 0, 0, 240, 320, SDL_WINDOW_HIDDEN);
    assert(window != NULL);

    char first[VMRP_CAPTURE_PATH_SIZE], rotated[VMRP_CAPTURE_PATH_SIZE];
    char video[VMRP_CAPTURE_PATH_SIZE], completed[VMRP_CAPTURE_PATH_SIZE];
    HANDLE obstruction = CreateFileW(L"captures", GENERIC_WRITE, 0, NULL, CREATE_NEW,
                                     FILE_ATTRIBUTE_NORMAL, NULL);
    assert(obstruction != INVALID_HANDLE_VALUE);
    assert(CloseHandle(obstruction));
    assert(!capturePhoto(window, first, sizeof(first)));
    assert(strstr(SDL_GetError(), "not a directory") != NULL);
    assert(!captureStart(window, video, sizeof(video)) && !captureIsRecording());
    assert(DeleteFileW(L"captures"));

    fill(window, 255, 0, 0, 0, 0, 255);
    assert(capturePhoto(window, first, sizeof(first)));
    checkPhoto(first, 240, 320, 255, 0, 0, 0, 0, 255);
    assert(captureStart(window, video, sizeof(video)) && captureIsRecording());
    Uint64 start = SDL_GetTicks64();
    assert(!captureStart(window, completed, sizeof(completed)));
    SDL_Delay(620);
    SDL_SetWindowSize(window, 320, 240);
    fill(window, 0, 255, 0, 0, 255, 0);
    assert(capturePhoto(window, rotated, sizeof(rotated)));
    assert(strcmp(first, rotated) != 0);
    checkPhoto(rotated, 320, 240, 0, 255, 0, 0, 255, 0);
    checkPhoto(first, 240, 320, 255, 0, 0, 0, 0, 255);
    Uint64 until = SDL_GetTicks64() + 700;
    while (SDL_GetTicks64() < until) {
        assert(captureUpdate(window));
        SDL_Delay(20);
    }
    Uint64 elapsed = SDL_GetTicks64() - start;
    assert(captureStop(completed, sizeof(completed)) && !captureIsRecording());
    assert(strcmp(video, completed) == 0);
    assert(!captureStop(completed, sizeof(completed)));
    checkVideo(video, elapsed);

    wchar_t path[MAX_PATH];
    widePath(first, path, SDL_arraysize(path)); assert(DeleteFileW(path));
    widePath(rotated, path, SDL_arraysize(path)); assert(DeleteFileW(path));
    widePath(video, path, SDL_arraysize(path)); assert(DeleteFileW(path));
    assert(RemoveDirectoryW(L"captures"));
    SDL_DestroyWindow(window);
    SDL_Quit();
    assert(SUCCEEDED(MFShutdown()));
    CoUninitialize();
    assert(SetCurrentDirectoryW(original));
    assert(RemoveDirectoryW(directory));
    puts("Windows capture tests passed");
    return 0;
}
