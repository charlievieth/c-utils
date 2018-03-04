#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <getopt.h>

#include "opts.h"
#include "slice.h"

void _Noreturn usage(int status) {
	const char *name = getprogname();
	if (name == NULL) {
		name = "linecount";
	}
	if (status != 0) {
		fprintf(stderr, "Usage: %s [OPTION]... FILE...\n", name);
		fprintf(stderr, "Try '%s --help' for more information.\n", name);
	} else {
		printf("Usage: %s [OPTION]... [FILE]...\n", name);
		printf("\n\
Options:\n\
  -n, --name=GLOB  Include only files whose base name matches GLOB.\n\
  -e, --ext=GLOB   Include only files whose extension name matches GLOB.\n\
\n");
	}
	exit(status);
}

void init_opts() {
	string_array_init(&opts.exts);
	string_array_init(&opts.names);
	string_array_init(&opts.paths);
}

static const size_t max_argument_length = 8192;

bool valid_string_option(const char *s, const char *arg_name) {
	size_t n = strlen(s);
	if (n == 0) {
		fprintf(stderr, "%s: option '%s' requires a non-empty argument\n",
			getprogname(), arg_name);
		return false;
	}
	if (n > max_argument_length) {
		fprintf(stderr, "%s: option '%s' argument exceeds max length (%zu): %zu\n",
			getprogname(), arg_name, max_argument_length, n);
		return false;
	}
	return true;
}

void parse_options(int argc, char **argv) {
	init_opts(); // initialize options

	struct option longopts[] = {
		{"name", required_argument, NULL, 'n'},
		{"ext",  required_argument, NULL, 'e'},
		{"help", no_argument,       NULL,  0},
		{NULL, 0, NULL, 0},
	};

	int ch;
	int opt_index = 0;
	while ((ch = getopt_long(argc, argv, "n:e:", longopts, &opt_index)) != -1) {
		switch (ch) {
		case 0:
			if (strcmp(longopts[opt_index].name, "help") == 0) {
				usage(0);
			}
			usage(1);
		case 'e':
			if (!valid_string_option(optarg, "--ext")) {
				usage(1);
			}
			string_array_append(&opts.exts, optarg);
			break;
		case 'n':
			if (!valid_string_option(optarg, "--name")) {
				usage(1);
			}
			string_array_append(&opts.names, optarg);
			break;
		case ':':
			usage(1);
			break;
		case '?':
			usage(1);
			break;
		default:
			fprintf(stderr, "Error: parsing arguments\n");
			usage(1);
		}
	}
	// Missing FILE argument, assume '.' was intended
	if (optind == argc) {
		string_array_append(&opts.paths, ".");
		return;
	}
	while(optind < argc) {
		if (!valid_string_option(argv[optind], "FILE")) {
			usage(1);
		}
		string_array_append(&opts.paths, argv[optind++]);
	}
}
