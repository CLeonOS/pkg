#include "pkg_internal.h"

int pkg_has_prefix(const char *text, const char *prefix) {
    u64 i = 0ULL;

    if (text == (const char *)0 || prefix == (const char *)0) {
        return 0;
    }

    while (prefix[i] != '\0') {
        if (text[i] != prefix[i]) {
            return 0;
        }
        i++;
    }

    return 1;
}

int pkg_has_suffix(const char *text, const char *suffix) {
    u64 text_len;
    u64 suffix_len;

    if (text == (const char *)0 || suffix == (const char *)0) {
        return 0;
    }

    text_len = ush_strlen(text);
    suffix_len = ush_strlen(suffix);
    if (suffix_len > text_len) {
        return 0;
    }

    return (strcmp(text + (text_len - suffix_len), suffix) == 0) ? 1 : 0;
}

int pkg_is_url(const char *text) {
    return (pkg_has_prefix(text, "http://") != 0 || pkg_has_prefix(text, "https://") != 0) ? 1 : 0;
}

int pkg_safe_name(const char *name) {
    u64 i;

    if (name == (const char *)0 || name[0] == '\0') {
        return 0;
    }

    for (i = 0ULL; name[i] != '\0'; i++) {
        char ch = name[i];
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_' ||
              ch == '-' || ch == '.')) {
            return 0;
        }
    }

    return (i > 0ULL && i < (u64)PKG_NAME_MAX) ? 1 : 0;
}

int pkg_safe_version_text(const char *version) {
    u64 i;

    if (version == (const char *)0 || version[0] == '\0') {
        return 0;
    }

    for (i = 0ULL; version[i] != '\0'; i++) {
        char ch = version[i];
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_' ||
              ch == '-' || ch == '.' || ch == ':' || ch == '+' || ch == '~')) {
            return 0;
        }
    }

    return (i > 0ULL && i < (u64)PKG_VERSION_MAX) ? 1 : 0;
}

char *pkg_trim_mut(char *text) {
    char *end;

    if (text == (char *)0) {
        return (char *)0;
    }

    while (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n') {
        text++;
    }

    end = text + ush_strlen(text);
    while (end > text && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n')) {
        end--;
    }
    *end = '\0';
    return text;
}

void pkg_copy_trimmed(char *dst, u64 dst_size, const char *src) {
    char tmp[PKG_URL_MAX];
    char *trimmed;

    if (dst == (char *)0 || dst_size == 0ULL) {
        return;
    }

    dst[0] = '\0';
    if (src == (const char *)0) {
        return;
    }

    ush_copy(tmp, (u64)sizeof(tmp), src);
    trimmed = pkg_trim_mut(tmp);
    ush_copy(dst, dst_size, trimmed);
}

int pkg_append_text(char *dst, u64 dst_size, u64 *io_len, const char *text) {
    u64 i;

    if (dst == (char *)0 || io_len == (u64 *)0 || text == (const char *)0) {
        return 0;
    }

    for (i = 0ULL; text[i] != '\0'; i++) {
        if (*io_len + 1ULL >= dst_size) {
            return 0;
        }
        dst[*io_len] = text[i];
        (*io_len)++;
    }
    dst[*io_len] = '\0';
    return 1;
}

int pkg_append_char(char *dst, u64 dst_size, u64 *io_len, char ch) {
    if (dst == (char *)0 || io_len == (u64 *)0 || *io_len + 1ULL >= dst_size) {
        return 0;
    }

    dst[*io_len] = ch;
    (*io_len)++;
    dst[*io_len] = '\0';
    return 1;
}

int pkg_is_url_unreserved(char ch) {
    return ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '-' ||
            ch == '_' || ch == '.' || ch == '~')
               ? 1
               : 0;
}

char pkg_hex_digit(u64 value) {
    value &= 0xFULL;
    if (value < 10ULL) {
        return (char)('0' + value);
    }
    return (char)('A' + (value - 10ULL));
}

int pkg_hex_char_value(char ch) {
    if (ch >= '0' && ch <= '9') {
        return (int)(ch - '0');
    }
    if (ch >= 'a' && ch <= 'f') {
        return (int)(ch - 'a' + 10);
    }
    if (ch >= 'A' && ch <= 'F') {
        return (int)(ch - 'A' + 10);
    }
    return -1;
}

int pkg_hex_digest_is_valid(const char *text) {
    u64 i;

    if (text == (const char *)0) {
        return 0;
    }

    for (i = 0ULL; text[i] != '\0'; i++) {
        if (i >= 64ULL || pkg_hex_char_value(text[i]) < 0) {
            return 0;
        }
    }
    return (i == 64ULL) ? 1 : 0;
}

int pkg_hex_digest_equals(const char *left, const char *right) {
    u64 i;

    if (pkg_hex_digest_is_valid(left) == 0 || pkg_hex_digest_is_valid(right) == 0) {
        return 0;
    }

    for (i = 0ULL; i < 64ULL; i++) {
        if (pkg_hex_char_value(left[i]) != pkg_hex_char_value(right[i])) {
            return 0;
        }
    }
    return 1;
}

int pkg_append_url_encoded(char *dst, u64 dst_size, u64 *io_len, const char *text) {
    u64 i;

    if (dst == (char *)0 || io_len == (u64 *)0 || text == (const char *)0) {
        return 0;
    }

    for (i = 0ULL; text[i] != '\0'; i++) {
        pkg_u8 ch = (pkg_u8)text[i];
        if (pkg_is_url_unreserved((char)ch) != 0) {
            if (pkg_append_char(dst, dst_size, io_len, (char)ch) == 0) {
                return 0;
            }
        } else {
            if (*io_len + 3ULL >= dst_size) {
                return 0;
            }
            dst[*io_len] = '%';
            (*io_len)++;
            dst[*io_len] = pkg_hex_digit(((u64)ch) >> 4U);
            (*io_len)++;
            dst[*io_len] = pkg_hex_digit((u64)ch);
            (*io_len)++;
            dst[*io_len] = '\0';
        }
    }

    return 1;
}
