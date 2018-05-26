#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>

#ifndef likely
#define likely(x)   __builtin_expect(!!(x), 1)
#endif
#ifndef unlikely
#define unlikely(x) __builtin_expect(!!(x), 0)
#endif

// static const int NC_ALIGNMENT = 4096;

int main() {
	return 0;
}

/*
func findIndex(b []byte) (int, int) {
	// Pattern: \x1b\[[0-?]*[ -/]*[@-~]
	const minLen = 2 // "\\[[@-~]"

	start := bytes.IndexByte(b, '\x1b')
	if start == -1 || len(b)-start < minLen || b[start+1]-'@' > '_'-'@' {
		return -1, -1
	}

	n := start + 2 // ESC + second byte [@-_]

	// parameter bytes
	for ; n < len(b) && b[n]-'0' <= '?'-'0'; n++ {
	}
	// intermediate bytes
	for ; n < len(b) && b[n]-' ' <= '/'-' '; n++ {
	}
	// final byte
	if n < len(b) && b[n]-'@' <= '~'-'@' {
		return start, n + 1
	}
	return -1, -1
}

func (c *NoColor) Strip(b []byte) []byte {
	c.buf = c.buf[:0]
	for {
		start, end := findIndex(b)
		if start == -1 {
			break
		}
		if start > 0 {
			c.buf = append(c.buf, b[:start]...)
		}
		if end < len(b) {
			b = b[end:]
		} else {
			b = nil
			break
		}
	}
	c.buf = append(c.buf, b...)
	return c.buf
}
*/
