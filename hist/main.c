#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>

char * join_paths(const char *dirname, const char *basename) {
	size_t len = strlen(dirname) + strlen(basename) + 2;
	char *buf = malloc(len);
	int ret = snprintf(buf, len, "%s/%s", dirname, basename);
	if (ret < 0) {
		return NULL;
	}
	return buf;
}

char * home_directory() {
	struct passwd *pwd = getpwuid(getuid());
	if (!pwd) {
		perror("home_directory: getpwuid");
		return NULL;
	}
	return pwd->pw_dir;
}

int history_timestamp(const char *line) {
	const unsigned char *p = (const unsigned char *)line;
	if (p == NULL || *p != '#') {
		return 0;
	}
	while (*++p) {
		if (*p < '0' || *p > '9') {
			return 0;
		}
	}
	return 1;
}

const char * print_bool(int ok) {
	return ok ? "true" : "false";
}

/*
func uitoa(val uint) string {
	if val == 0 { // avoid string allocation
		return "0"
	}
	var buf [20]byte // big enough for 64bit value base 10
	i := len(buf) - 1
	for val >= 10 {
		q := val / 10
		buf[i] = byte('0' + val - q*10)
		i--
		val = q
	}
	// val < 10
	buf[i] = byte('0' + val)
	return string(buf[i:])
}
*/

void uitoa(char *dst, ssize_t val) {
	if (val == 0) {
		*dst = '0';
		return;
	}
	char buf[20];
	int i = sizeof(buf) - 1;
	while (val >= 10) {
		ssize_t q = val / 10;
		buf[i] = (char)('0' + val - q*10);
	}
}

int main() {
	char *home = home_directory();
	if (!home) {
		return 1;
	}
	char *hist = join_paths(home, ".bash_history");
	printf("hist: %s\n", hist);

	FILE *fp = fopen(hist, "r");
	if (!fp) {
		perror("open");
		return 1;
	}

	size_t len = 16 * 1024;
	char *buf = malloc(len);
	if (!buf) {
		fprintf(stderr, "OOM\n");
		return 1;
	}
	// ssize_t getline(char **lineptr, size_t *n, FILE *stream);

	ssize_t n;
	ssize_t line = 0;
	while ((n = getline(&buf, &len, fp)) > 0) {
		if (!history_timestamp(buf)) {
			printf("%ld: %s", ++line, buf);
		}
	}
	free(buf);

	return 0;
}

/*
printf("#1312313: %s\n", print_bool(history_timestamp("#1312313")));
printf("#foo: %s\n", print_bool(history_timestamp("#foo")));
printf("#1231 A: %s\n", print_bool(history_timestamp("#1231 A")));
*/
