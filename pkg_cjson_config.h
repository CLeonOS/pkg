#ifndef CLEONOS_PKG_CJSON_CONFIG_H
#define CLEONOS_PKG_CJSON_CONFIG_H

#include <stdarg.h>

#ifndef CJSON_NESTING_LIMIT
#define CJSON_NESTING_LIMIT 64
#endif

int sprintf(char *out, const char *fmt, ...);
int sscanf(const char *text, const char *fmt, ...);
double fabs(double value);

#endif
