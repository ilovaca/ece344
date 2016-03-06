#ifndef _SYSCALL_H_
#define _SYSCALL_H_

/*
 * Prototypes for IN-KERNEL entry points for system call implementations.
 */

int sys_reboot(int code);

int sys_execv(/* argc and argv */);

int md_forkentry(struct trapframe *tf, int32_t*);

int sys_waitpid();

int sys_getpid(int32_t*);

int sys__exit();

int sys_read();

int sys_write();

#endif /* _SYSCALL_H_ */
