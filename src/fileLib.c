#include "vmrp/fileLib.h"
#include "vmrp/utils.h"

#include <string.h>
#include <errno.h>
#include <SDL2/SDL.h>
#include <dirent.h>
#include <fcntl.h>
#include <malloc.h>
#include <sys/stat.h>

// mrc_open需要返回0表示失败
static struct rb_root filef_map = RB_ROOT;
static uint32_t filef_count = 0;

// mrc_findStart需要返回-1表示失败
static struct rb_root dirf_map = RB_ROOT;
static uint32_t dirf_count = 0;

#if defined(__linux__) && !defined(__EMSCRIPTEN__)
typedef struct {
    struct dirent **entries;
    int count;
    int index;
} DirectoryHandle;

static int compareDirectoryNames(const struct dirent **left, const struct dirent **right) {
    return strcmp((*left)->d_name, (*right)->d_name);
}
#else
typedef DIR DirectoryHandle;
#endif

static int releaseDirectory(DirectoryHandle *directory) {
#if defined(__linux__) && !defined(__EMSCRIPTEN__)
    for (int i = 0; i < directory->count; i++) free(directory->entries[i]);
    free(directory->entries);
    free(directory);
    return 0;
#else
    return closedir(directory);
#endif
}

int32_t my_open(const char *filename, uint32_t mode) {
    int f;
    int new_mode = 0;

    if (mode & MR_FILE_RDONLY) new_mode = O_RDONLY;
    if (mode & MR_FILE_WRONLY) new_mode = O_WRONLY;
    if (mode & MR_FILE_RDWR) new_mode = O_RDWR;
    if (mode & MR_FILE_CREATE) new_mode |= O_CREAT;

#ifdef _WIN32
    new_mode |= O_RAW;
#endif

    f = open((char *)filename, new_mode, S_IRWXU | S_IRWXG | S_IRWXO);
    if (f == -1) {
        return 0;
    }

    filef_count++;
    uIntMap *obj = malloc(sizeof(uIntMap));
    obj->key = filef_count;
    obj->data = (void *)(intptr_t)f;
    uIntMap_insert(&filef_map, obj);
    return filef_count;
}

int32_t my_close(int32_t f) {
    uIntMap *obj = uIntMap_delete(&filef_map, f);
    if (obj == NULL) {
        return MR_FAILED;
    }
    if (f == filef_count) {
        filef_count--;
    }
    int fh = (int)(intptr_t)obj->data;
    free(obj);
    if (close(fh) != 0) {
        return MR_FAILED;
    }
    return MR_SUCCESS;
}

int32_t my_seek(int32_t f, int32_t pos, int method) {
    uIntMap *obj = uIntMap_search(&filef_map, f);
    if (obj == NULL) {
        return MR_FAILED;
    }
    off_t ret = lseek((int)(intptr_t)obj->data, (off_t)pos, method);
    if (ret == -1) {
        return MR_FAILED;
    }
    return MR_SUCCESS;
}

int32_t my_read(int32_t f, void *p, uint32_t l) {
    uIntMap *obj = uIntMap_search(&filef_map, f);
    if (obj == NULL) {
        return MR_FAILED;
    }
    int32_t readnum = read((int)(intptr_t)obj->data, p, (size_t)l);
    if (readnum == -1) {
        return MR_FAILED;
    }
    return readnum;
}

int32_t my_write(int32_t f, void *p, uint32_t l) {
    uIntMap *obj = uIntMap_search(&filef_map, f);
    if (obj == NULL) {
        return MR_FAILED;
    }
    int32_t writenum = write((int)(intptr_t)obj->data, p, (size_t)l);
    if (writenum == -1) {
        return MR_FAILED;
    }
    return writenum;
}

int32_t my_rename(const char *oldname, const char *newname) {
    int ret = rename(oldname, newname);
    if (ret != 0) {
        return MR_FAILED;
    }
    return MR_SUCCESS;
}

int32_t my_remove(const char *filename) {
    int ret = remove(filename);
    if (ret != 0) {
        return MR_FAILED;
    }
    return MR_SUCCESS;
}

int32_t my_getLen(const char *filename) {
    struct stat s1;
    int ret = stat(filename, &s1);
    if (ret != 0)
        return -1;
    return s1.st_size;
}

int32_t my_mkDir(const char *name) {
    int ret;
    if (access(name, F_OK) == 0) {  //检测是否已存在
        goto ok;
    }
#ifndef _WIN32
    ret = mkdir(name, S_IRWXU | S_IRWXG | S_IRWXO);
#else
    ret = mkdir(name);
#endif
    if (ret != 0) {
        return MR_FAILED;
    }
ok:
    return MR_SUCCESS;
}

int32_t my_rmDir(const char *name) {
    int ret = rmdir(name);
    if (ret != 0) {
        return MR_FAILED;
    }
    return MR_SUCCESS;
}

int32_t my_info(const char *filename) {
    struct stat s1;
    int ret = stat(filename, &s1);

    if (ret != 0) {
        return MR_IS_INVALID;
    }
    if (s1.st_mode & S_IFDIR) {
        return MR_IS_DIR;
    } else if (s1.st_mode & S_IFREG) {
        return MR_IS_FILE;
    }
    return MR_IS_INVALID;
}

int32_t my_opendir(const char *name) {
#if defined(__linux__) && !defined(__EMSCRIPTEN__)
    DirectoryHandle *pDir = malloc(sizeof(*pDir));
    if (pDir == NULL) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not allocate directory handle.");
        return MR_FAILED;
    }
    pDir->count = scandir(name, &pDir->entries, NULL, compareDirectoryNames);
    if (pDir->count < 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not enumerate directory '%s': %s", name, strerror(errno));
        free(pDir);
        return MR_FAILED;
    }
    pDir->index = 0;
#else
    DirectoryHandle *pDir = opendir(name);
#endif
    if (pDir != NULL) {
        uIntMap *obj = malloc(sizeof(uIntMap));
        if (obj == NULL) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not allocate directory map entry.");
            if (releaseDirectory(pDir) != 0) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not close directory: %s", strerror(errno));
            }
            return MR_FAILED;
        }
        dirf_count++;
        obj->key = dirf_count;
        obj->data = (void *)pDir;
        uIntMap_insert(&dirf_map, obj);
        return dirf_count;
    }
    return MR_FAILED;
}

char *my_readdir(int32_t f) {
    uIntMap *obj = uIntMap_search(&dirf_map, f);
    if (obj == NULL) {
        return NULL;
    }
#if defined(__linux__) && !defined(__EMSCRIPTEN__)
    DirectoryHandle *directory = obj->data;
    return directory->index < directory->count ? directory->entries[directory->index++]->d_name : NULL;
#else
    struct dirent *pDt = readdir((DIR *)obj->data); // 手册说返回的内存可能是静态分配的，不要尝试free()
    if (pDt != NULL) {
        return pDt->d_name;
    }
    return NULL;
#endif
}

int32_t my_closedir(int32_t f) {
    uIntMap *obj = uIntMap_delete(&dirf_map, f);
    if (obj == NULL) {
        return MR_FAILED;
    }
    if (f == dirf_count) {
        dirf_count--;
    }
    DirectoryHandle *pDir = obj->data;
    free(obj);
    if (releaseDirectory(pDir) != 0) {
        return MR_FAILED;
    }
    return MR_SUCCESS;
}

void writeFile(const char *filename, void *data, uint32 length) {
    int fh = my_open(filename, MR_FILE_CREATE | MR_FILE_RDWR);
    int32_t wLen = 0;
    char *ptr = (char *)data;
    do {
        ptr += wLen;
        wLen = my_write(fh, ptr, length < 1000 ? length : 1000);
        if (wLen == MR_FAILED) {
            break;
        }
        length -= wLen;
    } while (length > 0);
    my_close(fh);
}

char *readFile(const char *filename) {
    int32_t len = my_getLen(filename);
    if (len < 0) {
        return NULL;
    }

    char *p = malloc(len > 0 ? (size_t)len : 1);
    if (p == NULL) {
        return NULL;
    }
    int32_t fh = my_open(filename, MR_FILE_RDONLY);
    if (!fh) {
        free(p);
        return NULL;
    }

    char *ptr = p;
    int32_t remaining = len;
    while (remaining > 0) {
        int32_t readLen = my_read(fh, ptr, (uint32_t)remaining);
        if (readLen <= 0) {
            my_close(fh);
            free(p);
            return NULL;
        }
        ptr += readLen;
        remaining -= readLen;
    }

    my_close(fh);
    return p;
}
