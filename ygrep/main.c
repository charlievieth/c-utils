#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <yaml.h>

typedef struct {
	int  len;
	int  cap;
	char *data;
} char_slice;

void char_slice_init(char_slice *a) {
	a->len = 0;
	a->cap = 0;
	a->data = NULL;
}

int char_slice_do_grow(char_slice *a, int n) {
	int cap = a->cap > 0 ? 2*a->cap + n : n;
	char *data = realloc(a->data, (size_t)cap);
	if (!data) {
		free(a->data);
		return 0;
	}
	a->cap = cap;
	a->data = data;
	return 1;
}

static inline int char_slice_grow(char_slice *a, int n) {
	if (n <= a->cap - a->len) {
		return 1;
	}
	return char_slice_do_grow(a, n);
}

int char_slice_write_char(char_slice *a, char c) {
	if (!char_slice_grow(a, 1)) {
		return 0;
	}
	a->data[a->len++] = c;
	return 1;
}

int char_slice_unread_char(char_slice *a) {
	if (a->len > 0) {
		a->data[--a->len] = '\0';
		return 1;
	}
	return 0;
}

int char_slice_write_string(char_slice *a, const char *s) {
	const int n = strlen(s);
	if (!char_slice_grow(a, n+1)) {
		return 0;
	}
	memcpy(a->data + a->len, s, n+1);
	a->len += n;
	return 1;
}

// typedef struct {
// 	int  len;
// 	int  cap;
// 	char **str;
// } string_array;

typedef struct {
	yaml_char_t         *value; // The scalar value.
	size_t              length; // The length of the scalar value.
	yaml_scalar_style_t style;  // The scalar style.
} token_scalar;

typedef struct {
	int          depth;
	token_scalar **toks;
} parse_state;

// forward decls...
const char * token_type_string(yaml_token_type_t t);
void print_token(yaml_token_t *token);

int main() {
	FILE *fp = fopen("testdata/cf-deployment.yml", "r");
	if(!fp) {
		perror("fopen");
		return 1;
	}
	yaml_parser_t parser;
	if (yaml_parser_initialize(&parser) == 0) {
		fprintf(stderr, "error: initialize\n");
		goto InitError;
	}
	yaml_parser_set_input_file(&parser, fp);

	int ret;
	int count = 0;
	yaml_token_t token;
	while ((ret = yaml_parser_scan(&parser, &token))) {
		printf("%d: %s\n", count++, token_type_string(token.type));
		print_token(&token);
		if (token.type == YAML_STREAM_END_TOKEN) {
			break;
		}
	}
	if (ret == 1) {
		printf("error (scan): last state: %s\n", token_type_string(token.type));
	}
	yaml_parser_delete(&parser);
	return 0;

InitError:
	fclose(fp);
	return 1;
}

void print_token(yaml_token_t *token) {
	switch (token->type) {
	case YAML_STREAM_START_TOKEN:
		break;
	case YAML_ALIAS_TOKEN:
		printf("  alias: %s\n", token->data.alias.value);
		break;
	case YAML_ANCHOR_TOKEN:
		printf("  anchor: %s\n", token->data.anchor.value);
		break;
	case YAML_TAG_TOKEN:
		printf("  handle: %s\n", token->data.tag.handle);
		printf("  suffix: %s\n", token->data.tag.suffix);
		break;
	case YAML_SCALAR_TOKEN:
		printf("  value: %s\n", token->data.scalar.value);
		break;
	case YAML_VERSION_DIRECTIVE_TOKEN:
		break;
	case YAML_TAG_DIRECTIVE_TOKEN:
		printf("  handle: %s\n", token->data.tag_directive.handle);
		printf("  suffix: %s\n", token->data.tag_directive.prefix);
		break;
	// make the compiler happy...
	case YAML_BLOCK_END_TOKEN:
	case YAML_BLOCK_ENTRY_TOKEN:
	case YAML_BLOCK_MAPPING_START_TOKEN:
	case YAML_BLOCK_SEQUENCE_START_TOKEN:
	case YAML_DOCUMENT_END_TOKEN:
	case YAML_DOCUMENT_START_TOKEN:
	case YAML_FLOW_ENTRY_TOKEN:
	case YAML_FLOW_MAPPING_END_TOKEN:
	case YAML_FLOW_MAPPING_START_TOKEN:
	case YAML_FLOW_SEQUENCE_END_TOKEN:
	case YAML_FLOW_SEQUENCE_START_TOKEN:
	case YAML_KEY_TOKEN:
	case YAML_NO_TOKEN:
	case YAML_STREAM_END_TOKEN:
	case YAML_VALUE_TOKEN:
		return;
	}
}

const char * token_type_string(yaml_token_type_t t) {
	switch (t) {
	case YAML_NO_TOKEN:
		return "YAML_NO_TOKEN";
	case YAML_STREAM_START_TOKEN:
		return "YAML_STREAM_START_TOKEN";
	case YAML_STREAM_END_TOKEN:
		return "YAML_STREAM_END_TOKEN";
	case YAML_VERSION_DIRECTIVE_TOKEN:
		return "YAML_VERSION_DIRECTIVE_TOKEN";
	case YAML_TAG_DIRECTIVE_TOKEN:
		return "YAML_TAG_DIRECTIVE_TOKEN";
	case YAML_DOCUMENT_START_TOKEN:
		return "YAML_DOCUMENT_START_TOKEN";
	case YAML_DOCUMENT_END_TOKEN:
		return "YAML_DOCUMENT_END_TOKEN";
	case YAML_BLOCK_SEQUENCE_START_TOKEN:
		return "YAML_BLOCK_SEQUENCE_START_TOKEN";
	case YAML_BLOCK_MAPPING_START_TOKEN:
		return "YAML_BLOCK_MAPPING_START_TOKEN";
	case YAML_BLOCK_END_TOKEN:
		return "YAML_BLOCK_END_TOKEN";
	case YAML_FLOW_SEQUENCE_START_TOKEN:
		return "YAML_FLOW_SEQUENCE_START_TOKEN";
	case YAML_FLOW_SEQUENCE_END_TOKEN:
		return "YAML_FLOW_SEQUENCE_END_TOKEN";
	case YAML_FLOW_MAPPING_START_TOKEN:
		return "YAML_FLOW_MAPPING_START_TOKEN";
	case YAML_FLOW_MAPPING_END_TOKEN:
		return "YAML_FLOW_MAPPING_END_TOKEN";
	case YAML_BLOCK_ENTRY_TOKEN:
		return "YAML_BLOCK_ENTRY_TOKEN";
	case YAML_FLOW_ENTRY_TOKEN:
		return "YAML_FLOW_ENTRY_TOKEN";
	case YAML_KEY_TOKEN:
		return "YAML_KEY_TOKEN";
	case YAML_VALUE_TOKEN:
		return "YAML_VALUE_TOKEN";
	case YAML_ALIAS_TOKEN:
		return "YAML_ALIAS_TOKEN";
	case YAML_ANCHOR_TOKEN:
		return "YAML_ANCHOR_TOKEN";
	case YAML_TAG_TOKEN:
		return "YAML_TAG_TOKEN";
	case YAML_SCALAR_TOKEN:
		return "YAML_SCALAR_TOKEN";
	default:
		return "INVALID_TOKEN";
	}
}
