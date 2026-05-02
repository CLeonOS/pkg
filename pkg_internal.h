#ifndef CLEONOS_PKG_INTERNAL_H
#define CLEONOS_PKG_INTERNAL_H

#include "pkg_client.h"

#include "cmd_runtime.h"

#include <stdio.h>
#include <string.h>

#define PKG_DB_DIR "/system/pkg"
#define PKG_DB_PATH "/system/pkg/installed.db"
#define PKG_LOCK_PATH "/system/pkg/lock"
#define PKG_REPO_PATH "/system/pkg/repo.conf"
#define PKG_DEFAULT_REPO "http://clospkg.ecuil.com"
#define PKG_TMP_MANIFEST "/temp/p.clpkg"
#define PKG_TMP_ELF "/temp/p.elf"
#define PKG_TMP_API "/temp/pkg_api.json"

#define PKG_ARG_MAX 512U
#define PKG_NAME_MAX 64U
#define PKG_VERSION_MAX 32U
#define PKG_DESC_MAX 128U
#define PKG_SIZE_MAX 32U
#define PKG_DEPENDS_MAX 256U
#define PKG_CATEGORY_MAX 64U
#define PKG_TAGS_MAX 128U
#define PKG_SHA256_MAX 65U
#define PKG_DEPRECATED_MAX 256U
#define PKG_DB_LINE_MAX 1024U
#define PKG_URL_MAX 384U
#define PKG_TEXT_MAX 32768U
#define PKG_COPY_CHUNK 4096U
#define PKG_UPGRADE_ALL_MAX 64U
#define PKG_DEP_DEPTH_MAX 8U
#define PKG_DRY_RUN_MAX 32U

typedef unsigned char pkg_u8;

typedef struct pkg_sha256_ctx {
    pkg_u8 data[64];
    unsigned int datalen;
    u64 bitlen;
    unsigned int state[8];
} pkg_sha256_ctx;

typedef struct pkg_manifest {
    char name[PKG_NAME_MAX];
    char version[PKG_VERSION_MAX];
    char target[USH_PATH_MAX];
    char url[PKG_URL_MAX];
    char path[USH_PATH_MAX];
    char description[PKG_DESC_MAX];
    char depends[PKG_DEPENDS_MAX];
    char category[PKG_CATEGORY_MAX];
    char tags[PKG_TAGS_MAX];
    char sha256[PKG_SHA256_MAX];
    char deprecated[PKG_DEPRECATED_MAX];
} pkg_manifest;

typedef struct pkg_remote_package {
    char name[PKG_NAME_MAX];
    char version[PKG_VERSION_MAX];
    char target[USH_PATH_MAX];
    char description[PKG_DESC_MAX];
    char depends[PKG_DEPENDS_MAX];
    char category[PKG_CATEGORY_MAX];
    char tags[PKG_TAGS_MAX];
    char size[PKG_SIZE_MAX];
    char owner[PKG_NAME_MAX];
    char manifest_url[PKG_URL_MAX];
    char download_url[PKG_URL_MAX];
    char sha256[PKG_SHA256_MAX];
    char deprecated[PKG_DEPRECATED_MAX];
} pkg_remote_package;

typedef struct pkg_dependency {
    char name[PKG_NAME_MAX];
    char op[3];
    char version[PKG_VERSION_MAX];
} pkg_dependency;

typedef struct pkg_plan_item {
    char name[PKG_NAME_MAX];
    char version[PKG_VERSION_MAX];
    char target[USH_PATH_MAX];
    char source[PKG_URL_MAX];
    char depends[PKG_DEPENDS_MAX];
    char sha256[PKG_SHA256_MAX];
    char size_text[PKG_SIZE_MAX];
    u64 size_bytes;
    int size_known;
    int download;
    int overwrite;
} pkg_plan_item;

extern char pkg_text_buf[PKG_TEXT_MAX];
extern char pkg_db_buf[PKG_TEXT_MAX];
extern char pkg_db_new_buf[PKG_TEXT_MAX];
extern pkg_u8 pkg_copy_buf[PKG_COPY_CHUNK];
extern char pkg_upgrade_names[PKG_UPGRADE_ALL_MAX][PKG_NAME_MAX];
extern pkg_plan_item pkg_plan_items[PKG_DRY_RUN_MAX];
extern u64 pkg_plan_count;
extern int pkg_force_reinstall;

int pkg_has_prefix(const char *text, const char *prefix);
int pkg_has_suffix(const char *text, const char *suffix);
int pkg_is_url(const char *text);
int pkg_safe_name(const char *name);
int pkg_safe_version_text(const char *version);
char *pkg_trim_mut(char *text);
void pkg_copy_trimmed(char *dst, u64 dst_size, const char *src);
int pkg_append_text(char *dst, u64 dst_size, u64 *io_len, const char *text);
int pkg_append_char(char *dst, u64 dst_size, u64 *io_len, char ch);
int pkg_is_url_unreserved(char ch);
char pkg_hex_digit(u64 value);
int pkg_hex_char_value(char ch);
int pkg_hex_digest_is_valid(const char *text);
int pkg_hex_digest_equals(const char *left, const char *right);
int pkg_append_url_encoded(char *dst, u64 dst_size, u64 *io_len, const char *text);
const char *pkg_skip_ws_const(const char *pos);
int pkg_json_make_key_pattern(const char *key, char *out, u64 out_size);
const char *pkg_json_find_key_value(const char *start, const char *end, const char *key);
const char *pkg_json_object_end(const char *object_start);
int pkg_json_read_string_value(const char *value, const char *end, char *out, u64 out_size);
int pkg_json_get_string(const char *start, const char *end, const char *key, char *out, u64 out_size);
int pkg_json_get_number_text(const char *start, const char *end, const char *key, char *out, u64 out_size);
int pkg_read_file(const char *path, char *out, u64 out_size, u64 *out_len);
int pkg_write_file(const char *path, const char *data, u64 size);
int pkg_copy_file(const char *src, const char *dst);
int pkg_write_probe(const char *path);
unsigned int pkg_sha256_rotr(unsigned int value, unsigned int count);
unsigned int pkg_sha256_load_be32(const pkg_u8 *data);
void pkg_sha256_store_be32(unsigned int value, pkg_u8 *out);
void pkg_sha256_transform(pkg_sha256_ctx *ctx, const pkg_u8 data[64]);
void pkg_sha256_init(pkg_sha256_ctx *ctx);
void pkg_sha256_update(pkg_sha256_ctx *ctx, const pkg_u8 *data, u64 len);
void pkg_sha256_final(pkg_sha256_ctx *ctx, pkg_u8 hash[32]);
int pkg_sha256_file_hex(const char *path, char out_hex[PKG_SHA256_MAX]);
int pkg_file_has_elf_magic(const char *path);
void pkg_basename_no_ext(const char *path, char *out, u64 out_size);
int pkg_default_target(const char *name, char *out, u64 out_size);
int pkg_target_is_allowed(const char *path);
void pkg_dirname(const char *path, char *out, u64 out_size);
int pkg_join_path(const char *dir, const char *leaf, char *out, u64 out_size);
void pkg_manifest_init(pkg_manifest *manifest);
int pkg_parse_manifest(char *text, pkg_manifest *out_manifest);
int pkg_ensure_db_dir(void);
int pkg_load_repo(char *out, u64 out_size);
int pkg_build_repo_url(const char *repo, const char *mode, const char *name, char *out, u64 out_size);
int pkg_build_api_url(const char *repo, const char *api, const char *param_key, const char *param_value,
                             char *out, u64 out_size);
int pkg_write_command_ctx(const char *cmd, const char *arg, const char *cwd);
int pkg_download_to(const char *url, const char *out_path);
int pkg_fetch_api(const char *api, const char *param_key, const char *param_value, char *out, u64 out_size,
                         u64 *out_len);
void pkg_remote_package_init(pkg_remote_package *package);
int pkg_parse_remote_package_object(const char *start, const char *end, pkg_remote_package *out);
const char *pkg_json_find_named_array(const char *text, const char *key);
int pkg_remote_next_package(const char **io_cursor, pkg_remote_package *out);
int pkg_parse_info_package(const char *text, pkg_remote_package *out);
void pkg_print_api_error_or_default(const char *text, const char *fallback);
int pkg_char_is_digit(char ch);
char pkg_char_lower(char ch);
int pkg_version_compare(const char *left, const char *right);
int pkg_version_satisfies(const char *version, const char *op, const char *required);
int pkg_parse_dependency_spec(const char *spec, pkg_dependency *out);
int pkg_find_installed_version_text(const char *db_text, const char *name, char *out_version, u64 out_size);
int pkg_installed_dependency_satisfies(const pkg_dependency *dep);
int pkg_dependency_list_mentions(const char *depends, const char *name);
int pkg_remote_dependency_can_satisfy(const pkg_dependency *dep);
int pkg_db_line_parse(char *line, char **out_name, char **out_version, char **out_target, char **out_source,
                             char **out_depends);
int pkg_db_line_parse_ex(char *line, char **out_name, char **out_version, char **out_target, char **out_source,
                                char **out_depends, char **out_sha256);
int pkg_install_dependency_list(const ush_state *sh, const char *depends, u64 depth);
int pkg_resolve_local_path(const ush_state *sh, const char *arg, char *out, u64 out_size);
int pkg_record_install(const pkg_manifest *manifest, const char *source);
int pkg_install_elf_file(const pkg_manifest *manifest, const char *elf_path, const char *source);
int pkg_install_local_elf(const ush_state *sh, const char *source_arg);
int pkg_complete_manifest(const ush_state *sh, pkg_manifest *manifest, const char *manifest_source,
                                 int source_is_url);
int pkg_install_manifest_file_with_depth(const ush_state *sh, const char *manifest_path, const char *origin,
                                                int origin_is_url, u64 depth);
int pkg_install_manifest_file(const ush_state *sh, const char *manifest_path, const char *origin, int origin_is_url);
int pkg_install_url_elf(const char *url);
int pkg_install_repo_package_with_depth(const ush_state *sh, const char *name, const char *constraint_op,
                                               const char *constraint_version, u64 depth);
int pkg_install_repo_package(const ush_state *sh, const char *name);
int pkg_lock_acquire(void);
void pkg_lock_release(void);
void pkg_plan_reset(void);
int pkg_plan_add_manifest(const ush_state *sh, pkg_manifest *manifest, const char *source, u64 depth);
int pkg_plan_add_repo_package(const ush_state *sh, const char *name, const char *constraint_op,
                                     const char *constraint_version, u64 depth);
void pkg_plan_print(void);
int pkg_cmd_install_dry_run(const ush_state *sh, const char *arg);
int pkg_cmd_doctor(void);
int pkg_cmd_verify(const char *arg);
int pkg_cmd_install(const ush_state *sh, const char *arg);
void pkg_print_db_line(char *line);
int pkg_cmd_list(void);
int pkg_remove_record(const char *name, char *out_target, u64 out_target_size, int *out_found);
int pkg_remove_has_reverse_dependencies(const char *name);
int pkg_cmd_remove(const char *arg);
int pkg_cmd_repo(const char *arg);
int pkg_cmd_info(const char *arg);
int pkg_cmd_files(const char *arg);
void pkg_print_remote_header(void);
void pkg_print_remote_line(const pkg_remote_package *package);
int pkg_cmd_remote_list(void);
int pkg_cmd_remote_info(const char *arg);
int pkg_cmd_remote(const char *arg);
int pkg_cmd_search(const char *arg);
int pkg_cmd_filter_remote(const char *api, const char *label, const char *arg);
int pkg_find_installed_version_in_db(const char *db_text, const char *name, char *out_version, u64 out_size);
int pkg_load_installed_db_for_remote(void);
int pkg_cmd_update(void);
int pkg_collect_outdated_names(u64 *out_count, int *out_no_installed);
int pkg_cmd_upgrade(const ush_state *sh, const char *arg);
void pkg_usage(void);
int pkg_run(const ush_state *sh, const char *arg);
void pkg_reconstruct_argv(char *out, u64 out_size);
int pkg_client_app_main(void);

#endif
