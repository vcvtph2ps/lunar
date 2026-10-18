#include <common/cmdline.h>
#include <common/init.h>
#include <common/log.h>
#include <lib/string.h>
#include <stdbool.h>
#include <stddef.h>

#define CMDLINE_MAX_NAME_LENGTH  64
#define CMDLINE_MAX_VALUE_LENGTH 128

typedef bool (*cmdline_handler_t)(const char* value);

typedef struct {
    const char* name;
    cmdline_handler_t handler;
} cmdline_option_t;

static bool g_stop_at_stage_set = false;
static init_stage_t g_stop_at_stage = INIT_STAGE_BASE_MEM;

static bool cmdline_is_whitespace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static bool cmdline_copy_token(char* dest, size_t dest_size, const char* source, size_t length) {
    if(length + 1 > dest_size) {
        return false;
    }

    for(size_t i = 0; i < length; i++) {
        dest[i] = source[i];
    }
    dest[length] = '\0';
    return true;
}

static bool handle_stop_at_stage(const char* value) {
    init_stage_t stage;
    if(!init_stage_from_str(value, &stage)) {
        LOG_WARN("cmdline: invalid init stage '%s' for stop_at_stage\n", value);
        return false;
    }

    g_stop_at_stage = stage;
    g_stop_at_stage_set = true;
    return true;
}

static const cmdline_option_t g_cmdline_options[] = {
    { .name = "stop_at_stage", .handler = handle_stop_at_stage },
};

bool cmdline_get_stop_at_stage(init_stage_t* stage) {
    if(!g_stop_at_stage_set) {
        return false;
    }

    if(stage != nullptr) {
        *stage = g_stop_at_stage;
    }
    return true;
}

void cmdline_parse(const char* cmdline) {
    if(cmdline == nullptr) {
        return;
    }

    const char* cursor = cmdline;
    while(*cursor != '\0') {
        while(cmdline_is_whitespace(*cursor)) {
            cursor++;
        }
        if(*cursor == '\0') {
            break;
        }

        const char* token = cursor;
        while(*cursor != '\0' && !cmdline_is_whitespace(*cursor)) {
            cursor++;
        }
        size_t token_length = (size_t) (cursor - token);

        const char* equal = nullptr;
        for(const char* c = token; c < token + token_length; c++) {
            if(*c == '=') {
                equal = c;
                break;
            }
        }

        char name[CMDLINE_MAX_NAME_LENGTH];
        char value[CMDLINE_MAX_VALUE_LENGTH];

        if(equal == nullptr) {
            if(cmdline_copy_token(name, sizeof(name), token, token_length)) {
                LOG_WARN("cmdline: ignoring option '%s' without a value\n", name);
            } else {
                LOG_WARN("cmdline: ignoring malformed option\n");
            }
            continue;
        }

        size_t name_length = (size_t) (equal - token);
        size_t value_length = token_length - name_length - 1;

        if(name_length == 0) {
            LOG_WARN("cmdline: ignoring option with an empty name\n");
            continue;
        }

        if(!cmdline_copy_token(name, sizeof(name), token, name_length) || !cmdline_copy_token(value, sizeof(value), equal + 1, value_length)) {
            LOG_WARN("cmdline: ignoring option that is too long\n");
            continue;
        }

        bool handled = false;
        for(size_t i = 0; i < sizeof(g_cmdline_options) / sizeof(g_cmdline_options[0]); i++) {
            if(string_compare(name, g_cmdline_options[i].name) == 0) {
                handled = true;
                (void) g_cmdline_options[i].handler(value);
                break;
            }
        }

        if(!handled) {
            LOG_WARN("cmdline: unknown option '%s'\n", name);
        }
    }
}
