#include "pkg_internal.h"

#include <sqlite3.h>

typedef struct pkg_sqlite_state {
    sqlite3 *db;
    int ready;
} pkg_sqlite_state;

typedef struct pkg_sqlite_iter_state {
    pkg_installed_iter_fn fn;
    void *ctx;
    int failed;
} pkg_sqlite_iter_state;

typedef struct pkg_sqlite_source_iter_state {
    pkg_source_iter_fn fn;
    void *ctx;
    int failed;
} pkg_sqlite_source_iter_state;

static pkg_sqlite_state pkg_sqlite;

static int pkg_sqlite_exec(sqlite3 *db, const char *sql) {
    char *error = (char *)0;
    int rc;

    rc = sqlite3_exec(db, sql, 0, 0, &error);
    if (rc != SQLITE_OK) {
        if (error != (char *)0) {
            (void)printf("pkg: sqlite error: %s\n", error);
            sqlite3_free(error);
        } else {
            (void)printf("pkg: sqlite exec failed rc=%d\n", rc);
        }
        return 0;
    }

    return 1;
}

static int pkg_sqlite_open_db(void) {
    int rc;

    if (pkg_sqlite.ready != 0 && pkg_sqlite.db != (sqlite3 *)0) {
        return 1;
    }

    if (pkg_ensure_db_dir() == 0) {
        return 0;
    }

    rc = sqlite3_open_v2(PKG_SQLITE_PATH, &pkg_sqlite.db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, (const char *)0);
    if (rc != SQLITE_OK || pkg_sqlite.db == (sqlite3 *)0) {
        (void)printf("pkg: sqlite open failed rc=%d\n", rc);
        if (pkg_sqlite.db != (sqlite3 *)0) {
            sqlite3_close(pkg_sqlite.db);
            pkg_sqlite.db = (sqlite3 *)0;
        }
        return 0;
    }

    if (pkg_sqlite_exec(pkg_sqlite.db, "PRAGMA journal_mode=DELETE;") == 0 ||
        pkg_sqlite_exec(pkg_sqlite.db, "PRAGMA synchronous=FULL;") == 0 ||
        pkg_sqlite_exec(pkg_sqlite.db,
                        "CREATE TABLE IF NOT EXISTS installed_packages("
                        "name TEXT PRIMARY KEY,"
                        "version TEXT NOT NULL,"
                        "target TEXT NOT NULL,"
                        "source TEXT NOT NULL DEFAULT '',"
                        "depends TEXT NOT NULL DEFAULT '',"
                        "sha256 TEXT NOT NULL DEFAULT ''"
                        ");") == 0 ||
        pkg_sqlite_exec(pkg_sqlite.db,
                        "CREATE TABLE IF NOT EXISTS sources("
                        "name TEXT PRIMARY KEY,"
                        "url TEXT NOT NULL"
                        ");") == 0 ||
        pkg_sqlite_exec(pkg_sqlite.db,
                        "CREATE TABLE IF NOT EXISTS meta("
                        "key TEXT PRIMARY KEY,"
                        "value TEXT NOT NULL"
                        ");") == 0) {
        sqlite3_close(pkg_sqlite.db);
        pkg_sqlite.db = (sqlite3 *)0;
        return 0;
    }

    pkg_sqlite.ready = 1;
    return 1;
}

static int pkg_sqlite_table_empty(const char *table, int *out_empty) {
    sqlite3_stmt *stmt = (sqlite3_stmt *)0;
    char sql[128];
    int rc;

    if (out_empty == (int *)0 || pkg_sqlite_open_db() == 0) {
        return 0;
    }

    *out_empty = 1;
    rc = snprintf(sql, (usize)sizeof(sql), "SELECT COUNT(*) FROM %s;", table);
    if (rc <= 0 || (u64)rc >= (u64)sizeof(sql)) {
        return 0;
    }

    rc = sqlite3_prepare_v2(pkg_sqlite.db, sql, -1, &stmt, (const char **)0);
    if (rc != SQLITE_OK || stmt == (sqlite3_stmt *)0) {
        return 0;
    }

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        *out_empty = (sqlite3_column_int(stmt, 0) == 0) ? 1 : 0;
        sqlite3_finalize(stmt);
        return 1;
    }

    sqlite3_finalize(stmt);
    return 0;
}

static int pkg_sqlite_meta_get(const char *key, char *out, u64 out_size) {
    sqlite3_stmt *stmt = (sqlite3_stmt *)0;
    int rc;

    if (out == (char *)0 || out_size == 0ULL || key == (const char *)0 || pkg_sqlite_open_db() == 0) {
        return 0;
    }

    out[0] = '\0';
    rc = sqlite3_prepare_v2(pkg_sqlite.db, "SELECT value FROM meta WHERE key=?1;", -1, &stmt, (const char **)0);
    if (rc != SQLITE_OK || stmt == (sqlite3_stmt *)0) {
        return 0;
    }
    if (sqlite3_bind_text(stmt, 1, key, -1, SQLITE_STATIC) != SQLITE_OK) {
        sqlite3_finalize(stmt);
        return 0;
    }

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        const unsigned char *value = sqlite3_column_text(stmt, 0);
        ush_copy(out, out_size, (value != (const unsigned char *)0) ? (const char *)value : "");
        sqlite3_finalize(stmt);
        return 1;
    }

    sqlite3_finalize(stmt);
    return 0;
}

static int pkg_sqlite_meta_set(const char *key, const char *value) {
    sqlite3_stmt *stmt = (sqlite3_stmt *)0;
    int rc;

    if (key == (const char *)0 || value == (const char *)0 || pkg_sqlite_open_db() == 0) {
        return 0;
    }

    rc = sqlite3_prepare_v2(pkg_sqlite.db,
                            "INSERT INTO meta(key, value) VALUES(?1, ?2) "
                            "ON CONFLICT(key) DO UPDATE SET value=excluded.value;",
                            -1, &stmt, (const char **)0);
    if (rc != SQLITE_OK || stmt == (sqlite3_stmt *)0) {
        return 0;
    }
    if (sqlite3_bind_text(stmt, 1, key, -1, SQLITE_STATIC) != SQLITE_OK ||
        sqlite3_bind_text(stmt, 2, value, -1, SQLITE_STATIC) != SQLITE_OK) {
        sqlite3_finalize(stmt);
        return 0;
    }

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? 1 : 0;
}

static int pkg_sqlite_import_installed_text(void) {
    u64 len = 0ULL;
    char *line;
    sqlite3_stmt *stmt = (sqlite3_stmt *)0;
    int rc;

    if (pkg_read_file(PKG_DB_PATH, pkg_db_buf, (u64)sizeof(pkg_db_buf), &len) == 0 || len == 0ULL) {
        return 1;
    }

    rc = sqlite3_prepare_v2(pkg_sqlite.db,
                            "INSERT INTO installed_packages(name, version, target, source, depends, sha256) "
                            "VALUES(?1, ?2, ?3, ?4, ?5, ?6) "
                            "ON CONFLICT(name) DO UPDATE SET "
                            "version=excluded.version, "
                            "target=excluded.target, "
                            "source=excluded.source, "
                            "depends=excluded.depends, "
                            "sha256=excluded.sha256;",
                            -1, &stmt, (const char **)0);
    if (rc != SQLITE_OK || stmt == (sqlite3_stmt *)0) {
        return 0;
    }

    line = pkg_db_buf;
    while (*line != '\0') {
        char *next = line;
        char copy[PKG_DB_LINE_MAX];
        char *name;
        char *version;
        char *target;
        char *source;
        char *depends;
        char *sha256;

        while (*next != '\0' && *next != '\n') {
            next++;
        }
        if (*next == '\n') {
            *next = '\0';
            next++;
        }

        if (line[0] != '\0') {
            ush_copy(copy, (u64)sizeof(copy), line);
            if (pkg_db_line_parse_ex(copy, &name, &version, &target, &source, &depends, &sha256) != 0) {
                sqlite3_bind_text(stmt, 1, name, -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 2, version, -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 3, target, -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 4, source, -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 5, depends, -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 6, sha256, -1, SQLITE_TRANSIENT);
                rc = sqlite3_step(stmt);
                sqlite3_reset(stmt);
                sqlite3_clear_bindings(stmt);
                if (rc != SQLITE_DONE) {
                    sqlite3_finalize(stmt);
                    return 0;
                }
            }
        }

        line = next;
    }

    sqlite3_finalize(stmt);
    return 1;
}

static int pkg_sqlite_import_sources_text(void) {
    u64 len = 0ULL;
    char *line;
    sqlite3_stmt *stmt = (sqlite3_stmt *)0;
    int rc;

    if (pkg_read_file(PKG_SOURCES_PATH, pkg_db_buf, (u64)sizeof(pkg_db_buf), &len) == 0 || len == 0ULL) {
        return 1;
    }

    rc = sqlite3_prepare_v2(pkg_sqlite.db,
                            "INSERT INTO sources(name, url) VALUES(?1, ?2) "
                            "ON CONFLICT(name) DO UPDATE SET url=excluded.url;",
                            -1, &stmt, (const char **)0);
    if (rc != SQLITE_OK || stmt == (sqlite3_stmt *)0) {
        return 0;
    }

    line = pkg_db_buf;
    while (*line != '\0') {
        char *next = line;
        char copy[PKG_DB_LINE_MAX];
        char *url;

        while (*next != '\0' && *next != '\n') {
            next++;
        }
        if (*next == '\n') {
            *next = '\0';
            next++;
        }

        if (line[0] != '\0') {
            ush_copy(copy, (u64)sizeof(copy), line);
            url = strchr(copy, '|');
            if (url != (char *)0) {
                *url = '\0';
                url++;
                sqlite3_bind_text(stmt, 1, copy, -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 2, url, -1, SQLITE_TRANSIENT);
                rc = sqlite3_step(stmt);
                sqlite3_reset(stmt);
                sqlite3_clear_bindings(stmt);
                if (rc != SQLITE_DONE) {
                    sqlite3_finalize(stmt);
                    return 0;
                }
            }
        }

        line = next;
    }

    sqlite3_finalize(stmt);
    return 1;
}

static int pkg_sqlite_import_repo_text(void) {
    u64 got = 0ULL;
    char *trimmed;

    if (pkg_read_file(PKG_REPO_PATH, pkg_text_buf, (u64)sizeof(pkg_text_buf), &got) == 0 || got == 0ULL) {
        return 1;
    }

    trimmed = pkg_trim_mut(pkg_text_buf);
    if (trimmed[0] == '\0') {
        return 1;
    }

    return pkg_sqlite_meta_set("active_repo", trimmed);
}

int pkg_sqlite_init(void) {
    int empty_installed = 1;
    int empty_sources = 1;
    char active_repo[PKG_URL_MAX];

    if (pkg_sqlite_open_db() == 0) {
        return 0;
    }

    if (pkg_sqlite_table_empty("installed_packages", &empty_installed) == 0 ||
        pkg_sqlite_table_empty("sources", &empty_sources) == 0) {
        return 0;
    }

    if (empty_installed != 0 && pkg_sqlite_import_installed_text() == 0) {
        return 0;
    }
    if (empty_sources != 0 && pkg_sqlite_import_sources_text() == 0) {
        return 0;
    }
    if (pkg_sqlite_meta_get("active_repo", active_repo, (u64)sizeof(active_repo)) == 0) {
        if (pkg_sqlite_import_repo_text() == 0) {
            return 0;
        }
    }

    return 1;
}

int pkg_sqlite_record_install(const pkg_manifest *manifest, const char *source) {
    sqlite3_stmt *stmt = (sqlite3_stmt *)0;
    int rc;

    if (manifest == (const pkg_manifest *)0 || manifest->name[0] == '\0' || manifest->target[0] == '\0' ||
        pkg_sqlite_init() == 0) {
        return 0;
    }

    rc = sqlite3_prepare_v2(pkg_sqlite.db,
                            "INSERT INTO installed_packages(name, version, target, source, depends, sha256) "
                            "VALUES(?1, ?2, ?3, ?4, ?5, ?6) "
                            "ON CONFLICT(name) DO UPDATE SET "
                            "version=excluded.version, "
                            "target=excluded.target, "
                            "source=excluded.source, "
                            "depends=excluded.depends, "
                            "sha256=excluded.sha256;",
                            -1, &stmt, (const char **)0);
    if (rc != SQLITE_OK || stmt == (sqlite3_stmt *)0) {
        return 0;
    }

    sqlite3_bind_text(stmt, 1, manifest->name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, manifest->version, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, manifest->target, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, (source != (const char *)0) ? source : "unknown", -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 5, manifest->depends, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 6, manifest->sha256, -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? 1 : 0;
}

int pkg_sqlite_get_installed_version(const char *name, char *out_version, u64 out_size) {
    sqlite3_stmt *stmt = (sqlite3_stmt *)0;
    int rc;

    if (out_version != (char *)0 && out_size > 0ULL) {
        out_version[0] = '\0';
    }
    if (name == (const char *)0 || pkg_safe_name(name) == 0 || out_version == (char *)0 || out_size == 0ULL ||
        pkg_sqlite_init() == 0) {
        return 0;
    }

    rc = sqlite3_prepare_v2(pkg_sqlite.db, "SELECT version FROM installed_packages WHERE name=?1;", -1, &stmt,
                            (const char **)0);
    if (rc != SQLITE_OK || stmt == (sqlite3_stmt *)0) {
        return 0;
    }
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        const unsigned char *version = sqlite3_column_text(stmt, 0);
        ush_copy(out_version, out_size, (version != (const unsigned char *)0) ? (const char *)version : "");
        sqlite3_finalize(stmt);
        return 1;
    }

    sqlite3_finalize(stmt);
    return 0;
}

int pkg_sqlite_get_installed_record(const char *name, pkg_installed_record *out_record) {
    sqlite3_stmt *stmt = (sqlite3_stmt *)0;
    int rc;

    if (out_record != (pkg_installed_record *)0) {
        ush_zero(out_record, (u64)sizeof(*out_record));
    }
    if (name == (const char *)0 || pkg_safe_name(name) == 0 || out_record == (pkg_installed_record *)0 ||
        pkg_sqlite_init() == 0) {
        return 0;
    }

    rc = sqlite3_prepare_v2(pkg_sqlite.db,
                            "SELECT name, version, target, source, depends, sha256 "
                            "FROM installed_packages WHERE name=?1;",
                            -1, &stmt, (const char **)0);
    if (rc != SQLITE_OK || stmt == (sqlite3_stmt *)0) {
        return 0;
    }
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        const unsigned char *value0 = sqlite3_column_text(stmt, 0);
        const unsigned char *value1 = sqlite3_column_text(stmt, 1);
        const unsigned char *value2 = sqlite3_column_text(stmt, 2);
        const unsigned char *value3 = sqlite3_column_text(stmt, 3);
        const unsigned char *value4 = sqlite3_column_text(stmt, 4);
        const unsigned char *value5 = sqlite3_column_text(stmt, 5);

        ush_copy(out_record->name, (u64)sizeof(out_record->name),
                 (value0 != (const unsigned char *)0) ? (const char *)value0 : "");
        ush_copy(out_record->version, (u64)sizeof(out_record->version),
                 (value1 != (const unsigned char *)0) ? (const char *)value1 : "");
        ush_copy(out_record->target, (u64)sizeof(out_record->target),
                 (value2 != (const unsigned char *)0) ? (const char *)value2 : "");
        ush_copy(out_record->source, (u64)sizeof(out_record->source),
                 (value3 != (const unsigned char *)0) ? (const char *)value3 : "");
        ush_copy(out_record->depends, (u64)sizeof(out_record->depends),
                 (value4 != (const unsigned char *)0) ? (const char *)value4 : "");
        ush_copy(out_record->sha256, (u64)sizeof(out_record->sha256),
                 (value5 != (const unsigned char *)0) ? (const char *)value5 : "");
        sqlite3_finalize(stmt);
        return 1;
    }

    sqlite3_finalize(stmt);
    return 0;
}

int pkg_sqlite_remove_package(const char *name, char *out_target, u64 out_target_size, int *out_found) {
    sqlite3_stmt *stmt = (sqlite3_stmt *)0;
    int rc;

    if (out_found != (int *)0) {
        *out_found = 0;
    }
    if (out_target != (char *)0 && out_target_size > 0ULL) {
        out_target[0] = '\0';
    }
    if (name == (const char *)0 || pkg_safe_name(name) == 0 || pkg_sqlite_init() == 0) {
        return 0;
    }

    rc = sqlite3_prepare_v2(pkg_sqlite.db, "SELECT target FROM installed_packages WHERE name=?1;", -1, &stmt,
                            (const char **)0);
    if (rc != SQLITE_OK || stmt == (sqlite3_stmt *)0) {
        return 0;
    }
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        const unsigned char *target = sqlite3_column_text(stmt, 0);
        if (out_found != (int *)0) {
            *out_found = 1;
        }
        if (out_target != (char *)0 && out_target_size > 0ULL) {
            ush_copy(out_target, out_target_size, (target != (const unsigned char *)0) ? (const char *)target : "");
        }
    }
    sqlite3_finalize(stmt);

    rc = sqlite3_prepare_v2(pkg_sqlite.db, "DELETE FROM installed_packages WHERE name=?1;", -1, &stmt,
                            (const char **)0);
    if (rc != SQLITE_OK || stmt == (sqlite3_stmt *)0) {
        return 0;
    }
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? 1 : 0;
}

int pkg_sqlite_count_installed(u64 *out_count) {
    sqlite3_stmt *stmt = (sqlite3_stmt *)0;
    int rc;

    if (out_count != (u64 *)0) {
        *out_count = 0ULL;
    }
    if (out_count == (u64 *)0 || pkg_sqlite_init() == 0) {
        return 0;
    }

    rc = sqlite3_prepare_v2(pkg_sqlite.db, "SELECT COUNT(*) FROM installed_packages;", -1, &stmt, (const char **)0);
    if (rc != SQLITE_OK || stmt == (sqlite3_stmt *)0) {
        return 0;
    }

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        *out_count = (u64)sqlite3_column_int64(stmt, 0);
        sqlite3_finalize(stmt);
        return 1;
    }

    sqlite3_finalize(stmt);
    return 0;
}

int pkg_sqlite_foreach_installed(pkg_installed_iter_fn fn, void *ctx) {
    sqlite3_stmt *stmt = (sqlite3_stmt *)0;
    int rc;

    if (fn == (pkg_installed_iter_fn)0 || pkg_sqlite_init() == 0) {
        return 0;
    }

    rc = sqlite3_prepare_v2(pkg_sqlite.db,
                            "SELECT name, version, target, source, depends, sha256 "
                            "FROM installed_packages ORDER BY name;",
                            -1, &stmt, (const char **)0);
    if (rc != SQLITE_OK || stmt == (sqlite3_stmt *)0) {
        return 0;
    }

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        pkg_installed_record record;
        const unsigned char *value0 = sqlite3_column_text(stmt, 0);
        const unsigned char *value1 = sqlite3_column_text(stmt, 1);
        const unsigned char *value2 = sqlite3_column_text(stmt, 2);
        const unsigned char *value3 = sqlite3_column_text(stmt, 3);
        const unsigned char *value4 = sqlite3_column_text(stmt, 4);
        const unsigned char *value5 = sqlite3_column_text(stmt, 5);

        ush_zero(&record, (u64)sizeof(record));
        ush_copy(record.name, (u64)sizeof(record.name), (value0 != (const unsigned char *)0) ? (const char *)value0 : "");
        ush_copy(record.version, (u64)sizeof(record.version),
                 (value1 != (const unsigned char *)0) ? (const char *)value1 : "");
        ush_copy(record.target, (u64)sizeof(record.target), (value2 != (const unsigned char *)0) ? (const char *)value2 : "");
        ush_copy(record.source, (u64)sizeof(record.source), (value3 != (const unsigned char *)0) ? (const char *)value3 : "");
        ush_copy(record.depends, (u64)sizeof(record.depends), (value4 != (const unsigned char *)0) ? (const char *)value4 : "");
        ush_copy(record.sha256, (u64)sizeof(record.sha256), (value5 != (const unsigned char *)0) ? (const char *)value5 : "");
        if (fn(&record, ctx) == 0) {
            sqlite3_finalize(stmt);
            return 0;
        }
    }

    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? 1 : 0;
}

int pkg_sqlite_source_get(const char *name, char *out_url, u64 out_url_size) {
    sqlite3_stmt *stmt = (sqlite3_stmt *)0;
    int rc;

    if (out_url != (char *)0 && out_url_size > 0ULL) {
        out_url[0] = '\0';
    }
    if (name == (const char *)0 || pkg_safe_name(name) == 0 || out_url == (char *)0 || out_url_size == 0ULL ||
        pkg_sqlite_init() == 0) {
        return 0;
    }

    rc = sqlite3_prepare_v2(pkg_sqlite.db, "SELECT url FROM sources WHERE name=?1;", -1, &stmt, (const char **)0);
    if (rc != SQLITE_OK || stmt == (sqlite3_stmt *)0) {
        return 0;
    }
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        const unsigned char *url = sqlite3_column_text(stmt, 0);
        ush_copy(out_url, out_url_size, (url != (const unsigned char *)0) ? (const char *)url : "");
        sqlite3_finalize(stmt);
        return 1;
    }

    sqlite3_finalize(stmt);
    return 0;
}

int pkg_sqlite_source_set(const char *name, const char *url) {
    sqlite3_stmt *stmt = (sqlite3_stmt *)0;
    int rc;

    if (name == (const char *)0 || pkg_safe_name(name) == 0 || url == (const char *)0 || url[0] == '\0' ||
        pkg_sqlite_init() == 0) {
        return 0;
    }

    rc = sqlite3_prepare_v2(pkg_sqlite.db,
                            "INSERT INTO sources(name, url) VALUES(?1, ?2) "
                            "ON CONFLICT(name) DO UPDATE SET url=excluded.url;",
                            -1, &stmt, (const char **)0);
    if (rc != SQLITE_OK || stmt == (sqlite3_stmt *)0) {
        return 0;
    }
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, url, -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? 1 : 0;
}

int pkg_sqlite_source_remove(const char *name) {
    sqlite3_stmt *stmt = (sqlite3_stmt *)0;
    int rc;

    if (name == (const char *)0 || pkg_safe_name(name) == 0 || pkg_sqlite_init() == 0) {
        return 0;
    }

    rc = sqlite3_prepare_v2(pkg_sqlite.db, "DELETE FROM sources WHERE name=?1;", -1, &stmt, (const char **)0);
    if (rc != SQLITE_OK || stmt == (sqlite3_stmt *)0) {
        return 0;
    }
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? 1 : 0;
}

int pkg_sqlite_foreach_sources(pkg_source_iter_fn fn, void *ctx) {
    sqlite3_stmt *stmt = (sqlite3_stmt *)0;
    int rc;

    if (fn == (pkg_source_iter_fn)0 || pkg_sqlite_init() == 0) {
        return 0;
    }

    rc = sqlite3_prepare_v2(pkg_sqlite.db, "SELECT name, url FROM sources ORDER BY name;", -1, &stmt,
                            (const char **)0);
    if (rc != SQLITE_OK || stmt == (sqlite3_stmt *)0) {
        return 0;
    }

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        pkg_source_record record;
        const unsigned char *value0 = sqlite3_column_text(stmt, 0);
        const unsigned char *value1 = sqlite3_column_text(stmt, 1);

        ush_zero(&record, (u64)sizeof(record));
        ush_copy(record.name, (u64)sizeof(record.name), (value0 != (const unsigned char *)0) ? (const char *)value0 : "");
        ush_copy(record.url, (u64)sizeof(record.url), (value1 != (const unsigned char *)0) ? (const char *)value1 : "");
        if (fn(&record, ctx) == 0) {
            sqlite3_finalize(stmt);
            return 0;
        }
    }

    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? 1 : 0;
}

int pkg_sqlite_get_active_repo(char *out, u64 out_size) {
    if (out == (char *)0 || out_size == 0ULL || pkg_sqlite_init() == 0) {
        return 0;
    }

    if (pkg_sqlite_meta_get("active_repo", out, out_size) != 0 && out[0] != '\0') {
        return 1;
    }

    ush_copy(out, out_size, PKG_DEFAULT_REPO);
    return 1;
}

int pkg_sqlite_set_active_repo(const char *url) {
    if (url == (const char *)0 || url[0] == '\0' || pkg_sqlite_init() == 0) {
        return 0;
    }
    return pkg_sqlite_meta_set("active_repo", url);
}
