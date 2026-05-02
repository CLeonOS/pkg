#include "pkg_internal.h"

void pkg_reconstruct_argv(char *out, u64 out_size) {
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
