#include "tig/find_file.h"

#include <stdio.h>
#include <string.h>

#include "tig/compat.h"
#include "tig/memory.h"

#ifndef _WIN32
#include <dirent.h>

#include <fpattern/fpattern.h>
#endif

typedef struct TigFindFileDataPriv {
    char dir[TIG_MAX_PATH];
    char** entries;
    int count;
    int index;
} TigFindFileDataPriv;

static char** tig_find_file_glob(const char* dir, const char* pattern, int* count);
static void tig_find_file_free_entries(char** entries, int count);

bool tig_find_first_file(const char* pattern, TigFindFileData* find_file_data)
{
    char native_pattern[TIG_MAX_PATH];
    char dir[TIG_MAX_PATH];
    char fname[TIG_MAX_PATH];
    char* pch;
    int count;
    char** entries;
    TigFindFileDataPriv* priv;

    find_file_data->name[0] = '\0';
    find_file_data->handle = 0;

    strcpy(native_pattern, pattern);
    compat_windows_path_to_native(native_pattern);

    pch = strrchr(native_pattern, PATH_SEPARATOR);
    if (pch != NULL) {
        strncpy(dir, native_pattern, pch - native_pattern);
        dir[pch - native_pattern] = '\0';

        compat_windows_path_to_native(dir);
        compat_resolve_path(dir);

        strcpy(fname, pch + 1);
    } else {
        dir[0] = '.';
        dir[1] = '\0';

        strcpy(fname, native_pattern);
    }

    entries = tig_find_file_glob(dir, fname, &count);
    if (entries == NULL) {
        return false;
    }

    priv = (TigFindFileDataPriv*)MALLOC(sizeof(*priv));
    strcpy(priv->dir, dir);
    priv->entries = entries;
    priv->count = count;
    priv->index = 0;
    find_file_data->handle = (intptr_t)priv;

    if (!tig_find_next_file(find_file_data)) {
        tig_find_file_free_entries(entries, count);
        FREE(priv);
        find_file_data->handle = 0;
        return false;
    }

    return true;
}

bool tig_find_next_file(TigFindFileData* find_file_data)
{
    TigFindFileDataPriv* priv;
    char path[TIG_MAX_PATH];

    priv = (TigFindFileDataPriv*)find_file_data->handle;
    if (priv == NULL) {
        return false;
    }

    if (priv->index >= priv->count) {
        return false;
    }

    compat_join_path(path, sizeof(path), priv->dir, priv->entries[priv->index]);
    if (!SDL_GetPathInfo(COMPAT_SDL_PATH(path), &(find_file_data->path_info))) {
        return false;
    }

    strcpy(find_file_data->name, priv->entries[priv->index]);
    priv->index++;

    return true;
}

bool tig_find_close(TigFindFileData* find_file_data)
{
    TigFindFileDataPriv* priv;

    priv = (TigFindFileDataPriv*)find_file_data->handle;
    if (priv == NULL) {
        return false;
    }

    tig_find_file_free_entries(priv->entries, priv->count);
    FREE(priv);

    memset(find_file_data, 0, sizeof(*find_file_data));

    return true;
}

#ifdef _WIN32
static char** tig_find_file_glob(const char* dir, const char* pattern, int* count)
{
    char** entries;

    entries = SDL_GlobDirectory(dir, pattern, SDL_GLOB_CASEINSENSITIVE, count);
    if (entries != NULL && *count == 0) {
        SDL_free(entries);
        entries = NULL;
    }

    return entries;
}

static void tig_find_file_free_entries(char** entries, int count)
{
    (void)count;

    SDL_free(entries);
}
#else
// CE: `SDL_GlobDirectory` returns no entries on iOS, read the directory
// directly. Matching is case-insensitive with DOS wildcard semantics, same as
// the rest of the file layer.
static char** tig_find_file_glob(const char* dir, const char* pattern, int* count)
{
    DIR* handle;
    struct dirent* entry;
    char** entries = NULL;
    char** tmp;
    int capacity = 0;

    *count = 0;

    handle = opendir(dir);
    if (handle == NULL) {
        return NULL;
    }

    while ((entry = readdir(handle)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        if (!fpattern_matchn(pattern, entry->d_name)) {
            continue;
        }

        if (*count == capacity) {
            capacity = capacity != 0 ? capacity * 2 : 16;
            tmp = (char**)SDL_realloc(entries, sizeof(*entries) * capacity);
            if (tmp == NULL) {
                break;
            }
            entries = tmp;
        }

        entries[*count] = SDL_strdup(entry->d_name);
        (*count)++;
    }

    closedir(handle);

    if (*count == 0) {
        SDL_free(entries);
        return NULL;
    }

    return entries;
}

static void tig_find_file_free_entries(char** entries, int count)
{
    int index;

    for (index = 0; index < count; index++) {
        SDL_free(entries[index]);
    }

    SDL_free(entries);
}
#endif
