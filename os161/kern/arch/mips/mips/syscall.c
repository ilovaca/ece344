#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <machine/pcb.h>
#include <machine/spl.h>
#include <machine/trapframe.h>
#include <kern/callno.h>
#include <syscall.h>
#include <thread.h>
#include <addrspace.h>
#include <array.h>
// Kernel process table
extern struct array * PCBs;
extern unsigned int pid_count;
extern struct thread* curthread;
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
		err = sys_execv(tf);
		break;
		case SYS_fork:
		err = sys_fork(tf, &retval);
		break;
		case SYS_waitpid:
		err = sys_waitpid(tf, &retval);
		break;
		case SYS_getpid:
		err = sys_getpid(&retval);
		break;
		case SYS__exit:
		err = sys__exit();
		break;
		case SYS_read:
		err = sys_read(tf, &retval);
		break;
		case SYS_write:
		err = sys_write(tf, &retval);
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
md_forkentry(struct trapframe* tf, struct addrspace* vmspace)
{
	assert (curspl == 1);
	// (struct trapframe*) tf;
	// (struct addrspace*) vmspace;
	//tf->tf_epc += 4;
	struct addrspace * child_vmspace = NULL;
	// copy the parent vmspace
	int result = as_copy(vmspace, &child_vmspace);
	if (result) {
		return result;
	}
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

	// we should never reach here
	panic("switching to user mode returned\n");
	return 0;
}

/**
	Function to allocate a PID in the range [1 : MAX_PID]
	If all pids are used up, returns an error --> try again later.
*/

int allocate_PID(unsigned int * to_pid){
	int spl = splhigh();
	int i;
	if (pid_count >= MAX_PID - 1) {  
		//if the most recently used pid is outside the MAX_PID range, 
		// we start over at the very beginning. 
		pid_count = 1;
	}
		//  we find the next available pid  
	for (i = pid_count; i < MAX_PID; i++) {
		if (array_getguy(PCBs, i) == NULL){ 
			pid_count = i;
			*to_pid = i;
			splx(spl);
			return 0;
		}
	} 
	for (i = pid_count; i > 1; i--) {
		// if we didn't find an available pid in the 2nd half
		// of the pcb array, it must be in the first half, if 
		// there is one
		if (array_getguy(PCBs, i) == NULL){ 
			pid_count = i;
			*to_pid = i;
			splx(spl);
			return 0;
		}		
	}

	splx(spl);
	// reaching this line means there's no avaiable pid, try again later
	return EAGAIN;
}


/**
	Things to do in sys_fork():
	1. Copy the 
*/

int sys_fork(struct trapframe *tf, int * retval){
	/* In parent process */
	int spl = splhigh();
	int result;
	struct thread* child_thread = NULL; 
	// duplicate the parent's trapframe, note this trapframe is on kernel heap
	struct  trapframe* child_tf = kmalloc(sizeof(struct trapframe));
	if(child_tf == NULL){
		//return no-memory
		splx(spl);
		return ENOMEM;
	}
	*child_tf = *tf; 

	// assign pID to child process
	result = allocate_PID(&(child_thread->pID));
	if (result) {
		kfree(child_tf);
		return result;
	}
	// create child thread/process
	result =  thread_fork("child_process", 
		(void*)child_tf, (unsigned long) curthread->t_vmspace, 
		md_forkentry,
		&child_thread);

	// place child process's pcb into process table
	array_setguy(PCBs, child_thread->pID, child_thread);
	// add child process to parent, place the child process'PID to children. 
	if(curthread->children == NULL)
	{
		curthread->children = array_create();
	}
	array_add(curthread->children, &child_thread->pID);

	if(result) {
		splx(spl);
		return result;
	}
	/* parent process returns PID */
	*retval = child_thread->pID;
	splx(spl);
	return 0;

}


int sys_getpid(int32_t * retval){
	int spl = splhigh();
	assert(curthread->pID != 0);
	*retval = curthread->pID;
	splx(spl);
	return 0;
}


/* Do two things 
	1. put all children processes to init
	2. set exited bit and signal parent
	3. call thread_exit
*/
int sys__exit() {
	int result = splhigh();
	int i = 0;
	struct thread* init = array_getguy(PCBs, 1);
	// put all its children to init process
	for(;i<array_getnum(curthread->children);i++){

		array_add(init->children, array_getguy(curthread->children,i));
	}

	curthread->t_pcb.exited = 1; //indicates that the current thread has terminated.
	//signal the parent process
	thread_wakeup_single(curthread);
	splx(result);
	//here will delete address space, and it does not return.
	thread_exit(); 
	return 0;
}

/* 
	Basic Idea:
	1. check if the supplied pid IS a child of the calling process. If not, return error
	2. check if the child process has already exited:
			(1). if so, delete its PCB.
			(2). if not, sleep until it is waken up
*/
int sys_waitpid(struct trapframe* tf, int32_t* retval){
	u_int32_t child_pID = tf->tf_a0;
	struct thread* child = NULL;
	int i = 0;
	int spl = splhigh();
	for( ; i<array_getnum(curthread->children); i++){
		child = array_getguy(curthread->children,i);
		if(child->pID == child_pID){
			break;
		}
		child = NULL;
	}
	if(child == NULL){
		splx(spl);
		return EINVAL;
	}

	//if child process has terminated
	if(child->t_pcb.exited == 1){ 
		array_setguy(PCBs, child_pID, NULL);
		retval = child_pID;
		splx(spl);
		return 0;
	}
	else{ //if child process is still running
		thread_sleep(child);
		/* after the parent process wakes up, it returns the child-pID*/
		array_setguy(PCBs,child_pID,NULL);
		retval = child_pID; 
	}
	splx(spl);
	return 0;
}

int sys_read(struct trapframe * tf, int32_t* retval)
{
	int fd = tf->tf_a0;
	void * user_buf = tf->tf_a1;
	int read_count = tf->tf_a2;
	int * kernel_buf = kmalloc(sizeof(int)*read_count);
	if(fd != 0) {
		// must be STDIN
		return EINVAL;
	} else {
		
		int i = 0;
		for( ; i<read_count; i++){
			kernel_buf[i] = getch();
		}
		/* copies LEN bytes from a kernel-space address SRC to a
 	 user-space address USERDEST.*/
		copyout(kernel_buf, (userptr_t) user_buf, read_count);
	}
	kfree(kernel_buf);
	return 0;
}

/* from user to kernel*/
int sys_write(struct trapframe * tf, int32_t* retval) { 
	int fd = tf->tf_a0;
	void * user_buf = tf->tf_a1;
	int write_count = tf->tf_a2;
	int * kernel_buf = kmalloc(sizeof(int)*write_count);
	if (fd != 1) {
		// must be STDOUT
		return EINVAL;
	} else {
		/* copies LEN bytes from a user-space address USERSRC to a
 		* kernel-space address DEST.*/
		copyin((const_userptr_t)user_buf, kernel_buf, write_count);
		int i = 0;
		for(; i < write_count; i++){
			 kprintf("%c", (char)kernel_buf[i]);
		}
	}
}



int sys_execv(struct trapframe* tf){
	//clear the current thread's address space

	int char_count = 0;
	// kernel buf for program path
	char* prog_path = kmalloc(sizeof(char) * MAX_ARG_LEN);
	int result = copyinstr((const_userptr_t) tf->tf_a0, (void *)prog_path, MAX_ARG_LEN, &char_count);
	if (result) {
		kfree(prog_path);
		return result;
	}

	// kernel buf for program args (char **)
	char** argv = kmalloc(sizeof(char*) * MAX_ARGC);
	int i = 0;
	for (; i< MAX_ARGC; i++){
		argv[i] = kmalloc(sizeof(char) * MAX_ARG_LEN);
	}
	int argc = 0;
	int num_read = 0;
	do {
		copyinstr((const_userptr_t)tf->tf_a1, (void *)argv[argc], MAX_ARG_LEN, &num_read);
		if (num_read != 0) argc++; //tf_a1 stores a pointer.
	} while (num_read != 0);

	assert(curthread->t_vmspace != NULL);
	as_destroy(curthread->t_vmspace);
	// prog_path and argv is in kernel
	runprogram_execv(prog_path, argc, argv);

}