#include "pkg_internal.h"

int pkg_lock_acquire(void) {
    char old_lock[64];
    char lock_text[64];
    u64 len = 0ULL;
    u64 old_pid = 0ULL;
    int ret;
    cleonos_proc_snapshot snap;

    if (pkg_ensure_db_dir() == 0) {
        (void)puts("pkg: cannot create /system/pkg");
        return 0;
    }

    if (pkg_read_file(PKG_LOCK_PATH, old_lock, (u64)sizeof(old_lock), &len) != 0 && len > 0ULL) {
        char *trimmed = pkg_trim_mut(old_lock);
        if (ush_parse_u64_dec(trimmed, &old_pid) != 0 && old_pid != 0ULL) {
            if (cleonos_sys_proc_snapshot(old_pid, &snap, (u64)sizeof(snap)) != 0ULL &&
                snap.state != CLEONOS_PROC_STATE_EXITED && snap.state != CLEONOS_PROC_STATE_UNUSED) {
                (void)printf("pkg: locked by pid %llu\n", (unsigned long long)old_pid);
                return 0;
            }
        } else {
            (void)puts("pkg: lock exists and is not owned by a valid pid");
            return 0;
        }
    }

    ret = snprintf(lock_text, (usize)sizeof(lock_text), "%llu\n", (unsigned long long)cleonos_sys_getpid());
    if (ret <= 0 || (u64)ret >= (u64)sizeof(lock_text)) {
        return 0;
    }

    if (pkg_write_file(PKG_LOCK_PATH, lock_text, (u64)ret) == 0) {
        (void)puts("pkg: cannot create /system/pkg/lock");
        return 0;
    }

    return 1;
}

void pkg_lock_release(void) {
    (void)cleonos_sys_fs_remove(PKG_LOCK_PATH);
}

void pkg_plan_reset(void) {
    pkg_plan_count = 0ULL;
    ush_zero(pkg_plan_items, (u64)sizeof(pkg_plan_items));
}

static int pkg_plan_has_name(const char *name) {
    u64 i;

    if (name == (const char *)0 || name[0] == '\0') {
        return 0;
    }

    for (i = 0ULL; i < pkg_plan_count; i++) {
        if (ush_streq(pkg_plan_items[i].name, name) != 0) {
            return 1;
        }
    }
    return 0;
}

static void pkg_plan_parse_size(const char *text, pkg_plan_item *item) {
    u64 value = 0ULL;
    u64 i;

    if (item == (pkg_plan_item *)0) {
        return;
    }

    item->size_known = 0;
    item->size_bytes = 0ULL;
    if (text == (const char *)0 || text[0] == '\0') {
        return;
    }

    for (i = 0ULL; text[i] != '\0'; i++) {
        if (text[i] < '0' || text[i] > '9') {
            return;
        }
        value = (value * 10ULL) + (u64)(text[i] - '0');
    }

    item->size_known = 1;
    item->size_bytes = value;
    ush_copy(item->size_text, (u64)sizeof(item->size_text), text);
}

static int pkg_plan_add_item(const pkg_manifest *manifest, const char *source, const char *size_text) {
    pkg_plan_item *item;

    if (manifest == (const pkg_manifest *)0 || manifest->name[0] == '\0') {
        return 0;
    }
    if (pkg_plan_has_name(manifest->name) != 0) {
        return 1;
    }
    if (pkg_plan_count >= (u64)PKG_DRY_RUN_MAX) {
        (void)puts("pkg: dry-run dependency plan too large");
        return 0;
    }

    item = &pkg_plan_items[pkg_plan_count];
    ush_zero(item, (u64)sizeof(*item));
    ush_copy(item->name, (u64)sizeof(item->name), manifest->name);
    ush_copy(item->version, (u64)sizeof(item->version), manifest->version);
    ush_copy(item->target, (u64)sizeof(item->target), manifest->target);
    ush_copy(item->depends, (u64)sizeof(item->depends), manifest->depends);
    ush_copy(item->sha256, (u64)sizeof(item->sha256), manifest->sha256);
    if (manifest->url[0] != '\0') {
        ush_copy(item->source, (u64)sizeof(item->source), manifest->url);
    } else if (manifest->path[0] != '\0') {
        ush_copy(item->source, (u64)sizeof(item->source), manifest->path);
    } else {
        ush_copy(item->source, (u64)sizeof(item->source), (source != (const char *)0) ? source : "");
    }
    item->download = (manifest->url[0] != '\0' || pkg_is_url(item->source) != 0) ? 1 : 0;
    item->overwrite = (manifest->target[0] != '\0' && cleonos_sys_fs_stat_type(manifest->target) == 1ULL) ? 1 : 0;
    pkg_plan_parse_size(size_text, item);

    pkg_plan_count++;
    return 1;
}

static int pkg_plan_dependencies(const ush_state *sh, const char *depends, u64 depth) {
    char list[PKG_DEPENDS_MAX];
    char *item;

    if (depends == (const char *)0 || depends[0] == '\0') {
        return 1;
    }
    if (depth >= (u64)PKG_DEP_DEPTH_MAX) {
        (void)puts("pkg: dependency recursion too deep");
        return 0;
    }

    ush_copy(list, (u64)sizeof(list), depends);
    item = list;
    while (item != (char *)0 && *item != '\0') {
        char *next = strchr(item, ',');
        char *trimmed;
        pkg_dependency dep;

        if (next != (char *)0) {
            *next = '\0';
            next++;
        }

        trimmed = pkg_trim_mut(item);
        if (trimmed[0] != '\0') {
            if (pkg_parse_dependency_spec(trimmed, &dep) == 0) {
                (void)printf("pkg: invalid dependency: %s\n", trimmed);
                return 0;
            }
            if (pkg_installed_dependency_satisfies(&dep) == 0 &&
                pkg_plan_add_repo_package(sh, dep.name, dep.op, dep.version, depth + 1ULL) == 0) {
                return 0;
            }
        }

        item = next;
    }
    return 1;
}

int pkg_plan_add_manifest(const ush_state *sh, pkg_manifest *manifest, const char *source, u64 depth) {
    if (manifest == (pkg_manifest *)0) {
        return 0;
    }
    if (pkg_complete_manifest(sh, manifest, source, pkg_is_url(source)) == 0) {
        (void)puts("pkg: incomplete manifest");
        return 0;
    }
    if (pkg_target_is_allowed(manifest->target) == 0) {
        (void)puts("pkg: invalid install target, only /shell/*.elf is allowed");
        return 0;
    }
    if (pkg_plan_dependencies(sh, manifest->depends, depth) == 0) {
        return 0;
    }
    return pkg_plan_add_item(manifest, source, (const char *)0);
}

int pkg_plan_add_repo_package(const ush_state *sh, const char *name, const char *constraint_op,
                                     const char *constraint_version, u64 depth) {
    pkg_remote_package package;
    char manifest_url[PKG_URL_MAX];
    pkg_manifest manifest;
    u64 len = 0ULL;

    if (pkg_safe_name(name) == 0) {
        (void)puts("pkg: invalid package name");
        return 0;
    }
    if (depth >= (u64)PKG_DEP_DEPTH_MAX) {
        (void)puts("pkg: dependency recursion too deep");
        return 0;
    }
    if (pkg_plan_has_name(name) != 0) {
        return 1;
    }

    if (pkg_fetch_api("info", "name", name, pkg_text_buf, (u64)sizeof(pkg_text_buf), &len) == 0 ||
        pkg_parse_info_package(pkg_text_buf, &package) == 0) {
        pkg_print_api_error_or_default(pkg_text_buf, "pkg: package not found in repository");
        return 0;
    }
    (void)len;

    if (constraint_op != (const char *)0 && constraint_op[0] != '\0' &&
        pkg_version_satisfies(package.version, constraint_op, constraint_version) == 0) {
        (void)printf("pkg: %s remote version %s does not satisfy %s%s\n", name, package.version, constraint_op,
                     (constraint_version != (const char *)0) ? constraint_version : "");
        return 0;
    }

    pkg_manifest_init(&manifest);
    ush_copy(manifest.name, (u64)sizeof(manifest.name), package.name);
    ush_copy(manifest.version, (u64)sizeof(manifest.version), package.version);
    ush_copy(manifest.target, (u64)sizeof(manifest.target), package.target);
    ush_copy(manifest.url, (u64)sizeof(manifest.url), package.download_url);
    ush_copy(manifest.description, (u64)sizeof(manifest.description), package.description);
    ush_copy(manifest.depends, (u64)sizeof(manifest.depends), package.depends);
    ush_copy(manifest.category, (u64)sizeof(manifest.category), package.category);
    ush_copy(manifest.tags, (u64)sizeof(manifest.tags), package.tags);
    ush_copy(manifest.sha256, (u64)sizeof(manifest.sha256), package.sha256);
    ush_copy(manifest.deprecated, (u64)sizeof(manifest.deprecated), package.deprecated);
    ush_copy(manifest_url, (u64)sizeof(manifest_url), package.manifest_url);

    if (pkg_complete_manifest(sh, &manifest, manifest_url, 1) == 0) {
        (void)puts("pkg: incomplete remote manifest");
        return 0;
    }
    if (manifest.deprecated[0] != '\0') {
        (void)printf("pkg: warning: package %s is deprecated: %s\n", manifest.name, manifest.deprecated);
    }
    if (pkg_plan_dependencies(sh, manifest.depends, depth) == 0) {
        return 0;
    }
    return pkg_plan_add_item(&manifest, manifest_url, package.size);
}

void pkg_plan_print(void) {
    u64 i;
    u64 total = 0ULL;
    int total_known = 1;

    if (pkg_plan_count == 0ULL) {
        (void)puts("pkg: nothing to install");
        return;
    }

    (void)puts("dry-run install plan:");
    (void)puts("name               version      target                 size       action");
    (void)puts("----------------------------------------------------------------------------");
    for (i = 0ULL; i < pkg_plan_count; i++) {
        const pkg_plan_item *item = &pkg_plan_items[i];
        const char *size = item->size_known != 0 ? item->size_text : "unknown";
        const char *action = item->overwrite != 0 ? "overwrite" : "install";

        if (item->size_known != 0) {
            total += item->size_bytes;
        } else if (item->download != 0) {
            total_known = 0;
        }
        (void)printf("%-18s %-12s %-22s %-10s %s\n", item->name, item->version, item->target, size, action);
        if (item->depends[0] != '\0') {
            (void)printf("  depends: %s\n", item->depends);
        }
        if (item->sha256[0] != '\0') {
            (void)printf("  sha256: %s\n", item->sha256);
        }
        if (item->source[0] != '\0') {
            (void)printf("  source: %s\n", item->source);
        }
    }

    if (total_known != 0) {
        (void)printf("total download size: %llu bytes\n", (unsigned long long)total);
    } else {
        (void)printf("known download size: %llu bytes + unknown\n", (unsigned long long)total);
    }
}

int pkg_cmd_install_dry_run(const ush_state *sh, const char *arg) {
    char first[PKG_URL_MAX];
    const char *rest = "";

    if (sh == (const ush_state *)0 || arg == (const char *)0 ||
        ush_split_first_and_rest(arg, first, (u64)sizeof(first), &rest) == 0 || first[0] == '\0') {
        (void)puts("usage: pkg install --dry-run <name|file.elf|file.clpkg|url>");
        return 0;
    }
    if (rest != (const char *)0 && rest[0] != '\0') {
        (void)puts("pkg: dry-run accepts exactly one package source");
        return 0;
    }

    pkg_plan_reset();
    if (pkg_is_url(first) != 0) {
        if (pkg_has_suffix(first, ".elf") != 0) {
            pkg_manifest manifest;
            pkg_manifest_init(&manifest);
            pkg_basename_no_ext(first, manifest.name, (u64)sizeof(manifest.name));
            if (pkg_safe_name(manifest.name) == 0 ||
                pkg_default_target(manifest.name, manifest.target, (u64)sizeof(manifest.target)) == 0) {
                (void)puts("pkg: cannot infer package name from URL");
                return 0;
            }
            ush_copy(manifest.version, (u64)sizeof(manifest.version), "remote");
            ush_copy(manifest.url, (u64)sizeof(manifest.url), first);
            if (pkg_plan_add_manifest(sh, &manifest, first, 0ULL) == 0) {
                return 0;
            }
            pkg_plan_print();
            return 1;
        }

        (void)printf("pkg: download manifest %s\n", first);
        if (pkg_download_to(first, PKG_TMP_MANIFEST) == 0) {
            return 0;
        }
        if (pkg_read_file(PKG_TMP_MANIFEST, pkg_text_buf, (u64)sizeof(pkg_text_buf), (u64 *)0) == 0) {
            (void)puts("pkg: manifest read failed");
            return 0;
        }
        {
            pkg_manifest manifest;
            if (pkg_parse_manifest(pkg_text_buf, &manifest) == 0 ||
                pkg_plan_add_manifest(sh, &manifest, first, 0ULL) == 0) {
                (void)puts("pkg: invalid manifest");
                return 0;
            }
        }
        pkg_plan_print();
        return 1;
    }

    if (pkg_has_suffix(first, ".elf") != 0) {
        pkg_manifest manifest;
        char abs_path[USH_PATH_MAX];
        pkg_manifest_init(&manifest);
        if (pkg_resolve_local_path(sh, first, abs_path, (u64)sizeof(abs_path)) == 0) {
            (void)puts("pkg: invalid local path");
            return 0;
        }
        pkg_basename_no_ext(abs_path, manifest.name, (u64)sizeof(manifest.name));
        if (pkg_safe_name(manifest.name) == 0 ||
            pkg_default_target(manifest.name, manifest.target, (u64)sizeof(manifest.target)) == 0) {
            (void)puts("pkg: cannot infer package name");
            return 0;
        }
        ush_copy(manifest.version, (u64)sizeof(manifest.version), "local");
        ush_copy(manifest.path, (u64)sizeof(manifest.path), abs_path);
        if (pkg_plan_add_manifest(sh, &manifest, abs_path, 0ULL) == 0) {
            return 0;
        }
        pkg_plan_print();
        return 1;
    }

    if (pkg_has_suffix(first, ".clpkg") != 0) {
        char manifest_abs[USH_PATH_MAX];
        if (pkg_resolve_local_path(sh, first, manifest_abs, (u64)sizeof(manifest_abs)) == 0 ||
            pkg_read_file(manifest_abs, pkg_text_buf, (u64)sizeof(pkg_text_buf), (u64 *)0) == 0) {
            (void)puts("pkg: manifest read failed");
            return 0;
        }
        {
            pkg_manifest manifest;
            if (pkg_parse_manifest(pkg_text_buf, &manifest) == 0 ||
                pkg_plan_add_manifest(sh, &manifest, manifest_abs, 0ULL) == 0) {
                (void)puts("pkg: invalid manifest");
                return 0;
            }
        }
        pkg_plan_print();
        return 1;
    }

    {
        pkg_dependency dep;
        if (pkg_parse_dependency_spec(first, &dep) != 0 && dep.op[0] != '\0') {
            if (pkg_plan_add_repo_package(sh, dep.name, dep.op, dep.version, 0ULL) == 0) {
                return 0;
            }
            pkg_plan_print();
            return 1;
        }
    }

    if (pkg_safe_name(first) != 0) {
        if (pkg_plan_add_repo_package(sh, first, (const char *)0, (const char *)0, 0ULL) == 0) {
            return 0;
        }
        pkg_plan_print();
        return 1;
    }

    (void)puts("pkg: invalid package source");
    return 0;
}

static void pkg_doctor_result(const char *name, int ok, const char *detail) {
    (void)printf("%-28s %s", name, ok != 0 ? "OK" : "FAIL");
    if (detail != (const char *)0 && detail[0] != '\0') {
        (void)printf(" - %s", detail);
    }
    (void)puts("");
}

int pkg_cmd_doctor(void) {
    char repo[PKG_URL_MAX];
    char mount_path[USH_PATH_MAX];
    u64 len = 0ULL;
    int ok_all = 1;
    int ok;

    (void)puts("pkg doctor:");

    ok = (cleonos_sys_net_available() != 0ULL) ? 1 : 0;
    pkg_doctor_result("network", ok, ok != 0 ? "net stack available" : "net stack unavailable");
    if (ok == 0) {
        ok_all = 0;
    }

    ok = (pkg_load_repo(repo, (u64)sizeof(repo)) != 0 && repo[0] != '\0') ? 1 : 0;
    pkg_doctor_result("repo config", ok, ok != 0 ? repo : "repo unavailable");
    if (ok == 0) {
        ok_all = 0;
    }

    ok = (pkg_fetch_api("list", (const char *)0, (const char *)0, pkg_text_buf, (u64)sizeof(pkg_text_buf), &len) != 0 &&
          pkg_json_find_named_array(pkg_text_buf, "packages") != (const char *)0)
             ? 1
             : 0;
    pkg_doctor_result("repo API", ok, ok != 0 ? "?api=list readable" : "cannot read valid ?api=list");
    if (ok == 0) {
        ok_all = 0;
    }

    ok = (pkg_ensure_db_dir() != 0 && cleonos_sys_fs_stat_type(PKG_DB_DIR) == 2ULL) ? 1 : 0;
    pkg_doctor_result("/system/pkg", ok, ok != 0 ? "directory exists" : "not writable or missing");
    if (ok == 0) {
        ok_all = 0;
    }

    ok = pkg_write_probe("/system/pkg/.doctor.tmp");
    pkg_doctor_result("/system/pkg writable", ok, ok != 0 ? "write probe passed" : "write probe failed");
    if (ok == 0) {
        ok_all = 0;
    }

    ok = pkg_write_probe("/shell/.pkg_doctor.tmp");
    pkg_doctor_result("/shell writable", ok, ok != 0 ? "write probe passed" : "write probe failed");
    if (ok == 0) {
        ok_all = 0;
    }

    if (pkg_read_file(PKG_DB_PATH, pkg_db_buf, (u64)sizeof(pkg_db_buf), &len) == 0 || len == 0ULL) {
        pkg_doctor_result("installed.db", 1, "empty or not created yet");
    } else {
        char *line = pkg_db_buf;
        ok = 1;
        while (*line != '\0') {
            char *next = line;
            char copy[PKG_DB_LINE_MAX];
            char *name;
            char *version;
            char *target;

            while (*next != '\0' && *next != '\n') {
                next++;
            }
            if (*next == '\n') {
                *next = '\0';
                next++;
            }
            if (line[0] != '\0') {
                ush_copy(copy, (u64)sizeof(copy), line);
                if (pkg_db_line_parse_ex(copy, &name, &version, &target, (char **)0, (char **)0, (char **)0) == 0 ||
                    pkg_safe_name(name) == 0 || version[0] == '\0' || pkg_target_is_allowed(target) == 0) {
                    ok = 0;
                    break;
                }
            }
            line = next;
        }
        pkg_doctor_result("installed.db", ok, ok != 0 ? "parseable" : "corrupted record found");
        if (ok == 0) {
            ok_all = 0;
        }
    }

    if (cleonos_sys_disk_present() != 0ULL) {
        int mounted = (cleonos_sys_disk_mounted() != 0ULL) ? 1 : 0;
        (void)printf("%-28s OK - %llu bytes, mounted=%s", "disk", (unsigned long long)cleonos_sys_disk_size_bytes(),
                     mounted != 0 ? "yes" : "no");
        if (mounted != 0 && cleonos_sys_disk_mount_path(mount_path, (u64)sizeof(mount_path)) != 0ULL) {
            (void)printf(", path=%s", mount_path);
        }
        (void)puts("");
    } else {
        pkg_doctor_result("disk", 0, "not present");
        ok_all = 0;
    }

    return ok_all;
}

static int pkg_verify_one_record(char *record, const char *filter, int *io_any, int *io_ok_all) {
    char *name;
    char *version;
    char *target;
    char *source;
    char *depends;
    char *sha256;
    char actual[PKG_SHA256_MAX];
    int ok = 1;

    if (pkg_db_line_parse_ex(record, &name, &version, &target, &source, &depends, &sha256) == 0) {
        (void)puts("pkg verify: corrupted installed.db record");
        *io_ok_all = 0;
        return 1;
    }
    (void)version;
    (void)source;
    (void)depends;

    if (filter != (const char *)0 && filter[0] != '\0' && ush_streq(name, filter) == 0) {
        return 1;
    }
    *io_any = 1;

    (void)printf("%s:\n", name);
    if (pkg_target_is_allowed(target) == 0) {
        (void)printf("  target: FAIL invalid target %s\n", target);
        ok = 0;
    } else {
        (void)printf("  target: %s\n", target);
    }

    if (cleonos_sys_fs_stat_type(target) != 1ULL) {
        (void)puts("  file: FAIL missing");
        ok = 0;
    } else {
        (void)printf("  file: OK %llu bytes\n", (unsigned long long)cleonos_sys_fs_stat_size(target));
    }

    if (sha256 == (char *)0 || sha256[0] == '\0') {
        (void)puts("  sha256: WARN no checksum recorded");
    } else if (pkg_hex_digest_is_valid(sha256) == 0) {
        (void)puts("  sha256: FAIL invalid checksum in installed.db");
        ok = 0;
    } else if (pkg_sha256_file_hex(target, actual) == 0) {
        (void)puts("  sha256: FAIL cannot calculate");
        ok = 0;
    } else if (pkg_hex_digest_equals(sha256, actual) == 0) {
        (void)printf("  sha256: FAIL expected %s\n", sha256);
        (void)printf("          actual   %s\n", actual);
        ok = 0;
    } else {
        (void)puts("  sha256: OK");
    }

    if (ok == 0) {
        *io_ok_all = 0;
    }
    return 1;
}

int pkg_cmd_verify(const char *arg) {
    char name[PKG_NAME_MAX];
    const char *rest = "";
    u64 len = 0ULL;
    char *line;
    int any = 0;
    int ok_all = 1;

    name[0] = '\0';
    if (arg != (const char *)0 && arg[0] != '\0') {
        if (ush_split_first_and_rest(arg, name, (u64)sizeof(name), &rest) == 0 || pkg_safe_name(name) == 0 ||
            (rest != (const char *)0 && rest[0] != '\0')) {
            (void)puts("usage: pkg verify [name]");
            return 0;
        }
    }

    if (pkg_read_file(PKG_DB_PATH, pkg_db_buf, (u64)sizeof(pkg_db_buf), &len) == 0 || len == 0ULL) {
        (void)puts("pkg: no packages installed");
        return 0;
    }

    line = pkg_db_buf;
    while (*line != '\0') {
        char *next = line;
        char copy[PKG_DB_LINE_MAX];

        while (*next != '\0' && *next != '\n') {
            next++;
        }
        if (*next == '\n') {
            *next = '\0';
            next++;
        }
        if (line[0] != '\0') {
            ush_copy(copy, (u64)sizeof(copy), line);
            if (pkg_verify_one_record(copy, name, &any, &ok_all) == 0) {
                ok_all = 0;
            }
        }
        line = next;
    }

    if (any == 0) {
        (void)puts("pkg: package not installed");
        return 0;
    }
    return ok_all;
}
