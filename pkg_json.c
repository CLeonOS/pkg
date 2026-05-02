#include "pkg_internal.h"

const char *pkg_skip_ws_const(const char *pos) {
    if (pos == (const char *)0) {
        return (const char *)0;
    }

    while (*pos == ' ' || *pos == '\t' || *pos == '\r' || *pos == '\n') {
        pos++;
    }
    return pos;
}

int pkg_json_make_key_pattern(const char *key, char *out, u64 out_size) {
    int ret;

    if (key == (const char *)0 || out == (char *)0 || out_size == 0ULL) {
        return 0;
    }

    ret = snprintf(out, (usize)out_size, "\"%s\"", key);
    return (ret > 0 && (u64)ret < out_size) ? 1 : 0;
}

const char *pkg_json_find_key_value(const char *start, const char *end, const char *key) {
    char pattern[64];
    const char *pos;

    if (start == (const char *)0 || key == (const char *)0 || pkg_json_make_key_pattern(key, pattern, (u64)sizeof(pattern)) == 0) {
        return (const char *)0;
    }

    pos = start;
    while ((pos = strstr(pos, pattern)) != (const char *)0) {
        const char *value;

        if (end != (const char *)0 && pos >= end) {
            return (const char *)0;
        }

        value = pkg_skip_ws_const(pos + ush_strlen(pattern));
        if (value != (const char *)0 && *value == ':') {
            value = pkg_skip_ws_const(value + 1);
            if (end == (const char *)0 || value < end) {
                return value;
            }
        }
        pos += 1;
    }

    return (const char *)0;
}

const char *pkg_json_object_end(const char *object_start) {
    const char *pos;
    int in_string = 0;
    int escape = 0;
    int depth = 0;

    if (object_start == (const char *)0 || *object_start != '{') {
        return (const char *)0;
    }

    for (pos = object_start; *pos != '\0'; pos++) {
        char ch = *pos;
        if (in_string != 0) {
            if (escape != 0) {
                escape = 0;
            } else if (ch == '\\') {
                escape = 1;
            } else if (ch == '"') {
                in_string = 0;
            }
            continue;
        }

        if (ch == '"') {
            in_string = 1;
        } else if (ch == '{') {
            depth++;
        } else if (ch == '}') {
            depth--;
            if (depth == 0) {
                return pos + 1;
            }
        }
    }

    return (const char *)0;
}

int pkg_json_read_string_value(const char *value, const char *end, char *out, u64 out_size) {
    u64 used = 0ULL;
    const char *pos;

    if (value == (const char *)0 || out == (char *)0 || out_size == 0ULL || *value != '"') {
        return 0;
    }

    out[0] = '\0';
    pos = value + 1;
    while (*pos != '\0' && (end == (const char *)0 || pos < end)) {
        char ch = *pos;
        if (ch == '"') {
            out[used] = '\0';
            return 1;
        }

        if (ch == '\\') {
            pos++;
            if (*pos == '\0' || (end != (const char *)0 && pos >= end)) {
                return 0;
            }
            ch = *pos;
            if (ch == 'n') {
                ch = ' ';
            } else if (ch == 'r') {
                ch = ' ';
            } else if (ch == 't') {
                ch = ' ';
            } else if (ch == 'u') {
                if (pos[1] != '\0' && pos[2] != '\0' && pos[3] != '\0' && pos[4] != '\0') {
                    pos += 4;
                }
                ch = '?';
            }
        }

        if (used + 1ULL >= out_size) {
            out[used] = '\0';
            return 1;
        }
        out[used] = ch;
        used++;
        pos++;
    }

    out[used] = '\0';
    return 0;
}

int pkg_json_get_string(const char *start, const char *end, const char *key, char *out, u64 out_size) {
    const char *value = pkg_json_find_key_value(start, end, key);

    if (out != (char *)0 && out_size > 0ULL) {
        out[0] = '\0';
    }
    return pkg_json_read_string_value(value, end, out, out_size);
}

int pkg_json_get_number_text(const char *start, const char *end, const char *key, char *out, u64 out_size) {
    const char *value = pkg_json_find_key_value(start, end, key);
    u64 used = 0ULL;

    if (out == (char *)0 || out_size == 0ULL) {
        return 0;
    }
    out[0] = '\0';
    if (value == (const char *)0 || (end != (const char *)0 && value >= end)) {
        return 0;
    }

    if (*value == '"') {
        return pkg_json_read_string_value(value, end, out, out_size);
    }

    while (*value != '\0' && (end == (const char *)0 || value < end) &&
           ((*value >= '0' && *value <= '9') || *value == '-')) {
        if (used + 1ULL >= out_size) {
            break;
        }
        out[used] = *value;
        used++;
        value++;
    }
    out[used] = '\0';

    return (used > 0ULL) ? 1 : 0;
}
