#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <machine/pcb.h>
#include <machine/spl.h>
#include <machine/trapframe.h>
#include <kern/callno.h>
#include <syscall.h>

extern struct array * PCBs;
extern unsigned int pid_count;
/*
 * System call handler.
 *
 * A pointer to the trapframe created during exception entry (in
 * exception.S) is passed in.
 *
 * The calling conventions for syscalls are as follows: Like ordinary
 * function calls, the first 4 32-bit arguments are passed in the 4
 * argument registers a0-a3. In addition, the system call number is
 * passed in the v0 register.
 *
 * On successful return, the return value is passed back in the v0
 * register, like an ordinary function call, and the a3 register is
 * also set to 0 to indicate success.
 *
 * On an error return, the error code is passed back in the v0
 * register, and the a3 register is set to 1 to indicate failure.
 * (Userlevel code takes care of storing the error code in errno and
 * returning the value -1 from the actual userlevel syscall function.
 * See src/lib/libc/syscalls.S and related files.)
 *
 * Upon syscall return the program counter stored in the trapframe
 * must be incremented by one instruction; otherwise the exception
 * return code will restart the "syscall" instruction and the system
 * call will repeat forever.
 *
 * Since none of the OS/161 system calls have more than 4 arguments,
 * there should be no need to fetch additional arguments from the
 * user-level stack.
 *
 * Watch out: if you make system calls that have 64-bit quantities as
 * arguments, they will get passed in pairs of registers, and not
 * necessarily in the way you expect. We recommend you don't do it.
 * (In fact, we recommend you don't use 64-bit quantities at all. See
 * arch/mips/include/types.h.)
 */

/* call functions based on the call number.  */
void
mips_syscall(struct trapframe *tf)
{
	int callno;
	int32_t retval;
	int err;

	assert(curspl==0);

	callno = tf->tf_v0; // the system call number is
 	//passed in the v0 register.

	/*
	 * Initialize retval to 0. Many of the system calls don't
	 * really return a value, just 0 for success and -1 on
	 * error. Since retval is the value returned on success,
	 * initialize it to 0 by default; thus it's not necessary to
	 * deal with it except for calls that return other values, 
	 * like write.
	 */

	retval = 0;

	switch (callno) {
	    case SYS_reboot:
		err = sys_reboot(tf->tf_a0);
		break;
		case SYS_execv:
		err = sys_execv();
		break;
		case SYS_fork:
		err = sys_fork(tf, &retval);
		break;
		case SYS_waitpid:
		err = sys_waitpid(/* waitpid(int pid)*/);
		break;
		case SYS_getpid:
		err = sys_getpid(/* return the*/);
		break;
		case SYS__exit:
		err = sys__exit();
		break;
		case SYS_read:
		err = sys_read();
		break;
		case SYS_write:
		err = sys_write();
		break;

	    /* Add stuff here */
 
	    default:
		kprintf("Unknown syscall %d\n", callno);
		err = ENOSYS;
		break;
	}


	if (err) {
		/*
		 * Return the error code. This gets converted at
		 * userlevel to a return value of -1 and the error
		 * code in errno.
		 */
		tf->tf_v0 = err;
		tf->tf_a3 = 1;      /* signal an error */
	}
	else {
		/* Success. */
		tf->tf_v0 = retval;
		tf->tf_a3 = 0;      /* signal no error */
	}
	
	/*
	 * Now, advance the program counter, to avoid restarting
	 * the syscall over and over again.
	 */
	
	tf->tf_epc += 4;

	/* Make sure the syscall code didn't forget to lower spl */
	assert(curspl==0);

}


/* This is the entry point of the forked child process */
int
md_forkentry(void* tf, unsigned long vmspace)
{
	(struct trapframe*) tf;
	(struct addrspace*) vmspace;
	//tf->tf_epc += 4;
	struct addrspace * child_vmspace = NULL;
	// copy the parent vmspace
	int result = as_copy(vmspace, &child_vmspace);
	// we are a newly created thread with no vmspace!
	assert(curthread-> t_vmspace == NULL);
	// assign the copied addr space to this child process
	curthread->t_vmspace = child_vmspace;
	as_activate(curthread->t_vmspace);

	// make sure the child start executing after the fork
	tf->tf_epc += 4;
	//we set the return value to be 0.
	tf->tf_v0 = 0;
	// jump back to where the fork was called
	md_usermode(tf->tf_a0, tf->tf_a1, tf->tf_sp, tf->tf_epc);

}

unsigned int allocate_PID(){
	int spl = splhigh();
	if (pid_count >= MAX_PID) {  //if pid_count >= MAX_PID, we reset the pid_count to be 1, and we check from 1. 
		pid_count = 1;
	}
		// if currently the pid falls within the 0~MAX_PID range, we find the next
		// available 
	for (int i = pid_count; i < MAX_PID; i++) {
		if (array_getguy(PCBs, i) == NULL){ 
			pid_count = i;
			splx(spl);
			return i;
		}
	}

	splx(spl);
	return EAGAIN;
}

int sys_fork(struct trapframe *tf, int * retval){
	/* In parent process */
	int result;
	struct thread* child_thread; 
	// duplicate this parent's trapframe, note this trapframe is on kernel heap
	struct  trapframe* child_tf = kmalloc(sizeof(struct trapframe));
	if(child_tf == NULL){
		//return no-memory
		return ENOMEM;
	}

	*child_tf = *tf; 
	// assign pID to child process
	child_thread->pID = allocate_PID();
	// create child thread/process
	result =  thread_fork("child_process", 
		(void *)child_tf, (unsigned long)curthread->t_vmspace, 
		md_forkentry,
		&child_thread);

	if(result)
		return result;
	/* parent process returns PID */
	*retval = child_thread->pID;
	return 0;

}
