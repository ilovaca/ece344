#ifndef _SYSCALL_H_
#define _SYSCALL_H_

/*
 * Prototypes for IN-KERNEL entry points for system call implementations.
 */

int sys_reboot(int code);

// int sys_execv(const char *prog_path, char **args);

int sys_fork(struct trapframe *, int32_t *);

int sys_waitpid(int child_pID, int status, int *retval);

int sys_getpid(int32_t*);

int sys__exit(int , int32_t* );

int sys_read(struct trapframe *, int32_t *);

int sys_write(int fd, char* c, size_t size, int* retval);

int sys_sbrk(int increment, int32_t*);

int runprogram_execv(char *progname, int argc, char* argv[]);

int runprogram(char *progname);

int runprogram_exev_syscall(char *progname, char* args[], int nargs);

#endif /* _SYSCALL_H_ */
