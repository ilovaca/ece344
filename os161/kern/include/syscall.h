#ifndef _SYSCALL_H_
#define _SYSCALL_H_

/*
 * Prototypes for IN-KERNEL entry points for system call implementations.
 */

int sys_reboot(int code);

int sys_execv(struct trapframe *);

int sys_fork(struct trapframe *, int32_t *);

int sys_waitpid(struct trapframe *, int32_t *);

int sys_getpid(int32_t*);

int sys__exit();

int sys_read(struct trapframe *, int32_t *);

int sys_write(struct trapframe *, int32_t *);

#endif /* _SYSCALL_H_ */
