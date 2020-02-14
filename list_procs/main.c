#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>

#include <sys/sysctl.h>

//	struct kinfo_proc {
//		struct  extern_proc kp_proc;                    /* proc structure */
//		struct  eproc {
//			struct  proc *e_paddr;          /* address of proc */
//			struct  session *e_sess;        /* session pointer */
//			struct  _pcred e_pcred;         /* process credentials */
//			struct  _ucred e_ucred;         /* current credentials */
//			struct   vmspace e_vm;          /* address space */
//			pid_t   e_ppid;                 /* parent process id */
//			pid_t   e_pgid;                 /* process group id */
//			short   e_jobc;                 /* job control counter */
//			dev_t   e_tdev;                 /* controlling tty dev */
//			pid_t   e_tpgid;                /* tty process group id */
//			struct  session *e_tsess;       /* tty session pointer */
//	#define WMESGLEN        7
//			char    e_wmesg[WMESGLEN + 1];    /* wchan message */
//			segsz_t e_xsize;                /* text size */
//			short   e_xrssize;              /* text rss */
//			short   e_xccount;              /* text references */
//			short   e_xswrss;
//			int32_t e_flag;
//	#define EPROC_CTTY      0x01    /* controlling tty vnode active */
//	#define EPROC_SLEADER   0x02    /* session leader */
//	#define COMAPT_MAXLOGNAME       12
//			char    e_login[COMAPT_MAXLOGNAME];     /* short setlogin() name */
//			int32_t e_spare[4];
//		} kp_eproc;
//	};

typedef struct {
	pid_t pid;
	pid_t ppid;
	int   basename_offset;
	char  *args;
} process_t;

typedef struct {
	size_t    count;
	process_t *procs;
} process_list_t;

char * get_process_command_line(struct kinfo_proc *k, int* basename_offset) {
	// This function is from the old Mac version of htop. Originally from ps?
	int mib[3];

	char *procargs;
	char *retval;

	/* Get the maximum process arguments size. */
	mib[0] = CTL_KERN;
	mib[1] = KERN_ARGMAX;

	int argmax;
	size_t size = sizeof(argmax);
	if (sysctl(mib, 2, &argmax, &size, NULL, 0) == -1) {
		fprintf(stderr, "error: %s (%s:%d)\n", __FUNCTION__, __FILE__, __LINE__);
		goto ERROR_A;
	}

	/* Allocate space for the arguments. */
	procargs = malloc(argmax);
	if ( procargs == NULL ) {
		fprintf(stderr, "error: %s (%s:%d)\n", __FUNCTION__, __FILE__, __LINE__);
		goto ERROR_A;
	}

	/*
	 * Make a sysctl() call to get the raw argument space of the process.
	 * The layout is documented in start.s, which is part of the Csu
	 * project.  In summary, it looks like:
	 *
	 * /---------------\ 0x00000000
	 * :               :
	 * :               :
	 * |---------------|
	 * | argc          |
	 * |---------------|
	 * | arg[0]        |
	 * |---------------|
	 * :               :
	 * :               :
	 * |---------------|
	 * | arg[argc - 1] |
	 * |---------------|
	 * | 0             |
	 * |---------------|
	 * | env[0]        |
	 * |---------------|
	 * :               :
	 * :               :
	 * |---------------|
	 * | env[n]        |
	 * |---------------|
	 * | 0             |
	 * |---------------| <-- Beginning of data returned by sysctl() is here.
	 * | argc          |
	 * |---------------|
	 * | exec_path     |
	 * |:::::::::::::::|
	 * |               |
	 * | String area.  |
	 * |               |
	 * |---------------| <-- Top of stack.
	 * :               :
	 * :               :
	 * \---------------/ 0xffffffff
	 */
	mib[0] = CTL_KERN;
	mib[1] = KERN_PROCARGS2;
	mib[2] = k->kp_proc.p_pid;

	size = (size_t)argmax; // TODO: this can be done earlier
	if (sysctl(mib, 3, procargs, &size, NULL, 0) == -1) {
		fprintf(stderr, "error: %s (%s:%d)\n", __FUNCTION__, __FILE__, __LINE__);
		goto ERROR_B;
	}

	int nargs;
	memcpy(&nargs, procargs, sizeof(nargs)); // copy int into nargs
	const char *end = procargs + size;

	char *cp = memchr(procargs + sizeof(nargs), '\0', size);
	if (!cp || cp == end) {
		fprintf(stderr, "error: %s (%s:%d)\n", __FUNCTION__, __FILE__, __LINE__);
		goto ERROR_B;
	}
	cp++;

	// TODO: inc cp before looping
	//
	// Skip trailing '\0' characters.
	while (cp < end) {
		if (*cp++ != '\0') {
			break;
		}
	}
	if (cp == end) {
		fprintf(stderr, "error: %s (%s:%d)\n", __FUNCTION__, __FILE__, __LINE__);
		goto ERROR_B;
	}

	// Save where the argv[0] string starts.
	char *sp = cp;
	char *np;
	int c = 0;
	*basename_offset = 0;

	for (np = NULL; c < nargs && cp < end; cp++) {
		if (*cp == '\0') {
			c++;
			if (np) {
				*np = ' '; // replace '\0' with ' '
			}
			np = cp;
			if (*basename_offset == 0) {
				*basename_offset = cp - sp;
			}
		}
	}

	// sp points to the beginning of the arguments/environment string, and
	// np should point to the '\0' terminator for the string.
	if (np == NULL || np == sp) {
		fprintf(stderr, "error: %s (%s:%d)\n", __FUNCTION__, __FILE__, __LINE__);
		goto ERROR_B;
	}
	if (*basename_offset == 0) {
		*basename_offset = np - sp;
	}

	retval = strdup(sp); // copy the args

	free(procargs); // cleanup

	return retval;

ERROR_B:
	free(procargs);
	return NULL;

ERROR_A:
	*basename_offset = 0;
	return NULL;
}

static process_t process_from_kinfo_proc(struct kinfo_proc *ps) {
	struct extern_proc *ep = &ps->kp_proc;
	process_t p = {
		.pid = ep->p_pid,
		.ppid = ps->kp_eproc.e_ppid,
	};
	p.args = get_process_command_line(ps, &p.basename_offset);
	return p;
}

int get_process_list(process_list_t *list) {
	int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_ALL, 0};
	struct kinfo_proc *processes = NULL;

	size_t count;
	if (sysctl(mib, 4, NULL, &count, NULL, 0) < 0) {
		fprintf(stderr, "Error: unable to get size of kproc_infos\n");
		goto err_cleanup;
	}

	count += sizeof(struct kinfo_proc) * 32; // pad count
	processes = malloc(count);
	if (!processes) {
		fprintf(stderr, "Error: OOM\n");
		goto err_cleanup;
	}

	if (sysctl(mib, 4, processes, &count, NULL, 0) < 0) {
		fprintf(stderr, "Error: unable to get kinfo_procs\n");
		goto err_cleanup;
	}

	// TODO: allow reuse of the process_list
	list->count = count / sizeof(struct kinfo_proc);
	list->procs = malloc(sizeof(process_t) * list->count);
	if (!list->procs) {
		fprintf(stderr, "Error: OOM\n");
		goto err_cleanup;
	}
	for (size_t i = 0; i < list->count; i++) {
		list->procs[i] = process_from_kinfo_proc(&processes[i]);
	}

	free(processes);
	return 0;

err_cleanup:
	if (processes) {
		free(processes);
	}
	return 1;
}

int has_suffix(const char *s, const char *suffix) {
	size_t s_len = strlen(s);
	size_t sfx_len = strlen(suffix);
	if (s_len < sfx_len) {
		return 0;
	}
	if (s_len == sfx_len) {
		return memcmp(s, suffix, sfx_len) == 0;
	}
	const char *p = s + (s_len - sfx_len);
	return memcmp(p, suffix, sfx_len) == 0;
}

int main() {
	process_list_t list;
	if (get_process_list(&list) != 0) {
		fprintf(stderr, "ERROR\n");
		return 1;
	}
	for (size_t i = 0; i < list.count; i++) {
		printf("pid: %d ppid: %d args: %.*s\n", list.procs[i].pid,
			list.procs[i].ppid, list.procs[i].basename_offset, list.procs[i].args);
	}
	printf("count: %zu\n", list.count);

	// WARN: testing only (prints cmd line then just args)
	// NOTE: basename_offset is just the start of args
	for (size_t i = 0; i < list.count; i++) {
		printf("%d: %s\n\t%s\n", list.procs[i].basename_offset, list.procs[i].args,
				list.procs[i].args + list.procs[i].basename_offset);
	}

	return 0;
}

/*
const char * skip_to(const char *s1, const char *s2) {
	const unsigned char *p1 = (const unsigned char *)s1;
	const unsigned char *p2 = (const unsigned char *)s2;
	for ( ; p1 < p2; p1++) {
		if (*p1 == '\0') {
			return (const char *)p1;
		}
	}
	return NULL;
	while (p1 < p2 && *p1 != '\0') {
		p1++;
	}
	return p1 < p2 ? (const char *)p1 : NULL;
}

const char * skip_to_x(const char *s1, const char *s2) {
	const unsigned char *p1 = (const unsigned char *)s1;
	const unsigned char *p2 = (const unsigned char *)s2;
	while (p1 < p2 && *p1 != '\0') {
		p1++;
	}
	return p1 < p2 ? (const char *)p1 : NULL;
}
*/
