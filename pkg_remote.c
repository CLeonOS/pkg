#include "pkg_internal.h"

void pkg_remote_package_init(pkg_remote_package *package) {
    if (package != (pkg_remote_package *)0) {
        ush_zero(package, (u64)sizeof(*package));
    }
}

int pkg_parse_remote_package_object(const char *start, const char *end, pkg_remote_package *out) {
    if (start == (const char *)0 || end == (const char *)0 || out == (pkg_remote_package *)0) {
        return 0;
    }

    pkg_remote_package_init(out);
    if (pkg_json_get_string(start, end, "name", out->name, (u64)sizeof(out->name)) == 0 ||
        pkg_safe_name(out->name) == 0) {
        return 0;
    }

    (void)pkg_json_get_string(start, end, "version", out->version, (u64)sizeof(out->version));
    (void)pkg_json_get_string(start, end, "target", out->target, (u64)sizeof(out->target));
    (void)pkg_json_get_string(start, end, "description", out->description, (u64)sizeof(out->description));
    (void)pkg_json_get_string(start, end, "depends", out->depends, (u64)sizeof(out->depends));
    (void)pkg_json_get_string(start, end, "category", out->category, (u64)sizeof(out->category));
    (void)pkg_json_get_string(start, end, "tags", out->tags, (u64)sizeof(out->tags));
    (void)pkg_json_get_string(start, end, "owner", out->owner, (u64)sizeof(out->owner));
    (void)pkg_json_get_string(start, end, "manifest_url", out->manifest_url, (u64)sizeof(out->manifest_url));
    (void)pkg_json_get_string(start, end, "download_url", out->download_url, (u64)sizeof(out->download_url));
    (void)pkg_json_get_string(start, end, "sha256", out->sha256, (u64)sizeof(out->sha256));
    (void)pkg_json_get_string(start, end, "deprecated", out->deprecated, (u64)sizeof(out->deprecated));
    (void)pkg_json_get_number_text(start, end, "size", out->size, (u64)sizeof(out->size));

    if (out->version[0] == '\0') {
        ush_copy(out->version, (u64)sizeof(out->version), "0.0.0");
    }
    if (out->target[0] == '\0') {
        (void)pkg_default_target(out->name, out->target, (u64)sizeof(out->target));
    }
    if (out->size[0] == '\0') {
        ush_copy(out->size, (u64)sizeof(out->size), "0");
    }

    return 1;
}

const char *pkg_json_find_named_array(const char *text, const char *key) {
    const char *value = pkg_json_find_key_value(text, (const char *)0, key);

    if (value == (const char *)0 || *value != '[') {
        return (const char *)0;
    }
    return value + 1;
}

int pkg_remote_next_package(const char **io_cursor, pkg_remote_package *out) {
    const char *cursor;
    const char *end;

    if (io_cursor == (const char **)0 || *io_cursor == (const char *)0 || out == (pkg_remote_package *)0) {
        return 0;
    }

    cursor = *io_cursor;
    while (*cursor != '\0') {
        if (*cursor == ']') {
            *io_cursor = cursor;
            return 0;
        }
        if (*cursor == '{') {
            end = pkg_json_object_end(cursor);
            if (end == (const char *)0) {
                *io_cursor = cursor;
                return 0;
            }
            *io_cursor = end;
            return pkg_parse_remote_package_object(cursor, end, out);
        }
        cursor++;
    }

    *io_cursor = cursor;
    return 0;
}

int pkg_parse_info_package(const char *text, pkg_remote_package *out) {
    const char *value = pkg_json_find_key_value(text, (const char *)0, "package");
    const char *end;

    if (value == (const char *)0 || *value != '{') {
        return 0;
    }

    end = pkg_json_object_end(value);
    if (end == (const char *)0) {
        return 0;
    }

    return pkg_parse_remote_package_object(value, end, out);
}

void pkg_print_api_error_or_default(const char *text, const char *fallback) {
    char error[128];

    if (pkg_json_get_string(text, (const char *)0, "error", error, (u64)sizeof(error)) != 0 && error[0] != '\0') {
        (void)printf("pkg: %s\n", error);
    } else {
        (void)puts(fallback);
    }
}
