#include "pkg_internal.h"

int pkg_read_file(const char *path, char *out, u64 out_size, u64 *out_len) {
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

int pkg_write_file(const char *path, const char *data, u64 size) {
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

int pkg_copy_file(const char *src, const char *dst) {
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

int pkg_write_probe(const char *path) {
    static const char probe[] = "ok\n";

    if (path == (const char *)0 || path[0] == '\0') {
        return 0;
    }

    if (pkg_write_file(path, probe, (u64)(sizeof(probe) - 1U)) == 0) {
        return 0;
    }

    return (cleonos_sys_fs_remove(path) != 0ULL) ? 1 : 0;
}
