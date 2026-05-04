#include "pkg_cjson_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int sprintf(char *out, const char *fmt, ...) {
    va_list args;
    int rc;

    va_start(args, fmt);
    rc = vsnprintf(out, 0x7fffffffUL, fmt, args);
    va_end(args);
    return rc;
}

int sscanf(const char *text, const char *fmt, ...) {
    va_list args;
    int matched = 0;

    if (text == (const char *)0 || fmt == (const char *)0) {
        return 0;
    }

    va_start(args, fmt);
    if (strcmp(fmt, "%lg") == 0) {
        double *out = va_arg(args, double *);
        char *end = (char *)0;
        double value;

        if (out != (double *)0) {
            value = strtod(text, &end);
            if (end != (char *)0 && end != text) {
                *out = value;
                matched = 1;
            }
        }
    }
    va_end(args);

    return matched;
}
