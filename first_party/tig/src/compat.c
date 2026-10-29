#include "tig/compat.h"

#ifdef _WIN32
#include <stdlib.h>
#else
#include <dirent.h>
#include <unistd.h>
#endif

void compat_windows_path_to_native(char* path)
{
#ifdef _WIN32
    (void)path;
#else
    char* pch = path;
    while (*pch != '\0') {
        if (*pch == '\\') {
            *pch = '/';
        }
        pch++;
    }
#endif
}

void compat_resolve_path(char* path)
{
#ifdef _WIN32
    (void)path;
#else
    char* pch = path;

    DIR* dir;
    char name[TIG_MAX_PATH];
    if (pch[0] == '/') {
        dir = opendir("/");
        pch++;
    } else {
        dir = opendir(".");
    }

    while (dir != NULL) {
        char* sep = strchr(pch, '/');
        size_t length;
        if (sep != NULL) {
            length = sep - pch;
        } else {
            length = strlen(pch);
        }

        bool found = false;

        struct dirent* entry = readdir(dir);
        while (entry != NULL) {
            compat_os_name_to_game(entry->d_name, name, sizeof(name));
            if (strlen(name) == length && SDL_strncasecmp(pch, name, length) == 0) {
                strncpy(pch, name, length);
                found = true;
                break;
            }
            entry = readdir(dir);
        }

        closedir(dir);
        dir = NULL;

        if (!found) {
            break;
        }

        if (sep == NULL) {
            break;
        }

        *sep = '\0';
        dir = opendir(COMPAT_OS_PATH(path));
        *sep = '/';

        pch = sep + 1;
    }
#endif
}

void compat_append_path(char* path, size_t size, const char* comp)
{
    size_t path_len = strlen(path);
    size_t comp_len = strlen(comp);

    if (path_len + comp_len + 1 >= size) {
        return;
    }

    if (path_len != 0) {
        path[path_len++] = PATH_SEPARATOR;
        path[path_len] = '\0';
    }

    strncat(path, comp, comp_len);
}

void compat_join_path_ex(char* path, size_t size, ...)
{
    va_list args;
    const char* comp;
    size_t comp_len;
    size_t path_len = 0;

    path[0] = '\0';

    va_start(args, size);

    while ((comp = va_arg(args, const char*)) != NULL) {
        comp_len = strlen(comp);
        if (path_len + comp_len + 1 >= size) {
            break;
        }

        if (path_len != 0) {
            path[path_len++] = PATH_SEPARATOR;
            path[path_len] = '\0';
        }

        strncat(path, comp, comp_len);
        path_len += comp_len;
    }

    va_end(args);
}

void compat_splitpath(const char* path, char* drive, char* dir, char* fname, char* ext)
{
#ifdef _WIN32
    _splitpath(path, drive, dir, fname, ext);
#else
    const char* drive_start = path;
    if (path[0] == '/' && path[1] == '/') {
        path += 2;
        while (*path != '\0' && *path != '/' && *path != '.') {
            path++;
        }
    }

    if (drive != NULL) {
        size_t drive_size = path - drive_start;
        if (drive_size > COMPAT_MAX_DRIVE - 1) {
            drive_size = COMPAT_MAX_DRIVE - 1;
        }
        strncpy(drive, path, drive_size);
        drive[drive_size] = '\0';
    }

    const char* dir_start = path;
    const char* fname_start = path;
    const char* ext_start = NULL;

    const char* end = path;
    while (*end != '\0') {
        if (*end == '/') {
            fname_start = end + 1;
        } else if (*end == '.') {
            ext_start = end;
        }
        end++;
    }

    if (ext_start == NULL) {
        ext_start = end;
    }

    if (dir != NULL) {
        size_t dir_size = fname_start - dir_start;
        if (dir_size > COMPAT_MAX_DIR - 1) {
            dir_size = COMPAT_MAX_DIR - 1;
        }
        strncpy(dir, path, dir_size);
        dir[dir_size] = '\0';
    }

    if (fname != NULL) {
        size_t fname_length = ext_start - fname_start;
        if (fname_length > COMPAT_MAX_FNAME - 1) {
            fname_length = COMPAT_MAX_FNAME - 1;
        }
        strncpy(fname, fname_start, fname_length);
        fname[fname_length] = '\0';
    }

    if (ext != NULL) {
        size_t ext_length = end - ext_start;
        if (ext_length > COMPAT_MAX_EXT - 1) {
            ext_length = COMPAT_MAX_EXT - 1;
        }
        strncpy(ext, ext_start, ext_length);
        ext[ext_length] = '\0';
    }
#endif
}

void compat_makepath(char* path, const char* drive, const char* dir, const char* fname, const char* ext)
{
#ifdef _WIN32
    _makepath(path, drive, dir, fname, ext);
#else
    path[0] = '\0';

    if (drive != NULL) {
        if (*drive != '\0') {
            strcpy(path, drive);
            path = strchr(path, '\0');

            if (path[-1] == '/') {
                path--;
            } else {
                *path = '/';
            }
        }
    }

    if (dir != NULL) {
        if (*dir != '\0') {
            if (*dir != '/' && *path == '/') {
                path++;
            }

            strcpy(path, dir);
            path = strchr(path, '\0');

            if (path[-1] == '/') {
                path--;
            } else {
                *path = '/';
            }
        }
    }

    if (fname != NULL && *fname != '\0') {
        if (*fname != '/' && *path == '/') {
            path++;
        }

        strcpy(path, fname);
        path = strchr(path, '\0');
    } else {
        if (*path == '/') {
            path++;
        }
    }

    if (ext != NULL) {
        if (*ext != '\0') {
            if (*ext != '.') {
                *path++ = '.';
            }

            strcpy(path, ext);
            path = strchr(path, '\0');
        }
    }

    *path = '\0';
#endif
}

const char* compat_sdl_path(const char* path, char* buffer, size_t size)
{
#if SDL_PLATFORM_IOS
    char cwd[TIG_MAX_PATH];
    char encoded[TIG_MAX_PATH * 2];

    path = compat_os_path(path, encoded, sizeof(encoded));

    if (path[0] == '/' || getcwd(cwd, sizeof(cwd)) == NULL) {
        return path;
    }

    if (path[0] == '.' && path[1] == '/') {
        path += 2;
    } else if (path[0] == '.' && path[1] == '\0') {
        path++;
    }

    SDL_snprintf(buffer, size, "%s/%s", cwd, path);
    return buffer;
#else
    return compat_os_path(path, buffer, size);
#endif
}

#ifndef _WIN32
// Windows-1251 to Unicode for 0x80-0xBF (0xC0-0xFF map to U+0410-U+044F).
static const unsigned short compat_cp1251_high[64] = {
    0x0402, 0x0403, 0x201A, 0x0453, 0x201E, 0x2026, 0x2020, 0x2021,
    0x20AC, 0x2030, 0x0409, 0x2039, 0x040A, 0x040C, 0x040B, 0x040F,
    0x0452, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x0000, 0x2122, 0x0459, 0x203A, 0x045A, 0x045C, 0x045B, 0x045F,
    0x00A0, 0x040E, 0x045E, 0x0408, 0x00A4, 0x0490, 0x00A6, 0x00A7,
    0x0401, 0x00A9, 0x0404, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x0407,
    0x00B0, 0x00B1, 0x0406, 0x0456, 0x0491, 0x00B5, 0x00B6, 0x00B7,
    0x0451, 0x2116, 0x0454, 0x00BB, 0x0458, 0x0405, 0x0455, 0x0457,
};

static bool compat_is_valid_utf8(const char* str)
{
    const unsigned char* pch = (const unsigned char*)str;
    int extra;

    while (*pch != '\0') {
        if (*pch < 0x80) {
            pch++;
            continue;
        }

        if ((*pch & 0xE0) == 0xC0) {
            extra = 1;
        } else if ((*pch & 0xF0) == 0xE0) {
            extra = 2;
        } else if ((*pch & 0xF8) == 0xF0) {
            extra = 3;
        } else {
            return false;
        }

        pch++;
        while (extra-- > 0) {
            if ((*pch & 0xC0) != 0x80) {
                return false;
            }
            pch++;
        }
    }

    return true;
}
#endif

const char* compat_os_path(const char* path, char* buffer, size_t size)
{
#ifdef _WIN32
    (void)buffer;
    (void)size;
    return path;
#else
    const unsigned char* pch;
    size_t len = 0;
    unsigned int cp;

    if (compat_is_valid_utf8(path)) {
        return path;
    }

    for (pch = (const unsigned char*)path; *pch != '\0'; pch++) {
        if (*pch < 0x80) {
            cp = *pch;
        } else if (*pch >= 0xC0) {
            cp = 0x0410 + (*pch - 0xC0);
        } else {
            cp = compat_cp1251_high[*pch - 0x80];
            if (cp == 0) {
                cp = '_';
            }
        }

        if (cp < 0x80) {
            if (len + 1 >= size) {
                break;
            }
            buffer[len++] = (char)cp;
        } else if (cp < 0x800) {
            if (len + 2 >= size) {
                break;
            }
            buffer[len++] = (char)(0xC0 | (cp >> 6));
            buffer[len++] = (char)(0x80 | (cp & 0x3F));
        } else {
            if (len + 3 >= size) {
                break;
            }
            buffer[len++] = (char)(0xE0 | (cp >> 12));
            buffer[len++] = (char)(0x80 | ((cp >> 6) & 0x3F));
            buffer[len++] = (char)(0x80 | (cp & 0x3F));
        }
    }

    buffer[len] = '\0';

    return buffer;
#endif
}

void compat_os_name_to_game(const char* name, char* buffer, size_t size)
{
#ifndef _WIN32
    const char* src = name;
    size_t remaining = strlen(name);
    size_t len = 0;
    Uint32 cp;
    bool convertible = false;

    // Check that all non-ASCII characters are Cyrillic.
    while (remaining > 0) {
        cp = SDL_StepUTF8(&src, &remaining);
        if (cp >= 0x80) {
            if ((cp >= 0x0410 && cp <= 0x044F) || cp == 0x0401 || cp == 0x0451) {
                convertible = true;
            } else {
                convertible = false;
                break;
            }
        }
    }

    if (convertible) {
        src = name;
        remaining = strlen(name);
        while (remaining > 0 && len + 1 < size) {
            cp = SDL_StepUTF8(&src, &remaining);
            if (cp == 0x0401) {
                buffer[len++] = (char)0xA8;
            } else if (cp == 0x0451) {
                buffer[len++] = (char)0xB8;
            } else if (cp >= 0x0410) {
                buffer[len++] = (char)(0xC0 + (cp - 0x0410));
            } else {
                buffer[len++] = (char)cp;
            }
        }
        buffer[len] = '\0';
        return;
    }
#endif

    SDL_strlcpy(buffer, name, size);
}
