#include "tig/message.h"

#include <stdio.h>
#include <string.h>

#include "tig/button.h"
#include "tig/core.h"
#include "tig/debug.h"
#include "tig/kb.h"
#include "tig/memory.h"
#include "tig/mouse.h"
#include "tig/movie.h"
#include "tig/timer.h"
#include "tig/video.h"
#include "tig/window.h"

// Maximum number of key handler.
#define MAX_KEY_HANDLERS 16

typedef struct TigMessageListNode {
    /* 0000 */ TigMessage message;
    /* 001C */ struct TigMessageListNode* next;
} TigMessageListNode;

typedef struct TigMessageKeyboardHandler {
    /* 0000 */ int key;
    /* 0004 */ TigMessageKeyboardCallback callback;
} TigMessageKeyboardHandler;

static TigMessageListNode* tig_message_node_acquire(void);
static void tig_message_node_reserve(void);
static void tig_message_node_release(TigMessageListNode* node);
static const char* tig_message_text_to_ansi(const char* utf8);
static void tig_message_autotest_ping(void);

// Number of converted text input strings kept alive at once (several text
// input events can be queued in a single ping).
#define TEXT_INPUT_BUFFERS 8
#define TEXT_INPUT_BUFFER_SIZE 64

static char tig_message_text_buffers[TEXT_INPUT_BUFFERS][TEXT_INPUT_BUFFER_SIZE];
static int tig_message_text_buffer_index;

#if SDL_PLATFORM_IOS || SDL_PLATFORM_ANDROID
// Touch controls:
// - tap: left click
// - drag: left button held while moving (inventory drag & drop)
// - long press: right click (held until finger is lifted)
// - two-finger tap: right click
// - two-finger drag: pan the map
// - three-finger tap: Escape (game menu with save/load)
// - four-finger tap: F7 (quick save)

// Hold duration (in milliseconds) that turns a touch into a right click.
#define TOUCH_LONG_PRESS_MS 450

// Distance (in logical pixels) a finger must travel to start a drag.
#define TOUCH_DRAG_THRESHOLD 12.0f

static void tig_message_touch_handle_event(const SDL_Event* event);
static void tig_message_touch_ping(void);

static bool tig_message_touch_active;
static SDL_FingerID tig_message_touch_finger;
static int tig_message_touch_finger_count;
static int tig_message_touch_max_finger_count;
static bool tig_message_touch_panned;
static float tig_message_touch_last_x;
static float tig_message_touch_last_y;
static float tig_message_touch_pan_x;
static float tig_message_touch_pan_y;
static bool tig_message_touch_dragging;
static bool tig_message_touch_long_pressed;
static bool tig_message_touch_consumed;
static Uint64 tig_message_touch_down_ticks;
static float tig_message_touch_down_x;
static float tig_message_touch_down_y;
#endif

// 0x62B1A8
static bool tig_message_key_triggers[MAX_KEY_HANDLERS];

// 0x62B1F0
static TigMessageKeyboardHandler tig_message_key_handlers[MAX_KEY_HANDLERS];

// 0x62B278
static SDL_Mutex* tig_message_mutex;

// 0x62B290
static TigMessageListNode* tig_message_empty_node_head;

// 0x62B294
static TigMessageListNode* tig_message_queue_head;

// 0x62B298
static int tig_message_key_handlers_count;

// 0x52B6D0
int tig_message_init(TigInitInfo* init_info)
{
    (void)init_info;

    tig_message_mutex = SDL_CreateMutex();

#if SDL_PLATFORM_IOS || SDL_PLATFORM_ANDROID
    // Touches are translated into mouse input by
    // `tig_message_touch_handle_event`, disable SDL's own emulation.
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
#endif

    tig_message_empty_node_head = NULL;
    tig_message_queue_head = NULL;
    tig_message_key_handlers_count = 0;

    return TIG_OK;
}

// 0x52B750
void tig_message_exit(void)
{
    TigMessageListNode* next;

    while (tig_message_queue_head != NULL) {
        next = tig_message_queue_head->next;
        FREE(tig_message_queue_head);
        tig_message_queue_head = next;
    }

    while (tig_message_empty_node_head != NULL) {
        next = tig_message_empty_node_head->next;
        FREE(tig_message_empty_node_head);
        tig_message_empty_node_head = next;
    }

    SDL_DestroyMutex(tig_message_mutex);
    tig_message_mutex = NULL;
}

// 0x52B7B0
void tig_message_ping(void)
{
    TigMessage message;
    SDL_Event event;
    SDL_Renderer* renderer;
    int index;

    message.type = TIG_MESSAGE_PING;
    message.timestamp = tig_ping_timestamp;
    tig_message_enqueue(&message);

    tig_message_autotest_ping();

    for (index = 0; index < tig_message_key_handlers_count; index++) {
        if (tig_kb_is_key_pressed(tig_message_key_handlers[index].key)) {
            if (!tig_message_key_triggers[index]) {
                tig_message_key_triggers[index] = true;
                tig_message_key_handlers[index].callback(tig_message_key_handlers[index].key);
            }
        } else {
            tig_message_key_triggers[index] = false;
        }
    }

    if (tig_video_renderer_get(&renderer) != TIG_OK) {
        return;
    }

    while (SDL_PollEvent(&event)) {
        SDL_ConvertEventToRenderCoordinates(renderer, &event);

        switch (event.type) {
        case SDL_EVENT_WINDOW_MOUSE_ENTER:
            tig_set_active(true);
            break;
        case SDL_EVENT_WINDOW_MOUSE_LEAVE:
            tig_set_active(false);
            break;
        case SDL_EVENT_MOUSE_MOTION:
            tig_mouse_set_position((int)event.motion.x, (int)event.motion.y);
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            switch (event.button.button) {
            case SDL_BUTTON_LEFT:
                tig_mouse_set_button(TIG_MOUSE_BUTTON_LEFT, event.button.down);
                break;
            case SDL_BUTTON_RIGHT:
                tig_mouse_set_button(TIG_MOUSE_BUTTON_RIGHT, event.button.down);
                break;
            case SDL_BUTTON_MIDDLE:
                tig_mouse_set_button(TIG_MOUSE_BUTTON_MIDDLE, event.button.down);
                break;
            }
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            tig_mouse_wheel(event.wheel.integer_x, event.wheel.integer_y);
            break;
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
            tig_kb_set_key(event.key.key, event.key.scancode, event.key.down);
            break;
        case SDL_EVENT_TEXT_INPUT:
            message.type = TIG_MESSAGE_TEXT_INPUT;
            message.data.text.text = tig_message_text_to_ansi(event.text.text);
            tig_message_enqueue(&message);
            break;
        case SDL_EVENT_QUIT:
            tig_message_post_quit(0);
            break;
#if SDL_PLATFORM_IOS || SDL_PLATFORM_ANDROID
        case SDL_EVENT_FINGER_DOWN:
        case SDL_EVENT_FINGER_MOTION:
        case SDL_EVENT_FINGER_UP:
        case SDL_EVENT_FINGER_CANCELED:
            tig_message_touch_handle_event(&event);
            break;
#endif
        }
    }

#if SDL_PLATFORM_IOS || SDL_PLATFORM_ANDROID
    tig_message_touch_ping();
#endif
}

/**
 * Converts UTF-8 text input into the game's single-byte encoding.
 *
 * Game fonts use Windows code pages: Cyrillic is mapped to Windows-1251
 * (Russian localizations), U+0080-U+00FF are passed as is (Windows-1252
 * compatible for Western languages). Other characters are dropped.
 */
static const char* tig_message_text_to_ansi(const char* utf8)
{
    char* buffer;
    size_t len = 0;
    size_t remaining;
    Uint32 cp;

    buffer = tig_message_text_buffers[tig_message_text_buffer_index];
    tig_message_text_buffer_index = (tig_message_text_buffer_index + 1) % TEXT_INPUT_BUFFERS;

    remaining = SDL_strlen(utf8);
    while (remaining > 0 && len < TEXT_INPUT_BUFFER_SIZE - 1) {
        cp = SDL_StepUTF8(&utf8, &remaining);
        if (cp == 0) {
            break;
        }

        if (cp < 0x80) {
            buffer[len++] = (char)cp;
        } else if (cp >= 0x410 && cp <= 0x44F) {
            // А-я
            buffer[len++] = (char)(cp - 0x410 + 0xC0);
        } else if (cp == 0x401) {
            // Ё
            buffer[len++] = (char)0xA8;
        } else if (cp == 0x451) {
            // ё
            buffer[len++] = (char)0xB8;
        } else if (cp >= 0xA0 && cp <= 0xFF) {
            buffer[len++] = (char)cp;
        }
    }

    buffer[len] = '\0';

    return buffer;
}

#if SDL_PLATFORM_IOS || SDL_PLATFORM_ANDROID
static void tig_message_touch_press_key(SDL_Keycode key, SDL_Scancode scancode)
{
    tig_kb_set_key(key, scancode, true);
    tig_kb_set_key(key, scancode, false);
}

static void tig_message_touch_handle_event(const SDL_Event* event)
{
    int x = (int)event->tfinger.x;
    int y = (int)event->tfinger.y;
    float dx;
    float dy;

    switch (event->type) {
    case SDL_EVENT_FINGER_DOWN:
        tig_message_touch_finger_count++;

        if (!tig_message_touch_active && tig_message_touch_finger_count == 1) {
            tig_message_touch_active = true;
            tig_message_touch_finger = event->tfinger.fingerID;
            tig_message_touch_max_finger_count = 1;
            tig_message_touch_panned = false;
            tig_message_touch_last_x = event->tfinger.x;
            tig_message_touch_last_y = event->tfinger.y;
            tig_message_touch_dragging = false;
            tig_message_touch_long_pressed = false;
            tig_message_touch_consumed = false;
            tig_message_touch_down_ticks = SDL_GetTicks();
            tig_message_touch_down_x = event->tfinger.x;
            tig_message_touch_down_y = event->tfinger.y;
            tig_set_active(true);
            tig_mouse_set_position(x, y);
        } else if (tig_message_touch_active) {
            if (tig_message_touch_finger_count > tig_message_touch_max_finger_count) {
                tig_message_touch_max_finger_count = tig_message_touch_finger_count;
            }

            // Multi-finger gesture, resolved when all fingers are lifted.
            if (!tig_message_touch_dragging && !tig_message_touch_long_pressed) {
                tig_message_touch_consumed = true;
            }
        }
        break;
    case SDL_EVENT_FINGER_MOTION:
        if (!tig_message_touch_active || event->tfinger.fingerID != tig_message_touch_finger) {
            break;
        }

        // Two-finger drag pans the map following the primary finger.
        if (tig_message_touch_consumed && tig_message_touch_finger_count == 2) {
            dx = event->tfinger.x - tig_message_touch_last_x;
            dy = event->tfinger.y - tig_message_touch_last_y;
            tig_message_touch_last_x = event->tfinger.x;
            tig_message_touch_last_y = event->tfinger.y;

            if (!tig_message_touch_panned) {
                dx = event->tfinger.x - tig_message_touch_down_x;
                dy = event->tfinger.y - tig_message_touch_down_y;
                if (dx * dx + dy * dy < TOUCH_DRAG_THRESHOLD * TOUCH_DRAG_THRESHOLD) {
                    break;
                }
                tig_message_touch_panned = true;
            }

            tig_message_touch_pan_x += dx;
            tig_message_touch_pan_y += dy;
            break;
        }

        tig_message_touch_last_x = event->tfinger.x;
        tig_message_touch_last_y = event->tfinger.y;

        if (!tig_message_touch_dragging
            && !tig_message_touch_long_pressed
            && !tig_message_touch_consumed) {
            dx = event->tfinger.x - tig_message_touch_down_x;
            dy = event->tfinger.y - tig_message_touch_down_y;
            if (dx * dx + dy * dy < TOUCH_DRAG_THRESHOLD * TOUCH_DRAG_THRESHOLD) {
                break;
            }

            // Press at the original location so that drag starts from the
            // touched object.
            tig_message_touch_dragging = true;
            tig_mouse_set_button(TIG_MOUSE_BUTTON_LEFT, true);
        }

        if (!tig_message_touch_consumed) {
            tig_mouse_set_position(x, y);
        }
        break;
    case SDL_EVENT_FINGER_UP:
    case SDL_EVENT_FINGER_CANCELED:
        if (tig_message_touch_finger_count > 0) {
            tig_message_touch_finger_count--;
        }

        if (!tig_message_touch_active) {
            break;
        }

        if (event->tfinger.fingerID == tig_message_touch_finger) {
            // Primary finger lifted - finish its own action.
            if (tig_message_touch_dragging) {
                tig_mouse_set_button(TIG_MOUSE_BUTTON_LEFT, false);
                tig_message_touch_dragging = false;
            } else if (tig_message_touch_long_pressed) {
                tig_mouse_set_button(TIG_MOUSE_BUTTON_RIGHT, false);
                tig_message_touch_long_pressed = false;
            } else if (!tig_message_touch_consumed && event->type == SDL_EVENT_FINGER_UP) {
                tig_mouse_set_button(TIG_MOUSE_BUTTON_LEFT, true);
                tig_mouse_set_button(TIG_MOUSE_BUTTON_LEFT, false);
            }
        }

        if (tig_message_touch_finger_count > 0) {
            break;
        }

        // All fingers lifted.
        if (tig_message_touch_consumed && !tig_message_touch_panned && event->type == SDL_EVENT_FINGER_UP) {
            switch (tig_message_touch_max_finger_count) {
            case 2:
                tig_mouse_set_button(TIG_MOUSE_BUTTON_RIGHT, true);
                tig_mouse_set_button(TIG_MOUSE_BUTTON_RIGHT, false);
                break;
            case 3:
                tig_message_touch_press_key(SDLK_ESCAPE, SDL_SCANCODE_ESCAPE);
                break;
            case 4:
                tig_message_touch_press_key(SDLK_F7, SDL_SCANCODE_F7);
                break;
            }
        }

        tig_message_touch_active = false;
        break;
    }
}

void tig_message_take_pan(int* dx, int* dy)
{
    *dx = (int)tig_message_touch_pan_x;
    *dy = (int)tig_message_touch_pan_y;

    // Keep fractional remainder for smooth panning.
    tig_message_touch_pan_x -= (float)*dx;
    tig_message_touch_pan_y -= (float)*dy;
}

static void tig_message_touch_ping(void)
{
    if (!tig_message_touch_active
        || tig_message_touch_dragging
        || tig_message_touch_long_pressed
        || tig_message_touch_consumed) {
        return;
    }

    if (SDL_GetTicks() - tig_message_touch_down_ticks >= TOUCH_LONG_PRESS_MS) {
        tig_message_touch_long_pressed = true;
        tig_mouse_set_button(TIG_MOUSE_BUTTON_RIGHT, true);
    }
}
#endif

// 0x52B890
int tig_message_enqueue(TigMessage* message)
{
    TigMessageListNode* node;
    TigMessageListNode* prev;

    SDL_LockMutex(tig_message_mutex);

    node = tig_message_node_acquire();
    node->message = *message;
    node->next = NULL;

    if (message->type == TIG_MESSAGE_REDRAW || tig_message_queue_head == NULL) {
        // Redraw is always placed on top of the message queue.
        node->next = tig_message_queue_head;
        tig_message_queue_head = node;
    } else {
        // Append new node to the end of the message queue.
        prev = tig_message_queue_head;
        while (prev->next != NULL) {
            prev = prev->next;
        }

        prev->next = node;
    }

    SDL_UnlockMutex(tig_message_mutex);

    return TIG_OK;
}

// 0x52B920
int tig_message_set_key_handler(TigMessageKeyboardCallback callback, int key)
{
    int index;

    if (key < 0) {
        // Bad key code.
        return TIG_ERR_INVALID_PARAM;
    }

    if (callback == NULL) {
        // Find handler to remove.
        for (index = 0; index < MAX_KEY_HANDLERS; index++) {
            if (tig_message_key_handlers[index].key == key) {
                break;
            }
        }

        if (index >= MAX_KEY_HANDLERS) {
            // Not found.
            return TIG_ERR_INVALID_PARAM;
        }

        // Reorder subsequent slots.
        while (index + 1 < tig_message_key_handlers_count) {
            tig_message_key_triggers[index] = tig_message_key_triggers[index + 1];
            tig_message_key_handlers[index] = tig_message_key_handlers[index + 1];
            index++;
        }

        tig_message_key_handlers_count--;

        return TIG_OK;
    }

    if (tig_message_key_handlers_count >= MAX_KEY_HANDLERS) {
        return TIG_ERR_OUT_OF_HANDLES;
    }

    tig_message_key_handlers[tig_message_key_handlers_count].key = key;
    tig_message_key_handlers[tig_message_key_handlers_count].callback = callback;
    tig_message_key_triggers[tig_message_key_handlers_count] = false;
    tig_message_key_handlers_count++;

    return TIG_OK;
}

// 0x52BDA0
int tig_message_dequeue(TigMessage* message)
{
    TigMessageListNode* next;
    TigMessage temp_message;

    if (tig_message_queue_head == NULL) {
        return TIG_ERR_MESSAGE_QUEUE_EMPTY;
    }

    SDL_LockMutex(tig_message_mutex);

    while (tig_message_queue_head != NULL) {
        next = tig_message_queue_head->next;
        temp_message = tig_message_queue_head->message;
        tig_message_node_release(tig_message_queue_head);
        tig_message_queue_head = next;

        if (!tig_movie_is_playing()) {
            if (temp_message.type == TIG_MESSAGE_MOUSE
                && tig_button_process_mouse_msg(&(temp_message.data.mouse))) {
                continue;
            }

            if (tig_window_filter_message(&temp_message)
                && temp_message.type != TIG_MESSAGE_REDRAW) {
                continue;
            }
        }

        *message = temp_message;

        SDL_UnlockMutex(tig_message_mutex);

        return TIG_OK;
    }

    SDL_UnlockMutex(tig_message_mutex);

    return TIG_ERR_MESSAGE_QUEUE_EMPTY;
}

// 0x52BE60
int tig_message_post_quit(int exit_code)
{
    TigMessage message;
    tig_timer_now(&(message.timestamp));
    message.type = TIG_MESSAGE_QUIT;
    message.data.quit.exit_code = exit_code;
    return tig_message_enqueue(&message);
}

// 0x52BED0
TigMessageListNode* tig_message_node_acquire(void)
{
    TigMessageListNode* node;

    tig_message_node_reserve();

    node = tig_message_empty_node_head;
    tig_message_empty_node_head = node->next;
    node->next = NULL;

    return node;
}

// 0x52BF10
void tig_message_node_reserve(void)
{
    int index;
    TigMessageListNode* node;

    if (tig_message_empty_node_head == NULL) {
        for (index = 0; index < 32; index++) {
            node = (TigMessageListNode*)MALLOC(sizeof(*node));
            node->next = tig_message_empty_node_head;
            tig_message_empty_node_head = node;
        }
    }
}

// 0x52BF60
void tig_message_node_release(TigMessageListNode* node)
{
    node->next = tig_message_empty_node_head;
    tig_message_empty_node_head = node;
}

#if !(SDL_PLATFORM_IOS || SDL_PLATFORM_ANDROID)
void tig_message_take_pan(int* dx, int* dy)
{
    *dx = 0;
    *dy = 0;
}
#endif

/**
 * Debug autopilot: when `ARCANUM_AUTOTEST` environment variable points to a
 * script file, executes one command per line:
 *
 *   wait <ms>        - pause before the next command
 *   move <x> <y>     - move cursor (logical screen coordinates)
 *   click <x> <y>    - left click
 *   rclick <x> <y>   - right click
 *   key <name>       - press and release a key (SDL scancode name, e.g. Escape)
 *   text <string>    - text input
 *   shot             - save screenshot (screenNNNN.bmp)
 *   log <message>    - write message to the debug log
 *   quit             - exit the game
 */
static void tig_message_autotest_ping(void)
{
    static bool initialized;
    static FILE* stream;
    static Uint64 next_ticks;
    char line[256];
    char arg[256];
    int x;
    int y;
    size_t len;
    SDL_Scancode scancode;
    TigMessage message;
    const char* path;

    if (!initialized) {
        initialized = true;
        path = SDL_getenv("ARCANUM_AUTOTEST");
        if (path != NULL) {
            stream = fopen(path, "r");
        }
    }

    if (stream == NULL) {
        return;
    }

    // The game pauses when its window is not focused, keep it running.
    tig_set_active(true);

    if (SDL_GetTicks() < next_ticks) {
        return;
    }

    while (fgets(line, sizeof(line), stream) != NULL) {
        len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }

        if (line[0] == '\0' || line[0] == '#') {
            continue;
        }

        tig_debug_printf("AUTOTEST: %s\n", line);

        if (sscanf(line, "wait %d", &x) == 1) {
            next_ticks = SDL_GetTicks() + (Uint64)x;
            return;
        } else if (sscanf(line, "move %d %d", &x, &y) == 2) {
            tig_mouse_set_position(x, y);
        } else if (sscanf(line, "click %d %d", &x, &y) == 2) {
            tig_mouse_set_position(x, y);
            tig_mouse_set_button(TIG_MOUSE_BUTTON_LEFT, true);
            tig_mouse_set_button(TIG_MOUSE_BUTTON_LEFT, false);
        } else if (sscanf(line, "rclick %d %d", &x, &y) == 2) {
            tig_mouse_set_position(x, y);
            tig_mouse_set_button(TIG_MOUSE_BUTTON_RIGHT, true);
            tig_mouse_set_button(TIG_MOUSE_BUTTON_RIGHT, false);
        } else if (sscanf(line, "key %255s", arg) == 1) {
            scancode = SDL_GetScancodeFromName(arg);
            tig_kb_set_key(SDL_GetKeyFromScancode(scancode, SDL_KMOD_NONE, false), scancode, true);
            tig_kb_set_key(SDL_GetKeyFromScancode(scancode, SDL_KMOD_NONE, false), scancode, false);
        } else if (strncmp(line, "text ", 5) == 0) {
            message.type = TIG_MESSAGE_TEXT_INPUT;
            message.timestamp = tig_ping_timestamp;
            message.data.text.text = tig_message_text_to_ansi(line + 5);
            tig_message_enqueue(&message);
        } else if (strcmp(line, "shot") == 0) {
            tig_video_screenshot_make();
        } else if (strncmp(line, "log ", 4) == 0) {
            // Already logged above.
        } else if (strcmp(line, "quit") == 0) {
            tig_message_post_quit(0);
        }

        // Let the game process each input command before the next one.
        next_ticks = SDL_GetTicks() + 50;
        return;
    }

    fclose(stream);
    stream = NULL;
}
