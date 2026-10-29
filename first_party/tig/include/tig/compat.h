#ifndef TIG_COMPAT_H_
#define TIG_COMPAT_H_

#include "tig/types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define COMPAT_MAX_DRIVE 3
#define COMPAT_MAX_DIR 256
#define COMPAT_MAX_FNAME 256
#define COMPAT_MAX_EXT 256

#ifdef _WIN32
#define PATH_SEPARATOR '\\'
#define PATH_SEPARATOR_STR "\\"
#else
#define PATH_SEPARATOR '/'
#define PATH_SEPARATOR_STR "/"
#endif

void compat_windows_path_to_native(char* path);
void compat_resolve_path(char* path);

// CE: SDL filesystem functions on iOS resolve relative paths against the app's
// preferences folder rather than the current directory. Returns `path` made
// absolute (relative to the current directory) on iOS, `path` as is elsewhere.
#define COMPAT_SDL_PATH(path) compat_sdl_path(path, (char[TIG_MAX_PATH * 2]) { 0 }, TIG_MAX_PATH * 2)
const char* compat_sdl_path(const char* path, char* buffer, size_t size);

// CE: The game uses Windows-1251 (Russian localizations) in file names (e.g.
// save game descriptions), while Apple file systems only accept UTF-8. Returns
// `path` converted to UTF-8 when it is not valid UTF-8 already.
#define COMPAT_OS_PATH(path) compat_os_path(path, (char[TIG_MAX_PATH * 2]) { 0 }, TIG_MAX_PATH * 2)
const char* compat_os_path(const char* path, char* buffer, size_t size);

// Converts UTF-8 file name read from the file system back into Windows-1251 if
// it consists of ASCII and Cyrillic characters only, otherwise copies it as is.
void compat_os_name_to_game(const char* name, char* buffer, size_t size);

// Case-insensitive `fpattern_matchn` (the library matches case-sensitively
// outside of Windows). Handles ASCII and Windows-1251 Cyrillic letters.
bool compat_fpattern_matchn(const char* pattern, const char* name);
void compat_append_path(char* path, size_t size, const char* comp);
void compat_join_path_ex(char* path, size_t size, ...);
#define compat_join_path(path, size, ...) compat_join_path_ex(path, size, __VA_ARGS__, NULL)
void compat_splitpath(const char* path, char* drive, char* dir, char* fname, char* ext);
void compat_makepath(char* path, const char* drive, const char* dir, const char* fname, const char* ext);

#ifdef __cplusplus
}
#endif

#endif /* TIG_COMPAT_H_ */
