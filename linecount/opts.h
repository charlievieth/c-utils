#ifndef LC_OPTS_H
#define LC_OPTS_H

#include <stdbool.h>

#include "cdefs.h"
#include "slice.h"

typedef struct {
	string_array exts;
	string_array names;
	string_array paths;
} cli_options;

// Must init!
cli_options opts;

void _Noreturn usage(int status);
bool valid_string_option(const char *s, const char *arg_name);
void parse_options(int argc, char **argv);

#endif /* LC_OPTS_H */
