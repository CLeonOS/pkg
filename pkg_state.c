#include "pkg_internal.h"

char pkg_text_buf[PKG_TEXT_MAX];
char pkg_db_buf[PKG_TEXT_MAX];
char pkg_db_new_buf[PKG_TEXT_MAX];
pkg_u8 pkg_copy_buf[PKG_COPY_CHUNK];
char pkg_upgrade_names[PKG_UPGRADE_ALL_MAX][PKG_NAME_MAX];
pkg_plan_item pkg_plan_items[PKG_DRY_RUN_MAX];
u64 pkg_plan_count;
int pkg_force_reinstall;
