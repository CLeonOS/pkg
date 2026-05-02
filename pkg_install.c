#include "pkg_internal.h"

int pkg_char_is_digit(char ch) {
    return (ch >= '0' && ch <= '9') ? 1 : 0;
}

char pkg_char_lower(char ch) {
    if (ch >= 'A' && ch <= 'Z') {
        return (char)(ch - 'A' + 'a');
    }
    return ch;
}

int pkg_version_compare(const char *left, const char *right) {
    u64 li = 0ULL;
    u64 ri = 0ULL;

    if (left == (const char *)0) {
        left = "";
    }
    if (right == (const char *)0) {
        right = "";
    }

    while (left[li] != '\0' || right[ri] != '\0') {
        if (pkg_char_is_digit(left[li]) != 0 && pkg_char_is_digit(right[ri]) != 0) {
            u64 lz = li;
            u64 rz = ri;
            u64 lend;
            u64 rend;
            u64 llen;
            u64 rlen;
            u64 off;

            while (left[lz] == '0') {
                lz++;
            }
            while (right[rz] == '0') {
                rz++;
            }
            lend = lz;
            rend = rz;
            while (pkg_char_is_digit(left[lend]) != 0) {
                lend++;
            }
            while (pkg_char_is_digit(right[rend]) != 0) {
                rend++;
            }
            llen = lend - lz;
            rlen = rend - rz;
            if (llen != rlen) {
                return (llen > rlen) ? 1 : -1;
            }
            for (off = 0ULL; off < llen; off++) {
                if (left[lz + off] != right[rz + off]) {
                    return (left[lz + off] > right[rz + off]) ? 1 : -1;
                }
            }
            li = lend;
            ri = rend;
            continue;
        }

        if (pkg_char_lower(left[li]) != pkg_char_lower(right[ri])) {
            return (pkg_char_lower(left[li]) > pkg_char_lower(right[ri])) ? 1 : -1;
        }
        if (left[li] != '\0') {
            li++;
        }
        if (right[ri] != '\0') {
            ri++;
        }
    }

    return 0;
}

int pkg_version_satisfies(const char *version, const char *op, const char *required) {
    int cmp;

    if (op == (const char *)0 || op[0] == '\0') {
        return 1;
    }
    if (version == (const char *)0 || version[0] == '\0' || required == (const char *)0 || required[0] == '\0') {
        return 0;
    }

    cmp = pkg_version_compare(version, required);
    if (ush_streq(op, "=") != 0 || ush_streq(op, "==") != 0) {
        return (cmp == 0) ? 1 : 0;
    }
    if (ush_streq(op, "!=") != 0) {
        return (cmp != 0) ? 1 : 0;
    }
    if (ush_streq(op, ">") != 0) {
        return (cmp > 0) ? 1 : 0;
    }
    if (ush_streq(op, ">=") != 0) {
        return (cmp >= 0) ? 1 : 0;
    }
    if (ush_streq(op, "<") != 0) {
        return (cmp < 0) ? 1 : 0;
    }
    if (ush_streq(op, "<=") != 0) {
        return (cmp <= 0) ? 1 : 0;
    }
    return 0;
}

int pkg_parse_dependency_spec(const char *spec, pkg_dependency *out) {
    char tmp[128];
    char *text;
    char *op_pos = (char *)0;
    char *version;
    char op_ch;
    u64 i;

    if (spec == (const char *)0 || out == (pkg_dependency *)0) {
        return 0;
    }

    ush_zero(out, (u64)sizeof(*out));
    ush_copy(tmp, (u64)sizeof(tmp), spec);
    text = pkg_trim_mut(tmp);
    if (text[0] == '\0') {
        return 0;
    }

    for (i = 0ULL; text[i] != '\0'; i++) {
        if (text[i] == '<' || text[i] == '>' || text[i] == '=' || text[i] == '!' || text[i] == '@') {
            op_pos = text + i;
            break;
        }
    }

    if (op_pos == (char *)0) {
        pkg_copy_trimmed(out->name, (u64)sizeof(out->name), text);
        return pkg_safe_name(out->name);
    }

    op_ch = *op_pos;
    *op_pos = '\0';
    pkg_copy_trimmed(out->name, (u64)sizeof(out->name), text);
    if (pkg_safe_name(out->name) == 0) {
        return 0;
    }

    op_pos++;
    if (op_ch == '@') {
        ush_copy(out->op, (u64)sizeof(out->op), "=");
        version = op_pos;
    } else if ((op_ch == '<' || op_ch == '>' || op_ch == '!' || op_ch == '=') && *op_pos == '=') {
        out->op[0] = op_ch;
        out->op[1] = '=';
        out->op[2] = '\0';
        version = op_pos + 1;
    } else if (op_ch == '<' || op_ch == '>' || op_ch == '=') {
        out->op[0] = (op_ch == '=') ? '=' : op_ch;
        out->op[1] = '\0';
        version = op_pos;
    } else {
        return 0;
    }

    pkg_copy_trimmed(out->version, (u64)sizeof(out->version), version);
    return (pkg_safe_version_text(out->version) != 0) ? 1 : 0;
}

int pkg_find_installed_version_text(const char *db_text, const char *name, char *out_version, u64 out_size) {
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
        char copy[PKG_DB_LINE_MAX];
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

int pkg_installed_dependency_satisfies(const pkg_dependency *dep) {
    u64 len = 0ULL;
    char installed_version[PKG_VERSION_MAX];

    if (dep == (const pkg_dependency *)0 || pkg_safe_name(dep->name) == 0) {
        return 0;
    }

    if (pkg_read_file(PKG_DB_PATH, pkg_db_buf, (u64)sizeof(pkg_db_buf), &len) == 0 || len == 0ULL) {
        return 0;
    }

    if (pkg_find_installed_version_text(pkg_db_buf, dep->name, installed_version, (u64)sizeof(installed_version)) == 0) {
        return 0;
    }

    return pkg_version_satisfies(installed_version, dep->op, dep->version);
}

int pkg_dependency_list_mentions(const char *depends, const char *name) {
    char list[PKG_DEPENDS_MAX];
    char *item;

    if (depends == (const char *)0 || depends[0] == '\0' || name == (const char *)0) {
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
        if (trimmed[0] != '\0' && pkg_parse_dependency_spec(trimmed, &dep) != 0 && ush_streq(dep.name, name) != 0) {
            return 1;
        }

        item = next;
    }

    return 0;
}

int pkg_remote_dependency_can_satisfy(const pkg_dependency *dep) {
    pkg_remote_package package;
    u64 len = 0ULL;

    if (dep == (const pkg_dependency *)0) {
        return 0;
    }

    if (pkg_fetch_api("info", "name", dep->name, pkg_text_buf, (u64)sizeof(pkg_text_buf), &len) == 0) {
        return 0;
    }
    (void)len;

    if (pkg_parse_info_package(pkg_text_buf, &package) == 0) {
        pkg_print_api_error_or_default(pkg_text_buf, "pkg: dependency not found in repository");
        return 0;
    }

    if (pkg_version_satisfies(package.version, dep->op, dep->version) == 0) {
        if (dep->op[0] != '\0') {
            (void)printf("pkg: dependency %s remote version %s does not satisfy %s%s\n", dep->name, package.version,
                         dep->op, dep->version);
        } else {
            (void)printf("pkg: dependency %s has invalid remote version\n", dep->name);
        }
        return 0;
    }

    return 1;
}

int pkg_db_line_parse_ex(char *line, char **out_name, char **out_version, char **out_target, char **out_source,
                                char **out_depends, char **out_sha256) {
    char *version;
    char *target;
    char *source;
    char *depends;
    char *sha256;

    if (out_name != (char **)0) {
        *out_name = (char *)0;
    }
    if (out_version != (char **)0) {
        *out_version = (char *)0;
    }
    if (out_target != (char **)0) {
        *out_target = (char *)0;
    }
    if (out_source != (char **)0) {
        *out_source = (char *)0;
    }
    if (out_depends != (char **)0) {
        *out_depends = (char *)0;
    }
    if (out_sha256 != (char **)0) {
        *out_sha256 = (char *)0;
    }

    if (line == (char *)0 || line[0] == '\0') {
        return 0;
    }

    version = strchr(line, '|');
    if (version == (char *)0) {
        return 0;
    }
    *version = '\0';
    version++;

    target = strchr(version, '|');
    if (target == (char *)0) {
        return 0;
    }
    *target = '\0';
    target++;

    source = strchr(target, '|');
    if (source != (char *)0) {
        *source = '\0';
        source++;
        depends = strchr(source, '|');
        if (depends != (char *)0) {
            *depends = '\0';
            depends++;
            sha256 = strchr(depends, '|');
            if (sha256 != (char *)0) {
                *sha256 = '\0';
                sha256++;
            }
        } else {
            sha256 = (char *)0;
        }
    } else {
        depends = (char *)0;
        sha256 = (char *)0;
    }

    if (out_name != (char **)0) {
        *out_name = line;
    }
    if (out_version != (char **)0) {
        *out_version = version;
    }
    if (out_target != (char **)0) {
        *out_target = target;
    }
    if (out_source != (char **)0) {
        *out_source = (source != (char *)0) ? source : "";
    }
    if (out_depends != (char **)0) {
        *out_depends = (depends != (char *)0) ? depends : "";
    }
    if (out_sha256 != (char **)0) {
        *out_sha256 = (sha256 != (char *)0) ? sha256 : "";
    }
    return 1;
}

int pkg_db_line_parse(char *line, char **out_name, char **out_version, char **out_target, char **out_source,
                             char **out_depends) {
    return pkg_db_line_parse_ex(line, out_name, out_version, out_target, out_source, out_depends, (char **)0);
}

int pkg_install_dependency_list(const ush_state *sh, const char *depends, u64 depth) {
    char list[PKG_DEPENDS_MAX];
    char *item;

    if (sh == (const ush_state *)0 || depends == (const char *)0 || depends[0] == '\0') {
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
            if (pkg_installed_dependency_satisfies(&dep) == 0) {
                if (dep.op[0] != '\0') {
                    (void)printf("pkg: install dependency %s%s%s\n", dep.name, dep.op, dep.version);
                } else {
                    (void)printf("pkg: install dependency %s\n", dep.name);
                }
                if (pkg_remote_dependency_can_satisfy(&dep) == 0 ||
                    pkg_install_repo_package_with_depth(sh, dep.name, dep.op, dep.version, depth + 1ULL) == 0) {
                    return 0;
                }
            }
        }

        item = next;
    }

    return 1;
}

int pkg_resolve_local_path(const ush_state *sh, const char *arg, char *out, u64 out_size) {
    if (sh == (const ush_state *)0 || arg == (const char *)0 || out == (char *)0 || out_size == 0ULL) {
        return 0;
    }

    if (arg[0] == '/') {
        ush_copy(out, out_size, arg);
        return out[0] != '\0';
    }

    return ush_resolve_path(sh, arg, out, out_size);
}

int pkg_record_install(const pkg_manifest *manifest, const char *source) {
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
        char line_copy[PKG_DB_LINE_MAX];
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
        pkg_append_char(pkg_db_new_buf, (u64)sizeof(pkg_db_new_buf), &new_len, '|') == 0 ||
        pkg_append_text(pkg_db_new_buf, (u64)sizeof(pkg_db_new_buf), &new_len, manifest->depends) == 0 ||
        pkg_append_char(pkg_db_new_buf, (u64)sizeof(pkg_db_new_buf), &new_len, '|') == 0 ||
        pkg_append_text(pkg_db_new_buf, (u64)sizeof(pkg_db_new_buf), &new_len, manifest->sha256) == 0 ||
        pkg_append_char(pkg_db_new_buf, (u64)sizeof(pkg_db_new_buf), &new_len, '\n') == 0) {
        return 0;
    }

    return pkg_write_file(PKG_DB_PATH, pkg_db_new_buf, new_len);
}

int pkg_install_elf_file(const pkg_manifest *manifest, const char *elf_path, const char *source) {
    char actual_sha256[PKG_SHA256_MAX];

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

    if (manifest->sha256[0] != '\0') {
        if (pkg_hex_digest_is_valid(manifest->sha256) == 0) {
            (void)puts("pkg: invalid manifest sha256");
            return 0;
        }
        if (pkg_sha256_file_hex(elf_path, actual_sha256) == 0) {
            (void)puts("pkg: sha256 calculation failed");
            return 0;
        }
        if (pkg_hex_digest_equals(manifest->sha256, actual_sha256) == 0) {
            (void)printf("pkg: sha256 mismatch for %s\n", manifest->name);
            (void)printf("pkg: expected %s\n", manifest->sha256);
            (void)printf("pkg: actual   %s\n", actual_sha256);
            return 0;
        }
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

int pkg_install_local_elf(const ush_state *sh, const char *source_arg) {
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

int pkg_complete_manifest(const ush_state *sh, pkg_manifest *manifest, const char *manifest_source,
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

int pkg_install_manifest_file_with_depth(const ush_state *sh, const char *manifest_path, const char *origin,
                                                int origin_is_url, u64 depth) {
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

    if (manifest.deprecated[0] != '\0') {
        (void)printf("pkg: warning: package %s is deprecated: %s\n", manifest.name, manifest.deprecated);
    }

    if (pkg_install_dependency_list(sh, manifest.depends, depth) == 0) {
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

int pkg_install_manifest_file(const ush_state *sh, const char *manifest_path, const char *origin, int origin_is_url) {
    return pkg_install_manifest_file_with_depth(sh, manifest_path, origin, origin_is_url, 0ULL);
}

int pkg_install_url_elf(const char *url) {
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

int pkg_install_repo_package_with_depth(const ush_state *sh, const char *name, const char *constraint_op,
                                               const char *constraint_version, u64 depth) {
    char repo[PKG_URL_MAX];
    char manifest_url[PKG_URL_MAX];
    pkg_remote_package package;
    u64 api_len = 0ULL;

    if (pkg_safe_name(name) == 0) {
        (void)puts("pkg: invalid package name");
        return 0;
    }

    if (depth >= (u64)PKG_DEP_DEPTH_MAX) {
        (void)puts("pkg: dependency recursion too deep");
        return 0;
    }

    if (constraint_op != (const char *)0 && constraint_op[0] != '\0') {
        if (pkg_fetch_api("info", "name", name, pkg_text_buf, (u64)sizeof(pkg_text_buf), &api_len) == 0 ||
            pkg_parse_info_package(pkg_text_buf, &package) == 0) {
            pkg_print_api_error_or_default(pkg_text_buf, "pkg: package not found in repository");
            return 0;
        }
        (void)api_len;
        if (pkg_version_satisfies(package.version, constraint_op, constraint_version) == 0) {
            (void)printf("pkg: %s remote version %s does not satisfy %s%s\n", name, package.version, constraint_op,
                         (constraint_version != (const char *)0) ? constraint_version : "");
            return 0;
        }
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
    return pkg_install_manifest_file_with_depth(sh, PKG_TMP_MANIFEST, manifest_url, 1, depth);
}

int pkg_install_repo_package(const ush_state *sh, const char *name) {
    return pkg_install_repo_package_with_depth(sh, name, (const char *)0, (const char *)0, 0ULL);
}
