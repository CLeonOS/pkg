#include "pkg_internal.h"

typedef struct pkg_list_ctx {
    int any;
} pkg_list_ctx;

typedef struct pkg_source_list_ctx {
    const char *active;
    int any;
} pkg_source_list_ctx;

typedef struct pkg_update_ctx {
    int any;
} pkg_update_ctx;

int pkg_cmd_install(const ush_state *sh, const char *arg) {
    char first[PKG_URL_MAX];
    const char *rest = "";
    int ok;
    int reinstall = 0;

    if (sh == (const ush_state *)0 || arg == (const char *)0 ||
        ush_split_first_and_rest(arg, first, (u64)sizeof(first), &rest) == 0 || first[0] == '\0') {
        (void)puts("usage: pkg install [--dry-run] [--reinstall] <name|file.elf|file.clpkg|url>");
        return 0;
    }

    if (ush_streq(first, "--dry-run") != 0 || ush_streq(first, "-n") != 0) {
        return pkg_cmd_install_dry_run(sh, rest);
    }

    if (ush_streq(first, "--reinstall") != 0 || ush_streq(first, "--force-reinstall") != 0) {
        reinstall = 1;
        if (rest == (const char *)0 ||
            ush_split_first_and_rest(rest, first, (u64)sizeof(first), &rest) == 0 || first[0] == '\0') {
            (void)puts("usage: pkg install --reinstall <name|file.elf|file.clpkg|url>");
            return 0;
        }
    }

    if (rest != (const char *)0 && rest[0] != '\0') {
        (void)puts("pkg: install accepts exactly one argument");
        return 0;
    }

    if (pkg_lock_acquire() == 0) {
        return 0;
    }

    pkg_force_reinstall = reinstall;
    if (pkg_is_url(first) != 0) {
        if (pkg_has_suffix(first, ".elf") != 0) {
            ok = pkg_install_url_elf(first);
            pkg_force_reinstall = 0;
            pkg_lock_release();
            return ok;
        }
        (void)printf("pkg: download manifest %s\n", first);
        if (pkg_download_to(first, PKG_TMP_MANIFEST) == 0) {
            pkg_force_reinstall = 0;
            pkg_lock_release();
            return 0;
        }
        ok = pkg_install_manifest_file(sh, PKG_TMP_MANIFEST, first, 1);
        pkg_force_reinstall = 0;
        pkg_lock_release();
        return ok;
    }

    if (pkg_has_suffix(first, ".elf") != 0) {
        ok = pkg_install_local_elf(sh, first);
        pkg_force_reinstall = 0;
        pkg_lock_release();
        return ok;
    }

    if (pkg_has_suffix(first, ".clpkg") != 0) {
        char manifest_abs[USH_PATH_MAX];
        if (pkg_resolve_local_path(sh, first, manifest_abs, (u64)sizeof(manifest_abs)) == 0) {
            (void)puts("pkg: invalid manifest path");
            pkg_force_reinstall = 0;
            pkg_lock_release();
            return 0;
        }
        ok = pkg_install_manifest_file(sh, manifest_abs, manifest_abs, 0);
        pkg_force_reinstall = 0;
        pkg_lock_release();
        return ok;
    }

    {
        pkg_dependency dep;
        if (pkg_parse_dependency_spec(first, &dep) != 0 && dep.op[0] != '\0') {
            ok = pkg_install_repo_package_with_depth(sh, dep.name, dep.op, dep.version, 0ULL);
            pkg_force_reinstall = 0;
            pkg_lock_release();
            return ok;
        }
    }

    if (pkg_safe_name(first) != 0) {
        ok = pkg_install_repo_package(sh, first);
        pkg_force_reinstall = 0;
        pkg_lock_release();
        return ok;
    }

    (void)puts("pkg: invalid package source");
    pkg_force_reinstall = 0;
    pkg_lock_release();
    return 0;
}

void pkg_print_db_line(char *line) {
    char *name;
    char *version;
    char *target;

    if (line == (char *)0 || line[0] == '\0') {
        return;
    }

    if (pkg_db_line_parse(line, &name, &version, &target, (char **)0, (char **)0) == 0) {
        (void)puts(line);
        return;
    }

    (void)printf("%-18s %-12s %s\n", name, version, target);
}

static int pkg_list_iter(const pkg_installed_record *record, void *ctx_ptr) {
    pkg_list_ctx *ctx = (pkg_list_ctx *)ctx_ptr;

    if (record == (const pkg_installed_record *)0 || ctx == (pkg_list_ctx *)0) {
        return 0;
    }

    (void)printf("%-18s %-12s %s\n", record->name, record->version, record->target);
    ctx->any = 1;
    return 1;
}

int pkg_cmd_list(void) {
    pkg_list_ctx ctx;

    ush_zero(&ctx, (u64)sizeof(ctx));
    if (pkg_sqlite_init() == 0) {
        (void)puts("pkg: cannot open package database");
        return 0;
    }

    (void)puts("name               version      target");
    (void)puts("------------------------------------------------------------");
    if (pkg_sqlite_foreach_installed(pkg_list_iter, &ctx) == 0) {
        (void)puts("pkg: failed to read package database");
        return 0;
    }

    if (ctx.any == 0) {
        (void)puts("pkg: no packages installed");
    }
    return 1;
}

int pkg_remove_record(const char *name, char *out_target, u64 out_target_size, int *out_found) {
    return pkg_sqlite_remove_package(name, out_target, out_target_size, out_found);
}

typedef struct pkg_remove_dep_ctx {
    const char *name;
    int blockers;
} pkg_remove_dep_ctx;

static int pkg_remove_dep_iter(const pkg_installed_record *record, void *ctx_ptr) {
    pkg_remove_dep_ctx *ctx = (pkg_remove_dep_ctx *)ctx_ptr;

    if (record == (const pkg_installed_record *)0 || ctx == (pkg_remove_dep_ctx *)0 || ctx->name == (const char *)0) {
        return 0;
    }

    if (ush_streq(record->name, ctx->name) == 0 && pkg_dependency_list_mentions(record->depends, ctx->name) != 0) {
        if (ctx->blockers == 0) {
            (void)printf("pkg: cannot remove %s, required by:\n", ctx->name);
        }
        (void)printf("  %s\n", record->name);
        ctx->blockers = 1;
    }

    return 1;
}

int pkg_remove_has_reverse_dependencies(const char *name) {
    pkg_remove_dep_ctx ctx;

    ush_zero(&ctx, (u64)sizeof(ctx));
    ctx.name = name;
    if (pkg_sqlite_init() == 0) {
        return 0;
    }
    if (pkg_sqlite_foreach_installed(pkg_remove_dep_iter, &ctx) == 0) {
        return 0;
    }

    if (ctx.blockers != 0) {
        (void)puts("pkg: use pkg remove --force <name> to override");
    }
    return ctx.blockers;
}

int pkg_cmd_remove(const char *arg) {
    char name[PKG_NAME_MAX];
    char target[USH_PATH_MAX];
    const char *rest = "";
    int found = 0;
    int force = 0;
    int ok = 1;

    if (arg == (const char *)0 || ush_split_first_and_rest(arg, name, (u64)sizeof(name), &rest) == 0 ||
        name[0] == '\0') {
        (void)puts("usage: pkg remove [--force] <name>");
        return 0;
    }

    if (ush_streq(name, "--force") != 0 || ush_streq(name, "-f") != 0) {
        force = 1;
        if (rest == (const char *)0 || ush_split_first_and_rest(rest, name, (u64)sizeof(name), &rest) == 0 ||
            name[0] == '\0') {
            (void)puts("usage: pkg remove [--force] <name>");
            return 0;
        }
    }

    if (pkg_safe_name(name) == 0) {
        (void)puts("pkg: invalid package name");
        return 0;
    }

    if (rest != (const char *)0 && rest[0] != '\0') {
        (void)puts("pkg: remove accepts one package name");
        return 0;
    }

    if (pkg_lock_acquire() == 0) {
        return 0;
    }

    if (force == 0 && pkg_remove_has_reverse_dependencies(name) != 0) {
        pkg_lock_release();
        return 0;
    }

    if (pkg_remove_record(name, target, (u64)sizeof(target), &found) == 0) {
        (void)puts("pkg: failed to update installed.db");
        pkg_lock_release();
        return 0;
    }

    if (found == 0) {
        if (pkg_default_target(name, target, (u64)sizeof(target)) == 0) {
            (void)puts("pkg: package not installed");
            pkg_lock_release();
            return 0;
        }
    }

    if (target[0] != '\0' && cleonos_sys_fs_stat_type(target) == 1ULL) {
        if (cleonos_sys_fs_remove(target) == 0ULL) {
            (void)printf("pkg: failed to remove %s\n", target);
            ok = 0;
        } else {
            (void)printf("pkg: removed %s\n", target);
        }
    } else if (found == 0) {
        (void)puts("pkg: package not installed");
        ok = 0;
    }

    pkg_lock_release();
    return ok;
}

static int pkg_repo_set_active(const char *url) {
    char repo[PKG_URL_MAX];

    if (pkg_ensure_db_dir() == 0) {
        (void)puts("pkg: cannot create /system/databases/pkg");
        return 0;
    }

    pkg_copy_trimmed(repo, (u64)sizeof(repo), url);
    if (repo[0] == '\0' || (pkg_is_url(repo) == 0 && strstr(repo, "%s") == (char *)0)) {
        (void)puts("pkg: repo must be http(s) URL or URL template containing %s");
        return 0;
    }

    if (pkg_sqlite_set_active_repo(repo) == 0) {
        (void)puts("pkg: repo write failed");
        return 0;
    }

    (void)printf("pkg: repo set to %s\n", repo);
    return 1;
}

static int pkg_source_find(const char *name, char *out_url, u64 out_url_size) {
    return pkg_sqlite_source_get(name, out_url, out_url_size);
}

static int pkg_source_write_or_remove(const char *target_name, const char *new_url, int remove) {
    if (target_name == (const char *)0 || pkg_safe_name(target_name) == 0) {
        return 0;
    }
    if (pkg_ensure_db_dir() == 0) {
        (void)puts("pkg: cannot create /system/databases/pkg");
        return 0;
    }

    if (remove != 0) {
        return pkg_sqlite_source_remove(target_name);
    }

    return pkg_sqlite_source_set(target_name, new_url);
}

static int pkg_source_list_iter(const pkg_source_record *record, void *ctx_ptr) {
    pkg_source_list_ctx *ctx = (pkg_source_list_ctx *)ctx_ptr;

    if (record == (const pkg_source_record *)0 || ctx == (pkg_source_list_ctx *)0) {
        return 0;
    }

    (void)printf("%-18s %-7s %s\n", record->name,
                 (ctx->active != (const char *)0 && ush_streq(record->url, ctx->active) != 0) ? "*" : "", record->url);
    ctx->any = 1;
    return 1;
}

static int pkg_source_list(void) {
    char active[PKG_URL_MAX];
    pkg_source_list_ctx ctx;

    if (pkg_load_repo(active, (u64)sizeof(active)) == 0) {
        active[0] = '\0';
    }

    ush_zero(&ctx, (u64)sizeof(ctx));
    ctx.active = active;
    if (pkg_sqlite_init() == 0) {
        (void)puts("pkg: cannot open package database");
        return 0;
    }

    (void)puts("name               active  url");
    (void)puts("------------------------------------------------------------");
    if (pkg_sqlite_foreach_sources(pkg_source_list_iter, &ctx) == 0) {
        (void)puts("pkg: failed to read sources database");
        return 0;
    }

    if (ctx.any == 0) {
        (void)puts("pkg: no named sources");
    }
    return 1;
}

int pkg_cmd_repo(const char *arg) {
    char subcmd[32];
    const char *rest = "";

    if (arg == (const char *)0 || arg[0] == '\0') {
        char repo[PKG_URL_MAX];
        if (pkg_load_repo(repo, (u64)sizeof(repo)) == 0) {
            return 0;
        }
        (void)printf("pkg: repo %s\n", repo);
        return 1;
    }

    if (ush_split_first_and_rest(arg, subcmd, (u64)sizeof(subcmd), &rest) == 0 || subcmd[0] == '\0') {
        (void)puts("usage: pkg repo [url] | list | add <name> <url> | use <name> | remove <name>");
        return 0;
    }

    if (pkg_is_url(subcmd) != 0 || strstr(subcmd, "%s") != (char *)0) {
        if (rest != (const char *)0 && rest[0] != '\0') {
            (void)puts("pkg: repo URL does not accept extra arguments");
            return 0;
        }
        return pkg_repo_set_active(subcmd);
    }

    if (ush_streq(subcmd, "list") != 0 || ush_streq(subcmd, "ls") != 0) {
        if (rest != (const char *)0 && rest[0] != '\0') {
            (void)puts("pkg: repo list does not accept arguments");
            return 0;
        }
        return pkg_source_list();
    }

    if (ush_streq(subcmd, "add") != 0) {
        char name[PKG_NAME_MAX];
        char url[PKG_URL_MAX];
        const char *tail = "";

        if (rest == (const char *)0 ||
            ush_split_first_and_rest(rest, name, (u64)sizeof(name), &rest) == 0 ||
            ush_split_first_and_rest(rest, url, (u64)sizeof(url), &tail) == 0 || pkg_safe_name(name) == 0 ||
            url[0] == '\0') {
            (void)puts("usage: pkg repo add <name> <url>");
            return 0;
        }
        if (tail != (const char *)0 && tail[0] != '\0') {
            (void)puts("pkg: repo add accepts name and url");
            return 0;
        }
        if (pkg_is_url(url) == 0 && strstr(url, "%s") == (char *)0) {
            (void)puts("pkg: repo URL must be http(s) URL or URL template containing %s");
            return 0;
        }
        if (pkg_source_write_or_remove(name, url, 0) == 0) {
            (void)puts("pkg: repo source write failed");
            return 0;
        }
        (void)printf("pkg: repo source %s set to %s\n", name, url);
        return 1;
    }

    if (ush_streq(subcmd, "use") != 0) {
        char name[PKG_NAME_MAX];
        char url[PKG_URL_MAX];
        const char *tail = "";

        if (rest == (const char *)0 ||
            ush_split_first_and_rest(rest, name, (u64)sizeof(name), &tail) == 0 || pkg_safe_name(name) == 0 ||
            (tail != (const char *)0 && tail[0] != '\0')) {
            (void)puts("usage: pkg repo use <name>");
            return 0;
        }
        if (pkg_source_find(name, url, (u64)sizeof(url)) == 0) {
            (void)puts("pkg: repo source not found");
            return 0;
        }
        if (pkg_repo_set_active(url) == 0) {
            return 0;
        }
        (void)printf("pkg: repo source %s active\n", name);
        return 1;
    }

    if (ush_streq(subcmd, "remove") != 0 || ush_streq(subcmd, "rm") != 0) {
        char name[PKG_NAME_MAX];
        char url[PKG_URL_MAX];
        char active[PKG_URL_MAX];
        const char *tail = "";

        if (rest == (const char *)0 ||
            ush_split_first_and_rest(rest, name, (u64)sizeof(name), &tail) == 0 || pkg_safe_name(name) == 0 ||
            (tail != (const char *)0 && tail[0] != '\0')) {
            (void)puts("usage: pkg repo remove <name>");
            return 0;
        }
        if (pkg_source_find(name, url, (u64)sizeof(url)) == 0) {
            (void)puts("pkg: repo source not found");
            return 0;
        }
        if (pkg_source_write_or_remove(name, (const char *)0, 1) == 0) {
            (void)puts("pkg: repo source remove failed");
            return 0;
        }
        if (pkg_load_repo(active, (u64)sizeof(active)) != 0 && ush_streq(active, url) != 0) {
            (void)pkg_sqlite_set_active_repo(PKG_DEFAULT_REPO);
            (void)puts("pkg: removed active repo source; repo reset to default");
        }
        (void)printf("pkg: repo source %s removed\n", name);
        return 1;
    }

    (void)puts("usage: pkg repo [url] | list | add <name> <url> | use <name> | remove <name>");
    return 0;
}

int pkg_cmd_source(const char *arg) {
    return pkg_cmd_repo(arg);
}

static int pkg_remove_if_file(const char *path) {
    if (path == (const char *)0 || path[0] == '\0') {
        return 0;
    }
    if (cleonos_sys_fs_stat_type(path) != 1ULL) {
        return 0;
    }
    if (cleonos_sys_fs_remove(path) == 0ULL) {
        (void)printf("pkg: failed to remove %s\n", path);
        return 0;
    }
    (void)printf("pkg: removed %s\n", path);
    return 1;
}

static int pkg_lock_is_active(void) {
    char old_lock[64];
    char *trimmed;
    u64 len = 0ULL;
    u64 pid = 0ULL;
    cleonos_proc_snapshot snap;

    if (pkg_read_file(PKG_LOCK_PATH, old_lock, (u64)sizeof(old_lock), &len) == 0 || len == 0ULL) {
        return 0;
    }
    trimmed = pkg_trim_mut(old_lock);
    if (ush_parse_u64_dec(trimmed, &pid) == 0 || pid == 0ULL) {
        return 0;
    }
    if (cleonos_sys_proc_snapshot(pid, &snap, (u64)sizeof(snap)) == 0ULL) {
        return 0;
    }
    return (snap.state != CLEONOS_PROC_STATE_EXITED && snap.state != CLEONOS_PROC_STATE_UNUSED) ? 1 : 0;
}

int pkg_cmd_clean(void) {
    int removed = 0;

    if (pkg_lock_is_active() != 0) {
        (void)puts("pkg: lock is active; refusing to clean pkg temp files");
        return 0;
    }

    removed += pkg_remove_if_file(PKG_TMP_MANIFEST);
    removed += pkg_remove_if_file(PKG_TMP_ELF);
    removed += pkg_remove_if_file(PKG_TMP_API);
    removed += pkg_remove_if_file(USH_CMD_CTX_PATH);
    removed += pkg_remove_if_file(USH_CMD_RET_PATH);
    removed += pkg_remove_if_file(PKG_LOCK_PATH);

    if (removed == 0) {
        (void)puts("pkg: nothing to clean");
    }
    return 1;
}

int pkg_cmd_info(const char *arg) {
    char name[PKG_NAME_MAX];
    const char *rest = "";
    pkg_installed_record record;

    if (arg == (const char *)0 || ush_split_first_and_rest(arg, name, (u64)sizeof(name), &rest) == 0 ||
        pkg_safe_name(name) == 0) {
        (void)puts("usage: pkg info <name>");
        return 0;
    }

    if (rest != (const char *)0 && rest[0] != '\0') {
        (void)puts("pkg: info accepts exactly one package name");
        return 0;
    }

    if (pkg_sqlite_get_installed_record(name, &record) == 0) {
        (void)puts("pkg: package not installed");
        return 0;
    }

    (void)printf("name: %s\n", record.name);
    (void)printf("version: %s\n", record.version);
    (void)printf("target: %s\n", record.target);
    (void)printf("source: %s\n", record.source);
    if (record.depends[0] != '\0') {
        (void)printf("depends: %s\n", record.depends);
    }
    if (record.sha256[0] != '\0') {
        (void)printf("sha256: %s\n", record.sha256);
    }
    return 1;
}

int pkg_cmd_files(const char *arg) {
    char name[PKG_NAME_MAX];
    const char *rest = "";
    pkg_installed_record record;

    if (arg == (const char *)0 || ush_split_first_and_rest(arg, name, (u64)sizeof(name), &rest) == 0 ||
        pkg_safe_name(name) == 0) {
        (void)puts("usage: pkg files <name>");
        return 0;
    }

    if (rest != (const char *)0 && rest[0] != '\0') {
        (void)puts("pkg: files accepts exactly one package name");
        return 0;
    }

    if (pkg_sqlite_get_installed_record(name, &record) == 0) {
        (void)puts("pkg: package not installed");
        return 0;
    }

    (void)printf("%s %s files:\n", record.name, record.version);
    if (record.target[0] != '\0') {
        (void)printf("  %s", record.target);
        if (cleonos_sys_fs_stat_type(record.target) == 1ULL) {
            (void)printf(" (%llu bytes)", (unsigned long long)cleonos_sys_fs_stat_size(record.target));
        } else {
            (void)printf(" (missing)");
        }
        (void)puts("");
    }
    return 1;
}

void pkg_print_remote_header(void) {
    (void)puts("name               version      category     size       description");
    (void)puts("----------------------------------------------------------------------------");
}

void pkg_print_remote_line(const pkg_remote_package *package) {
    if (package == (const pkg_remote_package *)0) {
        return;
    }

    (void)printf("%-18s %-12s %-12s %-10s %s\n", package->name, package->version, package->category, package->size,
                 package->description);
}

int pkg_cmd_remote_list(void) {
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

int pkg_cmd_remote_info(const char *arg) {
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
    (void)printf("depends: %s\n", package.depends);
    (void)printf("category: %s\n", package.category);
    (void)printf("tags: %s\n", package.tags);
    (void)printf("owner: %s\n", package.owner);
    (void)printf("description: %s\n", package.description);
    if (package.deprecated[0] != '\0') {
        (void)printf("deprecated: %s\n", package.deprecated);
    }
    if (package.sha256[0] != '\0') {
        (void)printf("sha256: %s\n", package.sha256);
    }
    (void)printf("manifest: %s\n", package.manifest_url);
    (void)printf("download: %s\n", package.download_url);
    return 1;
}

int pkg_cmd_remote(const char *arg) {
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

int pkg_cmd_search(const char *arg) {
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

int pkg_cmd_filter_remote(const char *api, const char *label, const char *arg) {
    char query[PKG_URL_MAX];
    const char *cursor;
    pkg_remote_package package;
    u64 len = 0ULL;
    int any = 0;

    if (api == (const char *)0 || label == (const char *)0 || arg == (const char *)0) {
        return 0;
    }

    pkg_copy_trimmed(query, (u64)sizeof(query), arg);
    if (query[0] == '\0') {
        (void)printf("usage: pkg %s <name>\n", label);
        return 0;
    }

    if (pkg_fetch_api(api, "name", query, pkg_text_buf, (u64)sizeof(pkg_text_buf), &len) == 0) {
        return 0;
    }
    (void)len;

    cursor = pkg_json_find_named_array(pkg_text_buf, "packages");
    if (cursor == (const char *)0) {
        pkg_print_api_error_or_default(pkg_text_buf, "pkg: invalid filter API response");
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

int pkg_find_installed_version_in_db(const char *db_text, const char *name, char *out_version, u64 out_size) {
    (void)db_text;
    return pkg_sqlite_get_installed_version(name, out_version, out_size);
}

int pkg_load_installed_db_for_remote(void) {
    u64 count = 0ULL;

    if (pkg_sqlite_count_installed(&count) == 0 || count == 0ULL) {
        return 0;
    }
    return 1;
}

static int pkg_cmd_update_match_remote(const pkg_remote_package *package, void *ctx_ptr) {
    pkg_update_ctx *ctx = (pkg_update_ctx *)ctx_ptr;
    char installed_version[PKG_VERSION_MAX];

    if (package == (const pkg_remote_package *)0 || ctx == (pkg_update_ctx *)0) {
        return 0;
    }

    if (pkg_sqlite_get_installed_version(package->name, installed_version, (u64)sizeof(installed_version)) != 0 &&
        strcmp(installed_version, package->version) != 0) {
        if (ctx->any == 0) {
            (void)puts("name               installed    remote");
            (void)puts("-----------------------------------------------");
        }
        (void)printf("%-18s %-12s %s\n", package->name, installed_version, package->version);
        ctx->any = 1;
    }
    return 1;
}

int pkg_cmd_update(void) {
    const char *cursor;
    pkg_remote_package package;
    u64 len = 0ULL;
    pkg_update_ctx ctx;

    ush_zero(&ctx, (u64)sizeof(ctx));
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
        if (pkg_cmd_update_match_remote(&package, &ctx) == 0) {
            return 0;
        }
    }

    if (ctx.any == 0) {
        (void)puts("pkg: all installed packages are up to date");
    }
    return 1;
}

int pkg_collect_outdated_names(u64 *out_count, int *out_no_installed) {
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
        if (pkg_sqlite_get_installed_version(package.name, installed_version, (u64)sizeof(installed_version)) != 0 &&
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

int pkg_cmd_upgrade(const ush_state *sh, const char *arg) {
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
            if (pkg_lock_acquire() == 0) {
                ok = 0;
                continue;
            }
            pkg_force_reinstall = 1;
            if (pkg_install_repo_package(sh, pkg_upgrade_names[i]) == 0) {
                ok = 0;
            }
            pkg_force_reinstall = 0;
            pkg_lock_release();
        }
        return ok;
    }

    if (pkg_safe_name(name) == 0) {
        (void)puts("pkg: invalid package name");
        return 0;
    }

    if (pkg_load_installed_db_for_remote() == 0 ||
        pkg_sqlite_get_installed_version(name, installed_version, (u64)sizeof(installed_version)) == 0) {
        (void)puts("pkg: package is not installed; use pkg install <name>");
        return 0;
    }

    (void)installed_version;
    if (pkg_lock_acquire() == 0) {
        return 0;
    }
    pkg_force_reinstall = 1;
    ok = pkg_install_repo_package(sh, name);
    pkg_force_reinstall = 0;
    pkg_lock_release();
    return ok;
}

void pkg_usage(void) {
    (void)puts("usage:");
    (void)puts("  pkg install [--dry-run] [--reinstall] <name|file.elf|file.clpkg|url>");
    (void)puts("  pkg reinstall <name>");
    (void)puts("  pkg list");
    (void)puts("  pkg remove [--force] <name>");
    (void)puts("  pkg info <name>");
    (void)puts("  pkg files <name>");
    (void)puts("  pkg repo [url]");
    (void)puts("  pkg repo list");
    (void)puts("  pkg repo add <name> <url>");
    (void)puts("  pkg repo use <name>");
    (void)puts("  pkg repo remove <name>");
    (void)puts("  pkg remote list");
    (void)puts("  pkg remote info <name>");
    (void)puts("  pkg search <keyword>");
    (void)puts("  pkg category <name>");
    (void)puts("  pkg tag <name>");
    (void)puts("  pkg update");
    (void)puts("  pkg upgrade <name>");
    (void)puts("  pkg upgrade --all");
    (void)puts("  pkg doctor");
    (void)puts("  pkg verify [name]");
    (void)puts("  pkg clean");
    (void)puts("");
    (void)puts("manifest keys: name, version, target, url/path/elf, description, depends, category, tags, sha256, deprecated");
}

int pkg_run(const ush_state *sh, const char *arg) {
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
    if (ush_streq(cmd, "reinstall") != 0) {
        char reinstall_arg[PKG_ARG_MAX];
        int n = snprintf(reinstall_arg, (usize)sizeof(reinstall_arg), "--reinstall %s",
                         (rest != (const char *)0) ? rest : "");
        if (n <= 0 || (u64)n >= (u64)sizeof(reinstall_arg)) {
            (void)puts("pkg: reinstall argument too long");
            return 0;
        }
        return pkg_cmd_install(sh, reinstall_arg);
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
    if (ush_streq(cmd, "source") != 0 || ush_streq(cmd, "sources") != 0) {
        return pkg_cmd_source(rest);
    }
    if (ush_streq(cmd, "info") != 0 || ush_streq(cmd, "show") != 0) {
        return pkg_cmd_info(rest);
    }
    if (ush_streq(cmd, "files") != 0) {
        return pkg_cmd_files(rest);
    }
    if (ush_streq(cmd, "remote") != 0) {
        return pkg_cmd_remote(rest);
    }
    if (ush_streq(cmd, "search") != 0 || ush_streq(cmd, "find") != 0) {
        return pkg_cmd_search(rest);
    }
    if (ush_streq(cmd, "category") != 0 || ush_streq(cmd, "cat") != 0) {
        return pkg_cmd_filter_remote("category", "category", rest);
    }
    if (ush_streq(cmd, "tag") != 0) {
        return pkg_cmd_filter_remote("tag", "tag", rest);
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
    if (ush_streq(cmd, "doctor") != 0) {
        if (rest != (const char *)0 && rest[0] != '\0') {
            (void)puts("pkg: doctor does not accept arguments");
            return 0;
        }
        return pkg_cmd_doctor();
    }
    if (ush_streq(cmd, "verify") != 0) {
        return pkg_cmd_verify(rest);
    }
    if (ush_streq(cmd, "clean") != 0) {
        if (rest != (const char *)0 && rest[0] != '\0') {
            (void)puts("pkg: clean does not accept arguments");
            return 0;
        }
        return pkg_cmd_clean();
    }
    if (ush_streq(cmd, "help") != 0) {
        pkg_usage();
        return 1;
    }

    (void)puts("pkg: unknown command");
    pkg_usage();
    return 0;
}
