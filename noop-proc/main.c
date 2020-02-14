#include <signal.h>

int main() {
	sigset_t set = 0;
	sigsuspend(&set);
	return 0;
}
