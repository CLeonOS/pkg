#include "pkg_client.h"

#include "cmd_runtime.h"

#include <stdio.h>
#include <string.h>

#define PKG_DB_DIR "/system/pkg"
#define PKG_DB_PATH "/system/pkg/installed.db"
#define PKG_REPO_PATH "/system/pkg/repo.conf"
#define PKG_DEFAULT_REPO "http://localhost:8101/pkg/index.php"
#define PKG_TMP_MANIFEST "/temp/p.clpkg"
#define PKG_TMP_ELF "/temp/p.elf"
#define PKG_TMP_API "/temp/pkg_api.json"

#define PKG_ARG_MAX 512U
#define PKG_NAME_MAX 64U
#define PKG_VERSION_MAX 32U
#define PKG_DESC_MAX 128U
#define PKG_SIZE_MAX 32U
#define PKG_URL_MAX 384U
#define PKG_TEXT_MAX 32768U
#define PKG_COPY_CHUNK 4096U
#define PKG_UPGRADE_ALL_MAX 64U

typedef unsigned char pkg_u8;

typedef struct pkg_manifest {
    char name[PKG_NAME_MAX];
    char version[PKG_VERSION_MAX];
    char target[USH_PATH_MAX];
    char url[PKG_URL_MAX];
    char path[USH_PATH_MAX];
    char description[PKG_DESC_MAX];
} pkg_manifest;

typedef struct pkg_remote_package {
    char name[PKG_NAME_MAX];
    char version[PKG_VERSION_MAX];
    char target[USH_PATH_MAX];
    char description[PKG_DESC_MAX];
    char size[PKG_SIZE_MAX];
    char owner[PKG_NAME_MAX];
    char manifest_url[PKG_URL_MAX];
    char download_url[PKG_URL_MAX];
} pkg_remote_package;

static char pkg_text_buf[PKG_TEXT_MAX];
static char pkg_db_buf[PKG_TEXT_MAX];
static char pkg_db_new_buf[PKG_TEXT_MAX];
static pkg_u8 pkg_copy_buf[PKG_COPY_CHUNK];
static char pkg_upgrade_names[PKG_UPGRADE_ALL_MAX][PKG_NAME_MAX];

static int pkg_has_prefix(const char *text, const char *prefix) {
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

static int pkg_has_suffix(const char *text, const char *suffix) {
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

static int pkg_is_url(const char *text) {
    return (pkg_has_prefix(text, "http://") != 0 || pkg_has_prefix(text, "https://") != 0) ? 1 : 0;
}

static int pkg_safe_name(const char *name) {
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

static char *pkg_trim_mut(char *text) {
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

static void pkg_copy_trimmed(char *dst, u64 dst_size, const char *src) {
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

static int pkg_append_text(char *dst, u64 dst_size, u64 *io_len, const char *text) {
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

static int pkg_append_char(char *dst, u64 dst_size, u64 *io_len, char ch) {
    if (dst == (char *)0 || io_len == (u64 *)0 || *io_len + 1ULL >= dst_size) {
        return 0;
    }

    dst[*io_len] = ch;
    (*io_len)++;
    dst[*io_len] = '\0';
    return 1;
}

static int pkg_is_url_unreserved(char ch) {
    return ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '-' ||
            ch == '_' || ch == '.' || ch == '~')
               ? 1
               : 0;
}

static char pkg_hex_digit(u64 value) {
    value &= 0xFULL;
    if (value < 10ULL) {
        return (char)('0' + value);
    }
    return (char)('A' + (value - 10ULL));
}

static int pkg_append_url_encoded(char *dst, u64 dst_size, u64 *io_len, const char *text) {
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

static const char *pkg_skip_ws_const(const char *pos) {
    if (pos == (const char *)0) {
        return (const char *)0;
    }

    while (*pos == ' ' || *pos == '\t' || *pos == '\r' || *pos == '\n') {
        pos++;
    }
    return pos;
}

static int pkg_json_make_key_pattern(const char *key, char *out, u64 out_size) {
    int ret;

    if (key == (const char *)0 || out == (char *)0 || out_size == 0ULL) {
        return 0;
    }

    ret = snprintf(out, (usize)out_size, "\"%s\"", key);
    return (ret > 0 && (u64)ret < out_size) ? 1 : 0;
}

static const char *pkg_json_find_key_value(const char *start, const char *end, const char *key) {
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

static const char *pkg_json_object_end(const char *object_start) {
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

static int pkg_json_read_string_value(const char *value, const char *end, char *out, u64 out_size) {
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

static int pkg_json_get_string(const char *start, const char *end, const char *key, char *out, u64 out_size) {
    const char *value = pkg_json_find_key_value(start, end, key);

    if (out != (char *)0 && out_size > 0ULL) {
        out[0] = '\0';
    }
    return pkg_json_read_string_value(value, end, out, out_size);
}

static int pkg_json_get_number_text(const char *start, const char *end, const char *key, char *out, u64 out_size) {
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

static int pkg_read_file(const char *path, char *out, u64 out_size, u64 *out_len) {
    u64 fd;
    u64 total = 0ULL;

    if (path == (const char *)0 || out == (char *)0 || out_size == 0ULL) {
        return 0;
    }

    out[0] = '\0';
    fd = cleonos_sys_fd_open(path, CLEONOS_O_RDONLY, 0ULL);
    if (fd == (u64)-1) {
        return 0;
    }

    while (total + 1ULL < out_size) {
        u64 got = cleonos_sys_fd_read(fd, out + total, out_size - 1ULL - total);
        if (got == (u64)-1) {
            (void)cleonos_sys_fd_close(fd);
            return 0;
        }
        if (got == 0ULL) {
            break;
        }
        total += got;
    }

    (void)cleonos_sys_fd_close(fd);
    out[total] = '\0';
    if (out_len != (u64 *)0) {
        *out_len = total;
    }
    return 1;
}

static int pkg_write_file(const char *path, const char *data, u64 size) {
    u64 fd;
    u64 done = 0ULL;

    if (path == (const char *)0 || data == (const char *)0) {
        return 0;
    }

    fd = cleonos_sys_fd_open(path, CLEONOS_O_WRONLY | CLEONOS_O_CREAT | CLEONOS_O_TRUNC, 0ULL);
    if (fd == (u64)-1) {
        return 0;
    }

    while (done < size) {
        u64 wrote = cleonos_sys_fd_write(fd, data + done, size - done);
        if (wrote == 0ULL || wrote == (u64)-1) {
            (void)cleonos_sys_fd_close(fd);
            return 0;
        }
        done += wrote;
    }

    (void)cleonos_sys_fd_close(fd);
    return 1;
}

static int pkg_copy_file(const char *src, const char *dst) {
    u64 in_fd;
    u64 out_fd;
    int ok = 0;

    if (src == (const char *)0 || dst == (const char *)0 || cleonos_sys_fs_stat_type(src) != 1ULL) {
        return 0;
    }

    in_fd = cleonos_sys_fd_open(src, CLEONOS_O_RDONLY, 0ULL);
    if (in_fd == (u64)-1) {
        return 0;
    }

    out_fd = cleonos_sys_fd_open(dst, CLEONOS_O_WRONLY | CLEONOS_O_CREAT | CLEONOS_O_TRUNC, 0ULL);
    if (out_fd == (u64)-1) {
        (void)cleonos_sys_fd_close(in_fd);
        return 0;
    }

    for (;;) {
        u64 got = cleonos_sys_fd_read(in_fd, pkg_copy_buf, (u64)sizeof(pkg_copy_buf));
        u64 done = 0ULL;

        if (got == (u64)-1) {
            break;
        }
        if (got == 0ULL) {
            ok = 1;
            break;
        }
        while (done < got) {
            u64 wrote = cleonos_sys_fd_write(out_fd, pkg_copy_buf + done, got - done);
            if (wrote == 0ULL || wrote == (u64)-1) {
                got = (u64)-1;
                break;
            }
            done += wrote;
        }
        if (got == (u64)-1) {
            break;
        }
    }

    (void)cleonos_sys_fd_close(in_fd);
    (void)cleonos_sys_fd_close(out_fd);
    return ok;
}

static int pkg_file_has_elf_magic(const char *path) {
    u64 fd;
    pkg_u8 magic[4];
    u64 got;

    fd = cleonos_sys_fd_open(path, CLEONOS_O_RDONLY, 0ULL);
    if (fd == (u64)-1) {
        return 0;
    }

    got = cleonos_sys_fd_read(fd, magic, (u64)sizeof(magic));
    (void)cleonos_sys_fd_close(fd);

    return (got == 4ULL && magic[0] == 0x7FU && magic[1] == (pkg_u8)'E' && magic[2] == (pkg_u8)'L' &&
            magic[3] == (pkg_u8)'F')
               ? 1
               : 0;
}

static void pkg_basename_no_ext(const char *path, char *out, u64 out_size) {
    const char *base = path;
    u64 len = 0ULL;
    u64 i;

    if (out == (char *)0 || out_size == 0ULL) {
        return;
    }

    out[0] = '\0';
    if (path == (const char *)0 || path[0] == '\0') {
        return;
    }

    for (i = 0ULL; path[i] != '\0' && path[i] != '?' && path[i] != '#'; i++) {
        if (path[i] == '/' || path[i] == '\\') {
            base = path + i + 1ULL;
        }
    }

    while (base[len] != '\0' && base[len] != '?' && base[len] != '#' && len + 1ULL < out_size) {
        out[len] = base[len];
        len++;
    }
    out[len] = '\0';

    if (pkg_has_suffix(out, ".elf") != 0 || pkg_has_suffix(out, ".ELF") != 0) {
        out[ush_strlen(out) - 4ULL] = '\0';
    } else if (pkg_has_suffix(out, ".clpkg") != 0) {
        out[ush_strlen(out) - 6ULL] = '\0';
    }
}

static int pkg_default_target(const char *name, char *out, u64 out_size) {
    int ret;

    if (pkg_safe_name(name) == 0 || out == (char *)0 || out_size == 0ULL) {
        return 0;
    }

    ret = snprintf(out, (usize)out_size, "/shell/%s.elf", name);
    return (ret > 0 && (u64)ret < out_size) ? 1 : 0;
}

static int pkg_target_is_allowed(const char *path) {
    if (path == (const char *)0 || path[0] == '\0') {
        return 0;
    }

    if (pkg_has_prefix(path, "/shell/") == 0) {
        return 0;
    }

    if (strstr(path, "/../") != (char *)0 || strstr(path, "../") == path || strstr(path, "/..") != (char *)0) {
        return 0;
    }

    return pkg_has_suffix(path, ".elf");
}

static void pkg_dirname(const char *path, char *out, u64 out_size) {
    const char *last = (const char *)0;
    u64 i;

    if (out == (char *)0 || out_size == 0ULL) {
        return;
    }

    out[0] = '\0';
    if (path == (const char *)0) {
        return;
    }

    for (i = 0ULL; path[i] != '\0' && path[i] != '?' && path[i] != '#'; i++) {
        if (path[i] == '/' || path[i] == '\\') {
            last = path + i;
        }
    }

    if (last == (const char *)0) {
        ush_copy(out, out_size, ".");
        return;
    }

    for (i = 0ULL; path + i < last && i + 1ULL < out_size; i++) {
        out[i] = path[i];
    }
    out[i] = '\0';
}

static int pkg_join_path(const char *dir, const char *leaf, char *out, u64 out_size) {
    int ret;

    if (leaf == (const char *)0 || out == (char *)0 || out_size == 0ULL) {
        return 0;
    }

    if (leaf[0] == '/' || pkg_is_url(leaf) != 0) {
        ush_copy(out, out_size, leaf);
        return out[0] != '\0';
    }

    if (dir == (const char *)0 || dir[0] == '\0' || ush_streq(dir, ".") != 0) {
        ush_copy(out, out_size, leaf);
        return out[0] != '\0';
    }

    ret = snprintf(out, (usize)out_size, "%s/%s", dir, leaf);
    return (ret > 0 && (u64)ret < out_size) ? 1 : 0;
}

static void pkg_manifest_init(pkg_manifest *manifest) {
    if (manifest != (pkg_manifest *)0) {
        ush_zero(manifest, (u64)sizeof(*manifest));
        ush_copy(manifest->version, (u64)sizeof(manifest->version), "0.0.0");
    }
}

static int pkg_parse_manifest(char *text, pkg_manifest *out_manifest) {
    char *line;

    if (text == (char *)0 || out_manifest == (pkg_manifest *)0) {
        return 0;
    }

    pkg_manifest_init(out_manifest);
    line = text;
    while (*line != '\0') {
        char *next = line;
        char *eq;
        char *key;
        char *value;

        while (*next != '\0' && *next != '\n') {
            next++;
        }
        if (*next == '\n') {
            *next = '\0';
            next++;
        }

        key = pkg_trim_mut(line);
        if (key[0] != '\0' && key[0] != '#') {
            eq = strchr(key, '=');
            if (eq != (char *)0) {
                *eq = '\0';
                value = pkg_trim_mut(eq + 1);
                key = pkg_trim_mut(key);
                if (ush_streq(key, "name") != 0) {
                    pkg_copy_trimmed(out_manifest->name, (u64)sizeof(out_manifest->name), value);
                } else if (ush_streq(key, "version") != 0) {
                    pkg_copy_trimmed(out_manifest->version, (u64)sizeof(out_manifest->version), value);
                } else if (ush_streq(key, "target") != 0) {
                    pkg_copy_trimmed(out_manifest->target, (u64)sizeof(out_manifest->target), value);
                } else if (ush_streq(key, "url") != 0 || ush_streq(key, "elf_url") != 0) {
                    pkg_copy_trimmed(out_manifest->url, (u64)sizeof(out_manifest->url), value);
                } else if (ush_streq(key, "path") != 0 || ush_streq(key, "elf") != 0 || ush_streq(key, "file") != 0) {
                    if (pkg_is_url(value) != 0) {
                        pkg_copy_trimmed(out_manifest->url, (u64)sizeof(out_manifest->url), value);
                    } else {
                        pkg_copy_trimmed(out_manifest->path, (u64)sizeof(out_manifest->path), value);
                    }
                } else if (ush_streq(key, "description") != 0 || ush_streq(key, "desc") != 0) {
                    pkg_copy_trimmed(out_manifest->description, (u64)sizeof(out_manifest->description), value);
                }
            }
        }

        line = next;
    }

    return (pkg_safe_name(out_manifest->name) != 0) ? 1 : 0;
}

static int pkg_ensure_db_dir(void) {
    if (cleonos_sys_fs_stat_type(PKG_DB_DIR) == 2ULL) {
        return 1;
    }

    return (cleonos_sys_fs_mkdir(PKG_DB_DIR) != 0ULL) ? 1 : 0;
}

static int pkg_load_repo(char *out, u64 out_size) {
    u64 got = 0ULL;
    char *trimmed;

    if (out == (char *)0 || out_size == 0ULL) {
        return 0;
    }

    if (pkg_read_file(PKG_REPO_PATH, pkg_text_buf, (u64)sizeof(pkg_text_buf), &got) != 0 && got > 0ULL) {
        trimmed = pkg_trim_mut(pkg_text_buf);
        if (trimmed[0] != '\0') {
            ush_copy(out, out_size, trimmed);
            return 1;
        }
    }

    ush_copy(out, out_size, PKG_DEFAULT_REPO);
    return 1;
}

static int pkg_build_repo_url(const char *repo, const char *mode, const char *name, char *out, u64 out_size) {
    const char *templ;
    const char *sep;
    int ret;

    if (repo == (const char *)0 || mode == (const char *)0 || pkg_safe_name(name) == 0 || out == (char *)0 ||
        out_size == 0ULL) {
        return 0;
    }

    templ = strstr(repo, "%s");
    if (templ != (const char *)0) {
        u64 used = 0ULL;
        const char *tail = templ + 2;

        out[0] = '\0';
        while (repo < templ) {
            if (pkg_append_char(out, out_size, &used, *repo) == 0) {
                return 0;
            }
            repo++;
        }
        if (pkg_append_text(out, out_size, &used, name) == 0 ||
            pkg_append_text(out, out_size, &used, tail) == 0) {
            return 0;
        }
        return 1;
    } else if (strchr(repo, '?') != (char *)0) {
        ret = snprintf(out, (usize)out_size, "%s&%s=%s", repo, mode, name);
    } else if (pkg_has_suffix(repo, ".php") != 0) {
        ret = snprintf(out, (usize)out_size, "%s?%s=%s", repo, mode, name);
    } else {
        sep = pkg_has_suffix(repo, "/") != 0 ? "" : "/";
        ret = snprintf(out, (usize)out_size, "%s%sindex.php?%s=%s", repo, sep, mode, name);
    }

    return (ret > 0 && (u64)ret < out_size) ? 1 : 0;
}

static int pkg_build_api_url(const char *repo, const char *api, const char *param_key, const char *param_value,
                             char *out, u64 out_size) {
    const char *sep;
    u64 used = 0ULL;
    int ret;

    if (repo == (const char *)0 || api == (const char *)0 || out == (char *)0 || out_size == 0ULL ||
        strstr(repo, "%s") != (const char *)0) {
        return 0;
    }

    out[0] = '\0';
    if (strchr(repo, '?') != (char *)0) {
        ret = snprintf(out, (usize)out_size, "%s&api=%s", repo, api);
    } else if (pkg_has_suffix(repo, ".php") != 0) {
        ret = snprintf(out, (usize)out_size, "%s?api=%s", repo, api);
    } else {
        sep = pkg_has_suffix(repo, "/") != 0 ? "" : "/";
        ret = snprintf(out, (usize)out_size, "%s%sindex.php?api=%s", repo, sep, api);
    }

    if (ret <= 0 || (u64)ret >= out_size) {
        return 0;
    }
    used = (u64)ret;

    if (param_key != (const char *)0 && param_key[0] != '\0') {
        if (pkg_append_char(out, out_size, &used, '&') == 0 || pkg_append_text(out, out_size, &used, param_key) == 0 ||
            pkg_append_char(out, out_size, &used, '=') == 0 ||
            pkg_append_url_encoded(out, out_size, &used, (param_value != (const char *)0) ? param_value : "") == 0) {
            return 0;
        }
    }

    return 1;
}

static int pkg_write_command_ctx(const char *cmd, const char *arg, const char *cwd) {
    ush_cmd_ctx ctx;

    ush_zero(&ctx, (u64)sizeof(ctx));
    if (cmd != (const char *)0) {
        ush_copy(ctx.cmd, (u64)sizeof(ctx.cmd), cmd);
    }
    if (arg != (const char *)0) {
        ush_copy(ctx.arg, (u64)sizeof(ctx.arg), arg);
    }
    if (cwd != (const char *)0 && cwd[0] == '/') {
        ush_copy(ctx.cwd, (u64)sizeof(ctx.cwd), cwd);
    } else {
        ush_copy(ctx.cwd, (u64)sizeof(ctx.cwd), "/");
    }

    return (cleonos_sys_fs_write(USH_CMD_CTX_PATH, (const char *)&ctx, (u64)sizeof(ctx)) != 0ULL) ? 1 : 0;
}

static int pkg_download_to(const char *url, const char *out_path) {
    char wget_arg[USH_ARG_MAX];
    char env_line[64];
    int n;
    u64 status;

    if (url == (const char *)0 || out_path == (const char *)0 || url[0] == '\0' || out_path[0] == '\0') {
        return 0;
    }

    n = snprintf(wget_arg, sizeof(wget_arg), "-O %s %s", out_path, url);
    if (n <= 0 || (u64)n >= (u64)sizeof(wget_arg)) {
        (void)puts("pkg: URL too long for wget bridge");
        return 0;
    }

    (void)cleonos_sys_fs_remove(USH_CMD_CTX_PATH);
    (void)cleonos_sys_fs_remove(USH_CMD_RET_PATH);
    if (pkg_write_command_ctx("wget", wget_arg, "/") == 0) {
        (void)puts("pkg: cannot prepare wget command context");
        return 0;
    }

    ush_copy(env_line, (u64)sizeof(env_line), "PWD=/;PKG=1");
    status = cleonos_sys_exec_pathv("/shell/wget.elf", "", env_line);
    (void)cleonos_sys_fs_remove(USH_CMD_CTX_PATH);
    (void)cleonos_sys_fs_remove(USH_CMD_RET_PATH);

    if (status != 0ULL) {
        (void)printf("pkg: wget failed, status=0x%llx\n", (unsigned long long)status);
        return 0;
    }

    return (cleonos_sys_fs_stat_type(out_path) == 1ULL) ? 1 : 0;
}

static int pkg_fetch_api(const char *api, const char *param_key, const char *param_value, char *out, u64 out_size,
                         u64 *out_len) {
    char repo[PKG_URL_MAX];
    char url[PKG_URL_MAX];
    u64 len = 0ULL;

    if (api == (const char *)0 || out == (char *)0 || out_size == 0ULL) {
        return 0;
    }

    if (pkg_load_repo(repo, (u64)sizeof(repo)) == 0 ||
        pkg_build_api_url(repo, api, param_key, param_value, url, (u64)sizeof(url)) == 0) {
        (void)puts("pkg: repository API URL unavailable; use pkg repo http://host/index.php");
        return 0;
    }

    if (pkg_download_to(url, PKG_TMP_API) == 0) {
        return 0;
    }

    if (pkg_read_file(PKG_TMP_API, out, out_size, &len) == 0 || len == 0ULL) {
        (void)puts("pkg: API response read failed");
        return 0;
    }

    if (len + 1ULL >= out_size) {
        (void)puts("pkg: API response too large");
        return 0;
    }

    if (out_len != (u64 *)0) {
        *out_len = len;
    }
    return 1;
}

static void pkg_remote_package_init(pkg_remote_package *package) {
    if (package != (pkg_remote_package *)0) {
        ush_zero(package, (u64)sizeof(*package));
    }
}

static int pkg_parse_remote_package_object(const char *start, const char *end, pkg_remote_package *out) {
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
    (void)pkg_json_get_string(start, end, "owner", out->owner, (u64)sizeof(out->owner));
    (void)pkg_json_get_string(start, end, "manifest_url", out->manifest_url, (u64)sizeof(out->manifest_url));
    (void)pkg_json_get_string(start, end, "download_url", out->download_url, (u64)sizeof(out->download_url));
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

static const char *pkg_json_find_named_array(const char *text, const char *key) {
    const char *value = pkg_json_find_key_value(text, (const char *)0, key);

    if (value == (const char *)0 || *value != '[') {
        return (const char *)0;
    }
    return value + 1;
}

static int pkg_remote_next_package(const char **io_cursor, pkg_remote_package *out) {
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

static int pkg_parse_info_package(const char *text, pkg_remote_package *out) {
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

static void pkg_print_api_error_or_default(const char *text, const char *fallback) {
    char error[128];

    if (pkg_json_get_string(text, (const char *)0, "error", error, (u64)sizeof(error)) != 0 && error[0] != '\0') {
        (void)printf("pkg: %s\n", error);
    } else {
        (void)puts(fallback);
    }
}

static int pkg_resolve_local_path(const ush_state *sh, const char *arg, char *out, u64 out_size) {
    if (sh == (const ush_state *)0 || arg == (const char *)0 || out == (char *)0 || out_size == 0ULL) {
        return 0;
    }

    if (arg[0] == '/') {
        ush_copy(out, out_size, arg);
        return out[0] != '\0';
    }

    return ush_resolve_path(sh, arg, out, out_size);
}

static int pkg_record_install(const pkg_manifest *manifest, const char *source) {
    u64 old_len = 0ULL;
    u64 new_len = 0ULL;
    char *line;

    if (manifest == (const pkg_manifest *)0 || manifest->name[0] == '\0' || manifest->target[0] == '\0') {
        return 0;
    }

    if (pkg_ensure_db_dir() == 0) {
        (void)puts("pkg: cannot create /system/pkg");
        return 0;
    }

    pkg_db_new_buf[0] = '\0';
    if (pkg_read_file(PKG_DB_PATH, pkg_db_buf, (u64)sizeof(pkg_db_buf), &old_len) == 0) {
        pkg_db_buf[0] = '\0';
        old_len = 0ULL;
    }
    (void)old_len;

    line = pkg_db_buf;
    while (*line != '\0') {
        char *next = line;
        char line_copy[384];
        char *bar;

        while (*next != '\0' && *next != '\n') {
            next++;
        }
        if (*next == '\n') {
            *next = '\0';
            next++;
        }

        ush_copy(line_copy, (u64)sizeof(line_copy), line);
        bar = strchr(line_copy, '|');
        if (bar != (char *)0) {
            *bar = '\0';
        }

        if (line[0] != '\0' && ush_streq(line_copy, manifest->name) == 0) {
            if (pkg_append_text(pkg_db_new_buf, (u64)sizeof(pkg_db_new_buf), &new_len, line) == 0 ||
                pkg_append_char(pkg_db_new_buf, (u64)sizeof(pkg_db_new_buf), &new_len, '\n') == 0) {
                return 0;
            }
        }
        line = next;
    }

    if (pkg_append_text(pkg_db_new_buf, (u64)sizeof(pkg_db_new_buf), &new_len, manifest->name) == 0 ||
        pkg_append_char(pkg_db_new_buf, (u64)sizeof(pkg_db_new_buf), &new_len, '|') == 0 ||
        pkg_append_text(pkg_db_new_buf, (u64)sizeof(pkg_db_new_buf), &new_len, manifest->version) == 0 ||
        pkg_append_char(pkg_db_new_buf, (u64)sizeof(pkg_db_new_buf), &new_len, '|') == 0 ||
        pkg_append_text(pkg_db_new_buf, (u64)sizeof(pkg_db_new_buf), &new_len, manifest->target) == 0 ||
        pkg_append_char(pkg_db_new_buf, (u64)sizeof(pkg_db_new_buf), &new_len, '|') == 0 ||
        pkg_append_text(pkg_db_new_buf, (u64)sizeof(pkg_db_new_buf), &new_len,
                        (source != (const char *)0) ? source : "unknown") == 0 ||
        pkg_append_char(pkg_db_new_buf, (u64)sizeof(pkg_db_new_buf), &new_len, '\n') == 0) {
        return 0;
    }

    return pkg_write_file(PKG_DB_PATH, pkg_db_new_buf, new_len);
}

static int pkg_install_elf_file(const pkg_manifest *manifest, const char *elf_path, const char *source) {
    if (manifest == (const pkg_manifest *)0 || elf_path == (const char *)0) {
        return 0;
    }

    if (pkg_target_is_allowed(manifest->target) == 0) {
        (void)puts("pkg: invalid install target, only /shell/*.elf is allowed");
        return 0;
    }

    if (pkg_file_has_elf_magic(elf_path) == 0) {
        (void)puts("pkg: input is not an ELF file");
        return 0;
    }

    if (pkg_copy_file(elf_path, manifest->target) == 0) {
        (void)printf("pkg: copy failed: %s -> %s\n", elf_path, manifest->target);
        return 0;
    }

    if (pkg_record_install(manifest, source) == 0) {
        (void)puts("pkg: warning: installed ELF but failed to update installed.db");
        return 0;
    }

    (void)printf("pkg: installed %s %s -> %s\n", manifest->name, manifest->version, manifest->target);
    return 1;
}

static int pkg_install_local_elf(const ush_state *sh, const char *source_arg) {
    pkg_manifest manifest;
    char abs_path[USH_PATH_MAX];

    if (pkg_resolve_local_path(sh, source_arg, abs_path, (u64)sizeof(abs_path)) == 0) {
        (void)puts("pkg: invalid local path");
        return 0;
    }

    pkg_manifest_init(&manifest);
    pkg_basename_no_ext(abs_path, manifest.name, (u64)sizeof(manifest.name));
    if (pkg_safe_name(manifest.name) == 0 || pkg_default_target(manifest.name, manifest.target, (u64)sizeof(manifest.target)) == 0) {
        (void)puts("pkg: cannot infer package name");
        return 0;
    }
    ush_copy(manifest.version, (u64)sizeof(manifest.version), "local");
    return pkg_install_elf_file(&manifest, abs_path, abs_path);
}

static int pkg_complete_manifest(const ush_state *sh, pkg_manifest *manifest, const char *manifest_source,
                                 int source_is_url) {
    char base[USH_PATH_MAX];
    char resolved[USH_PATH_MAX];
    (void)sh;

    if (manifest == (pkg_manifest *)0 || pkg_safe_name(manifest->name) == 0) {
        return 0;
    }

    if (manifest->target[0] == '\0' && pkg_default_target(manifest->name, manifest->target,
                                                          (u64)sizeof(manifest->target)) == 0) {
        return 0;
    }

    if (manifest->path[0] != '\0' && manifest->path[0] != '/' && pkg_is_url(manifest->path) == 0) {
        pkg_dirname(manifest_source, base, (u64)sizeof(base));
        if (pkg_join_path(base, manifest->path, resolved, (u64)sizeof(resolved)) == 0) {
            return 0;
        }
        if (source_is_url != 0) {
            ush_copy(manifest->url, (u64)sizeof(manifest->url), resolved);
            manifest->path[0] = '\0';
        } else {
            ush_copy(manifest->path, (u64)sizeof(manifest->path), resolved);
        }
    }

    return 1;
}

static int pkg_install_manifest_file(const ush_state *sh, const char *manifest_path, const char *origin, int origin_is_url) {
    pkg_manifest manifest;
    u64 manifest_len = 0ULL;
    const char *elf_source = (const char *)0;

    if (pkg_read_file(manifest_path, pkg_text_buf, (u64)sizeof(pkg_text_buf), &manifest_len) == 0 || manifest_len == 0ULL) {
        (void)puts("pkg: manifest read failed");
        return 0;
    }

    if (pkg_parse_manifest(pkg_text_buf, &manifest) == 0) {
        (void)puts("pkg: invalid manifest");
        return 0;
    }

    if (pkg_complete_manifest(sh, &manifest, (origin != (const char *)0) ? origin : manifest_path, origin_is_url) == 0) {
        (void)puts("pkg: incomplete manifest");
        return 0;
    }

    if (manifest.url[0] != '\0') {
        (void)printf("pkg: download %s\n", manifest.url);
        if (pkg_download_to(manifest.url, PKG_TMP_ELF) == 0) {
            return 0;
        }
        elf_source = PKG_TMP_ELF;
    } else if (manifest.path[0] != '\0') {
        elf_source = manifest.path;
    } else {
        (void)puts("pkg: manifest has no url/path/elf");
        return 0;
    }

    return pkg_install_elf_file(&manifest, elf_source, (origin != (const char *)0) ? origin : manifest_path);
}

static int pkg_install_url_elf(const char *url) {
    pkg_manifest manifest;

    pkg_manifest_init(&manifest);
    pkg_basename_no_ext(url, manifest.name, (u64)sizeof(manifest.name));
    if (pkg_safe_name(manifest.name) == 0 || pkg_default_target(manifest.name, manifest.target, (u64)sizeof(manifest.target)) == 0) {
        (void)puts("pkg: cannot infer package name from URL");
        return 0;
    }
    ush_copy(manifest.version, (u64)sizeof(manifest.version), "remote");

    if (pkg_download_to(url, PKG_TMP_ELF) == 0) {
        return 0;
    }

    return pkg_install_elf_file(&manifest, PKG_TMP_ELF, url);
}

static int pkg_install_repo_package(const ush_state *sh, const char *name) {
    char repo[PKG_URL_MAX];
    char manifest_url[PKG_URL_MAX];

    if (pkg_safe_name(name) == 0) {
        (void)puts("pkg: invalid package name");
        return 0;
    }

    if (pkg_load_repo(repo, (u64)sizeof(repo)) == 0 ||
        pkg_build_repo_url(repo, "manifest", name, manifest_url, (u64)sizeof(manifest_url)) == 0) {
        (void)puts("pkg: cannot build repository URL");
        return 0;
    }

    (void)printf("pkg: repo %s\n", repo);
    if (pkg_download_to(manifest_url, PKG_TMP_MANIFEST) == 0) {
        return 0;
    }
    return pkg_install_manifest_file(sh, PKG_TMP_MANIFEST, manifest_url, 1);
}

static int pkg_cmd_install(const ush_state *sh, const char *arg) {
    char first[PKG_URL_MAX];
    const char *rest = "";

    if (sh == (const ush_state *)0 || arg == (const char *)0 ||
        ush_split_first_and_rest(arg, first, (u64)sizeof(first), &rest) == 0 || first[0] == '\0') {
        (void)puts("usage: pkg install <name|file.elf|file.clpkg|url>");
        return 0;
    }

    if (rest != (const char *)0 && rest[0] != '\0') {
        (void)puts("pkg: install accepts exactly one argument");
        return 0;
    }

    if (pkg_is_url(first) != 0) {
        if (pkg_has_suffix(first, ".elf") != 0) {
            return pkg_install_url_elf(first);
        }
        (void)printf("pkg: download manifest %s\n", first);
        if (pkg_download_to(first, PKG_TMP_MANIFEST) == 0) {
            return 0;
        }
        return pkg_install_manifest_file(sh, PKG_TMP_MANIFEST, first, 1);
    }

    if (pkg_has_suffix(first, ".elf") != 0) {
        return pkg_install_local_elf(sh, first);
    }

    if (pkg_has_suffix(first, ".clpkg") != 0) {
        char manifest_abs[USH_PATH_MAX];
        if (pkg_resolve_local_path(sh, first, manifest_abs, (u64)sizeof(manifest_abs)) == 0) {
            (void)puts("pkg: invalid manifest path");
            return 0;
        }
        return pkg_install_manifest_file(sh, manifest_abs, manifest_abs, 0);
    }

    if (pkg_safe_name(first) != 0) {
        return pkg_install_repo_package(sh, first);
    }

    (void)puts("pkg: invalid package source");
    return 0;
}

static void pkg_print_db_line(char *line) {
    char *version;
    char *target;
    char *source;

    if (line == (char *)0 || line[0] == '\0') {
        return;
    }

    version = strchr(line, '|');
    if (version == (char *)0) {
        (void)puts(line);
        return;
    }
    *version = '\0';
    version++;

    target = strchr(version, '|');
    if (target == (char *)0) {
        (void)printf("%-18s %s\n", line, version);
        return;
    }
    *target = '\0';
    target++;

    source = strchr(target, '|');
    if (source != (char *)0) {
        *source = '\0';
        source++;
    }

    (void)printf("%-18s %-12s %s\n", line, version, target);
}

static int pkg_cmd_list(void) {
    u64 len = 0ULL;
    char *line;
    int any = 0;

    if (pkg_read_file(PKG_DB_PATH, pkg_db_buf, (u64)sizeof(pkg_db_buf), &len) == 0 || len == 0ULL) {
        (void)puts("pkg: no packages installed");
        return 1;
    }

    (void)puts("name               version      target");
    (void)puts("------------------------------------------------------------");
    line = pkg_db_buf;
    while (*line != '\0') {
        char *next = line;
        char copy[384];

        while (*next != '\0' && *next != '\n') {
            next++;
        }
        if (*next == '\n') {
            *next = '\0';
            next++;
        }

        if (line[0] != '\0') {
            ush_copy(copy, (u64)sizeof(copy), line);
            pkg_print_db_line(copy);
            any = 1;
        }
        line = next;
    }

    if (any == 0) {
        (void)puts("pkg: no packages installed");
    }
    return 1;
}

static int pkg_remove_record(const char *name, char *out_target, u64 out_target_size, int *out_found) {
    u64 len = 0ULL;
    u64 new_len = 0ULL;
    char *line;

    if (out_found != (int *)0) {
        *out_found = 0;
    }
    if (out_target != (char *)0 && out_target_size > 0ULL) {
        out_target[0] = '\0';
    }

    if (pkg_read_file(PKG_DB_PATH, pkg_db_buf, (u64)sizeof(pkg_db_buf), &len) == 0) {
        pkg_db_buf[0] = '\0';
        return 1;
    }

    pkg_db_new_buf[0] = '\0';
    line = pkg_db_buf;
    while (*line != '\0') {
        char *next = line;
        char copy[384];
        char *bar;

        while (*next != '\0' && *next != '\n') {
            next++;
        }
        if (*next == '\n') {
            *next = '\0';
            next++;
        }

        ush_copy(copy, (u64)sizeof(copy), line);
        bar = strchr(copy, '|');
        if (bar != (char *)0) {
            *bar = '\0';
        }

        if (line[0] != '\0' && ush_streq(copy, name) != 0) {
            char *target_start = (bar != (char *)0) ? strchr(bar + 1, '|') : (char *)0;
            char *target_end;
            if (out_found != (int *)0) {
                *out_found = 1;
            }
            if (target_start != (char *)0 && out_target != (char *)0 && out_target_size > 0ULL) {
                target_start++;
                target_end = strchr(target_start, '|');
                if (target_end != (char *)0) {
                    *target_end = '\0';
                }
                ush_copy(out_target, out_target_size, target_start);
            }
        } else if (line[0] != '\0') {
            if (pkg_append_text(pkg_db_new_buf, (u64)sizeof(pkg_db_new_buf), &new_len, line) == 0 ||
                pkg_append_char(pkg_db_new_buf, (u64)sizeof(pkg_db_new_buf), &new_len, '\n') == 0) {
                return 0;
            }
        }

        line = next;
    }

    return pkg_write_file(PKG_DB_PATH, pkg_db_new_buf, new_len);
}

static int pkg_cmd_remove(const char *arg) {
    char name[PKG_NAME_MAX];
    char target[USH_PATH_MAX];
    const char *rest = "";
    int found = 0;

    if (arg == (const char *)0 || ush_split_first_and_rest(arg, name, (u64)sizeof(name), &rest) == 0 ||
        pkg_safe_name(name) == 0) {
        (void)puts("usage: pkg remove <name>");
        return 0;
    }

    if (rest != (const char *)0 && rest[0] != '\0') {
        (void)puts("pkg: remove accepts exactly one package name");
        return 0;
    }

    if (pkg_remove_record(name, target, (u64)sizeof(target), &found) == 0) {
        (void)puts("pkg: failed to update installed.db");
        return 0;
    }

    if (found == 0) {
        if (pkg_default_target(name, target, (u64)sizeof(target)) == 0) {
            (void)puts("pkg: package not installed");
            return 0;
        }
    }

    if (target[0] != '\0' && cleonos_sys_fs_stat_type(target) == 1ULL) {
        if (cleonos_sys_fs_remove(target) == 0ULL) {
            (void)printf("pkg: failed to remove %s\n", target);
            return 0;
        }
        (void)printf("pkg: removed %s\n", target);
    } else if (found == 0) {
        (void)puts("pkg: package not installed");
        return 0;
    }

    return 1;
}

static int pkg_cmd_repo(const char *arg) {
    char repo[PKG_URL_MAX];

    if (arg == (const char *)0 || arg[0] == '\0') {
        if (pkg_load_repo(repo, (u64)sizeof(repo)) == 0) {
            return 0;
        }
        (void)printf("pkg: repo %s\n", repo);
        return 1;
    }

    if (pkg_ensure_db_dir() == 0) {
        (void)puts("pkg: cannot create /system/pkg");
        return 0;
    }

    pkg_copy_trimmed(repo, (u64)sizeof(repo), arg);
    if (repo[0] == '\0' || (pkg_is_url(repo) == 0 && strstr(repo, "%s") == (char *)0)) {
        (void)puts("pkg: repo must be http(s) URL or URL template containing %s");
        return 0;
    }

    if (pkg_write_file(PKG_REPO_PATH, repo, ush_strlen(repo)) == 0) {
        (void)puts("pkg: repo write failed");
        return 0;
    }

    (void)printf("pkg: repo set to %s\n", repo);
    return 1;
}

static int pkg_cmd_info(const char *arg) {
    char name[PKG_NAME_MAX];
    const char *rest = "";
    u64 len = 0ULL;
    char *line;

    if (arg == (const char *)0 || ush_split_first_and_rest(arg, name, (u64)sizeof(name), &rest) == 0 ||
        pkg_safe_name(name) == 0) {
        (void)puts("usage: pkg info <name>");
        return 0;
    }

    if (rest != (const char *)0 && rest[0] != '\0') {
        (void)puts("pkg: info accepts exactly one package name");
        return 0;
    }

    if (pkg_read_file(PKG_DB_PATH, pkg_db_buf, (u64)sizeof(pkg_db_buf), &len) == 0 || len == 0ULL) {
        (void)puts("pkg: package not installed");
        return 0;
    }

    line = pkg_db_buf;
    while (*line != '\0') {
        char *next = line;
        char copy[384];
        char *version;
        char *target;
        char *source;

        while (*next != '\0' && *next != '\n') {
            next++;
        }
        if (*next == '\n') {
            *next = '\0';
            next++;
        }

        ush_copy(copy, (u64)sizeof(copy), line);
        version = strchr(copy, '|');
        if (version != (char *)0) {
            *version = '\0';
            version++;
            if (ush_streq(copy, name) != 0) {
                target = strchr(version, '|');
                if (target != (char *)0) {
                    *target = '\0';
                    target++;
                }
                source = (target != (char *)0) ? strchr(target, '|') : (char *)0;
                if (source != (char *)0) {
                    *source = '\0';
                    source++;
                }
                (void)printf("name: %s\n", name);
                (void)printf("version: %s\n", version);
                (void)printf("target: %s\n", (target != (char *)0) ? target : "");
                (void)printf("source: %s\n", (source != (char *)0) ? source : "");
                return 1;
            }
        }

        line = next;
    }

    (void)puts("pkg: package not installed");
    return 0;
}

static void pkg_print_remote_header(void) {
    (void)puts("name               version      size       description");
    (void)puts("----------------------------------------------------------------");
}

static void pkg_print_remote_line(const pkg_remote_package *package) {
    if (package == (const pkg_remote_package *)0) {
        return;
    }

    (void)printf("%-18s %-12s %-10s %s\n", package->name, package->version, package->size, package->description);
}

static int pkg_cmd_remote_list(void) {
    const char *cursor;
    pkg_remote_package package;
    u64 len = 0ULL;
    int any = 0;

    if (pkg_fetch_api("list", (const char *)0, (const char *)0, pkg_text_buf, (u64)sizeof(pkg_text_buf), &len) == 0) {
        return 0;
    }
    (void)len;

    cursor = pkg_json_find_named_array(pkg_text_buf, "packages");
    if (cursor == (const char *)0) {
        pkg_print_api_error_or_default(pkg_text_buf, "pkg: invalid list API response");
        return 0;
    }

    pkg_print_remote_header();
    while (pkg_remote_next_package(&cursor, &package) != 0) {
        pkg_print_remote_line(&package);
        any = 1;
    }

    if (any == 0) {
        (void)puts("pkg: no remote packages");
    }
    return 1;
}

static int pkg_cmd_remote_info(const char *arg) {
    char name[PKG_NAME_MAX];
    const char *rest = "";
    pkg_remote_package package;
    u64 len = 0ULL;

    if (arg == (const char *)0 || ush_split_first_and_rest(arg, name, (u64)sizeof(name), &rest) == 0 ||
        pkg_safe_name(name) == 0) {
        (void)puts("usage: pkg remote info <name>");
        return 0;
    }

    if (rest != (const char *)0 && rest[0] != '\0') {
        (void)puts("pkg: remote info accepts exactly one package name");
        return 0;
    }

    if (pkg_fetch_api("info", "name", name, pkg_text_buf, (u64)sizeof(pkg_text_buf), &len) == 0) {
        return 0;
    }
    (void)len;

    if (pkg_parse_info_package(pkg_text_buf, &package) == 0) {
        pkg_print_api_error_or_default(pkg_text_buf, "pkg: package not found");
        return 0;
    }

    (void)printf("name: %s\n", package.name);
    (void)printf("version: %s\n", package.version);
    (void)printf("target: %s\n", package.target);
    (void)printf("size: %s\n", package.size);
    (void)printf("owner: %s\n", package.owner);
    (void)printf("description: %s\n", package.description);
    (void)printf("manifest: %s\n", package.manifest_url);
    (void)printf("download: %s\n", package.download_url);
    return 1;
}

static int pkg_cmd_remote(const char *arg) {
    char subcmd[32];
    const char *rest = "";

    if (arg == (const char *)0 || ush_split_first_and_rest(arg, subcmd, (u64)sizeof(subcmd), &rest) == 0 ||
        subcmd[0] == '\0') {
        (void)puts("usage: pkg remote list | pkg remote info <name>");
        return 0;
    }

    if (ush_streq(subcmd, "list") != 0 || ush_streq(subcmd, "ls") != 0) {
        if (rest != (const char *)0 && rest[0] != '\0') {
            (void)puts("pkg: remote list does not accept arguments");
            return 0;
        }
        return pkg_cmd_remote_list();
    }

    if (ush_streq(subcmd, "info") != 0 || ush_streq(subcmd, "show") != 0) {
        return pkg_cmd_remote_info(rest);
    }

    (void)puts("usage: pkg remote list | pkg remote info <name>");
    return 0;
}

static int pkg_cmd_search(const char *arg) {
    char query[PKG_URL_MAX];
    const char *cursor;
    pkg_remote_package package;
    u64 len = 0ULL;
    int any = 0;

    if (arg == (const char *)0) {
        (void)puts("usage: pkg search <keyword>");
        return 0;
    }

    pkg_copy_trimmed(query, (u64)sizeof(query), arg);
    if (query[0] == '\0') {
        (void)puts("usage: pkg search <keyword>");
        return 0;
    }

    if (pkg_fetch_api("search", "q", query, pkg_text_buf, (u64)sizeof(pkg_text_buf), &len) == 0) {
        return 0;
    }
    (void)len;

    cursor = pkg_json_find_named_array(pkg_text_buf, "packages");
    if (cursor == (const char *)0) {
        pkg_print_api_error_or_default(pkg_text_buf, "pkg: invalid search API response");
        return 0;
    }

    pkg_print_remote_header();
    while (pkg_remote_next_package(&cursor, &package) != 0) {
        pkg_print_remote_line(&package);
        any = 1;
    }

    if (any == 0) {
        (void)puts("pkg: no matches");
    }
    return 1;
}

static int pkg_find_installed_version_in_db(const char *db_text, const char *name, char *out_version, u64 out_size) {
    const char *line;

    if (out_version != (char *)0 && out_size > 0ULL) {
        out_version[0] = '\0';
    }
    if (db_text == (const char *)0 || name == (const char *)0 || pkg_safe_name(name) == 0) {
        return 0;
    }

    line = db_text;
    while (*line != '\0') {
        const char *next = line;
        char copy[384];
        u64 len = 0ULL;
        char *version;

        while (*next != '\0' && *next != '\n') {
            if (len + 1ULL < (u64)sizeof(copy)) {
                copy[len] = *next;
                len++;
            }
            next++;
        }
        copy[len] = '\0';
        if (*next == '\n') {
            next++;
        }

        version = strchr(copy, '|');
        if (version != (char *)0) {
            *version = '\0';
            version++;
            if (ush_streq(copy, name) != 0) {
                char *target = strchr(version, '|');
                if (target != (char *)0) {
                    *target = '\0';
                }
                if (out_version != (char *)0 && out_size > 0ULL) {
                    ush_copy(out_version, out_size, version);
                }
                return 1;
            }
        }

        line = next;
    }

    return 0;
}

static int pkg_load_installed_db_for_remote(void) {
    u64 len = 0ULL;

    if (pkg_read_file(PKG_DB_PATH, pkg_db_buf, (u64)sizeof(pkg_db_buf), &len) == 0 || len == 0ULL) {
        pkg_db_buf[0] = '\0';
        return 0;
    }

    return 1;
}

static int pkg_cmd_update(void) {
    const char *cursor;
    pkg_remote_package package;
    char installed_version[PKG_VERSION_MAX];
    u64 len = 0ULL;
    int any = 0;

    if (pkg_load_installed_db_for_remote() == 0) {
        (void)puts("pkg: no packages installed");
        return 1;
    }

    if (pkg_fetch_api("list", (const char *)0, (const char *)0, pkg_text_buf, (u64)sizeof(pkg_text_buf), &len) == 0) {
        return 0;
    }
    (void)len;

    cursor = pkg_json_find_named_array(pkg_text_buf, "packages");
    if (cursor == (const char *)0) {
        pkg_print_api_error_or_default(pkg_text_buf, "pkg: invalid list API response");
        return 0;
    }

    while (pkg_remote_next_package(&cursor, &package) != 0) {
        if (pkg_find_installed_version_in_db(pkg_db_buf, package.name, installed_version,
                                             (u64)sizeof(installed_version)) != 0 &&
            strcmp(installed_version, package.version) != 0) {
            if (any == 0) {
                (void)puts("name               installed    remote");
                (void)puts("-----------------------------------------------");
            }
            (void)printf("%-18s %-12s %s\n", package.name, installed_version, package.version);
            any = 1;
        }
    }

    if (any == 0) {
        (void)puts("pkg: all installed packages are up to date");
    }
    return 1;
}

static int pkg_collect_outdated_names(u64 *out_count, int *out_no_installed) {
    const char *cursor;
    pkg_remote_package package;
    char installed_version[PKG_VERSION_MAX];
    u64 len = 0ULL;
    u64 count = 0ULL;

    if (out_count != (u64 *)0) {
        *out_count = 0ULL;
    }
    if (out_no_installed != (int *)0) {
        *out_no_installed = 0;
    }

    if (pkg_load_installed_db_for_remote() == 0) {
        if (out_no_installed != (int *)0) {
            *out_no_installed = 1;
        }
        (void)puts("pkg: no packages installed");
        return 1;
    }

    if (pkg_fetch_api("list", (const char *)0, (const char *)0, pkg_text_buf, (u64)sizeof(pkg_text_buf), &len) == 0) {
        return 0;
    }
    (void)len;

    cursor = pkg_json_find_named_array(pkg_text_buf, "packages");
    if (cursor == (const char *)0) {
        pkg_print_api_error_or_default(pkg_text_buf, "pkg: invalid list API response");
        return 0;
    }

    while (pkg_remote_next_package(&cursor, &package) != 0) {
        if (pkg_find_installed_version_in_db(pkg_db_buf, package.name, installed_version,
                                             (u64)sizeof(installed_version)) != 0 &&
            strcmp(installed_version, package.version) != 0) {
            if (count >= (u64)PKG_UPGRADE_ALL_MAX) {
                (void)puts("pkg: too many pending upgrades; upgrade packages one by one");
                return 0;
            }
            ush_copy(pkg_upgrade_names[count], (u64)PKG_NAME_MAX, package.name);
            count++;
        }
    }

    if (out_count != (u64 *)0) {
        *out_count = count;
    }
    return 1;
}

static int pkg_cmd_upgrade(const ush_state *sh, const char *arg) {
    char name[PKG_NAME_MAX];
    const char *rest = "";
    char installed_version[PKG_VERSION_MAX];
    u64 count = 0ULL;
    u64 i;
    int ok = 1;
    int no_installed = 0;

    if (sh == (const ush_state *)0 || arg == (const char *)0 ||
        ush_split_first_and_rest(arg, name, (u64)sizeof(name), &rest) == 0 || name[0] == '\0') {
        (void)puts("usage: pkg upgrade <name> | pkg upgrade --all");
        return 0;
    }

    if (rest != (const char *)0 && rest[0] != '\0') {
        (void)puts("pkg: upgrade accepts one package name or --all");
        return 0;
    }

    if (ush_streq(name, "--all") != 0 || ush_streq(name, "-a") != 0) {
        if (pkg_collect_outdated_names(&count, &no_installed) == 0) {
            return 0;
        }
        if (no_installed != 0) {
            return 1;
        }
        if (count == 0ULL) {
            (void)puts("pkg: all installed packages are up to date");
            return 1;
        }
        for (i = 0ULL; i < count; i++) {
            (void)printf("pkg: upgrade %s\n", pkg_upgrade_names[i]);
            if (pkg_install_repo_package(sh, pkg_upgrade_names[i]) == 0) {
                ok = 0;
            }
        }
        return ok;
    }

    if (pkg_safe_name(name) == 0) {
        (void)puts("pkg: invalid package name");
        return 0;
    }

    if (pkg_load_installed_db_for_remote() == 0 ||
        pkg_find_installed_version_in_db(pkg_db_buf, name, installed_version, (u64)sizeof(installed_version)) == 0) {
        (void)puts("pkg: package is not installed; use pkg install <name>");
        return 0;
    }

    (void)installed_version;
    return pkg_install_repo_package(sh, name);
}

static void pkg_usage(void) {
    (void)puts("usage:");
    (void)puts("  pkg install <name|file.elf|file.clpkg|url>");
    (void)puts("  pkg list");
    (void)puts("  pkg remove <name>");
    (void)puts("  pkg info <name>");
    (void)puts("  pkg remote list");
    (void)puts("  pkg remote info <name>");
    (void)puts("  pkg search <keyword>");
    (void)puts("  pkg update");
    (void)puts("  pkg upgrade <name>");
    (void)puts("  pkg upgrade --all");
    (void)puts("  pkg repo [url]");
    (void)puts("");
    (void)puts("manifest keys: name, version, target, url/path/elf, description");
}

static int pkg_run(const ush_state *sh, const char *arg) {
    char cmd[32];
    const char *rest = "";

    if (arg == (const char *)0 || arg[0] == '\0' || ush_streq(arg, "--help") != 0 || ush_streq(arg, "-h") != 0) {
        pkg_usage();
        return (arg != (const char *)0 && (ush_streq(arg, "--help") != 0 || ush_streq(arg, "-h") != 0)) ? 1 : 0;
    }

    if (ush_split_first_and_rest(arg, cmd, (u64)sizeof(cmd), &rest) == 0) {
        pkg_usage();
        return 0;
    }

    if (ush_streq(cmd, "install") != 0 || ush_streq(cmd, "add") != 0) {
        return pkg_cmd_install(sh, rest);
    }
    if (ush_streq(cmd, "list") != 0 || ush_streq(cmd, "ls") != 0) {
        return pkg_cmd_list();
    }
    if (ush_streq(cmd, "remove") != 0 || ush_streq(cmd, "rm") != 0 || ush_streq(cmd, "uninstall") != 0) {
        return pkg_cmd_remove(rest);
    }
    if (ush_streq(cmd, "repo") != 0) {
        return pkg_cmd_repo(rest);
    }
    if (ush_streq(cmd, "info") != 0 || ush_streq(cmd, "show") != 0) {
        return pkg_cmd_info(rest);
    }
    if (ush_streq(cmd, "remote") != 0) {
        return pkg_cmd_remote(rest);
    }
    if (ush_streq(cmd, "search") != 0 || ush_streq(cmd, "find") != 0) {
        return pkg_cmd_search(rest);
    }
    if (ush_streq(cmd, "update") != 0 || ush_streq(cmd, "check") != 0) {
        if (rest != (const char *)0 && rest[0] != '\0') {
            (void)puts("pkg: update does not accept arguments");
            return 0;
        }
        return pkg_cmd_update();
    }
    if (ush_streq(cmd, "upgrade") != 0) {
        return pkg_cmd_upgrade(sh, rest);
    }
    if (ush_streq(cmd, "help") != 0) {
        pkg_usage();
        return 1;
    }

    (void)puts("pkg: unknown command");
    pkg_usage();
    return 0;
}

static void pkg_reconstruct_argv(char *out, u64 out_size) {
    u64 argc;
    u64 i;
    u64 used = 0ULL;

    if (out == (char *)0 || out_size == 0ULL) {
        return;
    }
    out[0] = '\0';

    argc = cleonos_sys_proc_argc();
    for (i = 1ULL; i < argc; i++) {
        char item[128];
        if (cleonos_sys_proc_argv(i, item, (u64)sizeof(item)) == 0ULL || item[0] == '\0') {
            continue;
        }
        if (used != 0ULL && pkg_append_char(out, out_size, &used, ' ') == 0) {
            return;
        }
        if (pkg_append_text(out, out_size, &used, item) == 0) {
            return;
        }
    }
}

int pkg_client_app_main(void) {
    ush_cmd_ctx ctx;
    ush_cmd_ret ret;
    ush_state sh;
    char initial_cwd[USH_PATH_MAX];
    char arg_line[PKG_ARG_MAX];
    int has_context = 0;
    int success;
    const char *arg = "";

    ush_zero(&ctx, (u64)sizeof(ctx));
    ush_zero(&ret, (u64)sizeof(ret));
    ush_zero(arg_line, (u64)sizeof(arg_line));
    ush_init_state(&sh);
    ush_copy(initial_cwd, (u64)sizeof(initial_cwd), sh.cwd);

    if (ush_command_ctx_read(&ctx) != 0) {
        if (ctx.cmd[0] != '\0' && ush_streq(ctx.cmd, "pkg") != 0) {
            has_context = 1;
            ush_copy(arg_line, (u64)sizeof(arg_line), ctx.arg);
            arg = arg_line;
            if (ctx.cwd[0] == '/') {
                ush_copy(sh.cwd, (u64)sizeof(sh.cwd), ctx.cwd);
                ush_copy(initial_cwd, (u64)sizeof(initial_cwd), sh.cwd);
            }
        }
    }

    if (has_context == 0) {
        pkg_reconstruct_argv(arg_line, (u64)sizeof(arg_line));
        arg = arg_line;
    }

    success = pkg_run(&sh, arg);

    if (has_context != 0) {
        if (ush_streq(sh.cwd, initial_cwd) == 0) {
            ret.flags |= USH_CMD_RET_FLAG_CWD;
            ush_copy(ret.cwd, (u64)sizeof(ret.cwd), sh.cwd);
        }
        if (sh.exit_requested != 0) {
            ret.flags |= USH_CMD_RET_FLAG_EXIT;
            ret.exit_code = sh.exit_code;
        }
        (void)ush_command_ret_write(&ret);
    }

    return (success != 0) ? 0 : 1;
}
