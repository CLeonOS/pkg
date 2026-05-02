#include "pkg_internal.h"

void pkg_basename_no_ext(const char *path, char *out, u64 out_size) {
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

int pkg_default_target(const char *name, char *out, u64 out_size) {
    int ret;

    if (pkg_safe_name(name) == 0 || out == (char *)0 || out_size == 0ULL) {
        return 0;
    }

    ret = snprintf(out, (usize)out_size, "/shell/%s.elf", name);
    return (ret > 0 && (u64)ret < out_size) ? 1 : 0;
}

int pkg_target_is_allowed(const char *path) {
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

void pkg_dirname(const char *path, char *out, u64 out_size) {
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

int pkg_join_path(const char *dir, const char *leaf, char *out, u64 out_size) {
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

void pkg_manifest_init(pkg_manifest *manifest) {
    if (manifest != (pkg_manifest *)0) {
        ush_zero(manifest, (u64)sizeof(*manifest));
        ush_copy(manifest->version, (u64)sizeof(manifest->version), "0.0.0");
    }
}

int pkg_parse_manifest(char *text, pkg_manifest *out_manifest) {
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
                } else if (ush_streq(key, "depends") != 0 || ush_streq(key, "dependencies") != 0) {
                    pkg_copy_trimmed(out_manifest->depends, (u64)sizeof(out_manifest->depends), value);
                } else if (ush_streq(key, "category") != 0) {
                    pkg_copy_trimmed(out_manifest->category, (u64)sizeof(out_manifest->category), value);
                } else if (ush_streq(key, "tags") != 0) {
                    pkg_copy_trimmed(out_manifest->tags, (u64)sizeof(out_manifest->tags), value);
                } else if (ush_streq(key, "sha256") != 0 || ush_streq(key, "checksum") != 0) {
                    pkg_copy_trimmed(out_manifest->sha256, (u64)sizeof(out_manifest->sha256), value);
                } else if (ush_streq(key, "deprecated") != 0 || ush_streq(key, "deprecation") != 0) {
                    pkg_copy_trimmed(out_manifest->deprecated, (u64)sizeof(out_manifest->deprecated), value);
                }
            }
        }

        line = next;
    }

    return (pkg_safe_name(out_manifest->name) != 0) ? 1 : 0;
}

int pkg_ensure_db_dir(void) {
    if (cleonos_sys_fs_stat_type(PKG_DB_DIR) == 2ULL) {
        return 1;
    }

    return (cleonos_sys_fs_mkdir(PKG_DB_DIR) != 0ULL) ? 1 : 0;
}

int pkg_load_repo(char *out, u64 out_size) {
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

int pkg_build_repo_url(const char *repo, const char *mode, const char *name, char *out, u64 out_size) {
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

int pkg_build_api_url(const char *repo, const char *api, const char *param_key, const char *param_value,
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

int pkg_write_command_ctx(const char *cmd, const char *arg, const char *cwd) {
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

int pkg_download_to(const char *url, const char *out_path) {
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

    (void)cleonos_sys_fs_remove(out_path);
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
        (void)cleonos_sys_fs_remove(out_path);
        (void)printf("pkg: wget failed, status=0x%llx\n", (unsigned long long)status);
        return 0;
    }

    if (cleonos_sys_fs_stat_type(out_path) != 1ULL || cleonos_sys_fs_stat_size(out_path) == 0ULL) {
        (void)cleonos_sys_fs_remove(out_path);
        (void)puts("pkg: wget produced no output");
        return 0;
    }

    return 1;
}

int pkg_fetch_api(const char *api, const char *param_key, const char *param_value, char *out, u64 out_size,
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
