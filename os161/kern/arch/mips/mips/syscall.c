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
#include <vfs.h>
#include "syscall.h"
#include <kern/unistd.h>

// Kernel process table
extern pcb_t * PCBs[MAX_PID];
extern struct lock* lock;
extern struct thread* curthread;

void destroy_pcb_unit(u_int32_t pID);

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
		// err = sys_execv_(tf);
		err = sys_execv(tf->tf_a0, tf->tf_a1);
		break;
		case SYS_fork:
		err = sys_fork(tf, &retval);
		break;
		case SYS_waitpid:
		err = sys_waitpid(tf->tf_a0,tf->tf_a1, &retval);
		break;
		case SYS_getpid:
		err = sys_getpid(&retval);
		break;
		case SYS__exit:
		err = sys__exit(tf->tf_a0, &retval);
		break;
		case SYS_read:
		err = sys_read(tf, &retval);
		break;
		case SYS_write:
		// err = sys_write(tf, &retval);
		err = sys_write(tf->tf_a0, tf->tf_a1, tf->tf_a2, &retval);
		break;
		case SYS_sbrk:
		err = sys_sbrk(tf->tf_a0, &retval);
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
	// int spl = splhigh();	
	//kprintf(" process %d is in enter md_forkentry\n",curthread->pID);
	struct trapframe child_tf;
	struct trapframe *tf_parent = (struct trapframe*) tf;
	assert(tf_parent != NULL);
	// memcpy(child_tf, tf, sizeof(struct trapframe));


	tf_parent->tf_epc += 4;
	//we set the return value to be 0.
	tf_parent->tf_v0 = 0;
	tf_parent->tf_a3 = 0;

	// Load Address Space of Child and Activate it.

	curthread->t_vmspace = (struct addrspace*)vmspace;
	assert(curthread->t_vmspace != NULL);
	as_activate(curthread->t_vmspace);

	child_tf = *tf_parent;
	mips_usermode(&child_tf);

	// we should never reach here
	panic("switching to user mode returned\n");
	return 0;
}

/**
	Function to allocate a PID in the range [1 : MAX_PID]
	If all pids are used up, returns an error --> try again later.
*/


int allocate_PID(unsigned int * to_pid){
	// int spl = splhigh();
	assert(curspl > 0);
	int i;
	//assert(process_counter >= 1 && process_counter < MAX_PID);

	//  we find the first available slot 
	for (i = 1; i < MAX_PID; i++) {
		if (PCBs[i] == NULL){ 
			*to_pid = i;
			return 0;
		}
	} 
	
	// reaching this line means there's no avaiable pid, try again later
	return EAGAIN;
}

// #define CV_IMPL 1


int sys_fork(struct trapframe *tf, int * retval){
	int spl = splhigh();
	struct addrspace * child_vmspace;
	// copy the parent's address space
	int result = as_copy(curthread->t_vmspace, &child_vmspace);
	if(result){
		splx(spl);
		return result;
	}
	struct thread *child_thread = NULL;
	// duplicate the parent's trapframe, which is on this kernel thread's stack
	// we need to copy the trapframe to kernel heap
	struct  trapframe* child_tf = kmalloc(sizeof(struct trapframe));

	if (child_tf == NULL) {
		splx(spl);
		return ENOMEM;
	}	
	*child_tf = *tf; 


	// create child thread/process
	result =  thread_fork("child_process", 
		(void*)child_tf, (unsigned long)child_vmspace, 
		md_forkentry,
		&child_thread);

	assert(child_thread != NULL);
	// parent returns the child pID
	*retval = child_thread->pID;
	splx(spl);
	return 0;

}



	

int sys_getpid(int32_t * retval){
	int spl = splhigh();
	*retval = curthread->pID;
	splx(spl);
	return 0;
}


/* Do two things 
	1. put all children processes to init
	2. set exited bit and save exit code 
	3. wakeup parent
	4. call thread_exit to perform cleanup
*/

int sys__exit(int exitcode, int32_t* retval) {

	int result = splhigh();
	
	struct thread* init = PCBs[1]-> this_thread;
	assert(init != NULL);

	int current_pid = curthread->pID;
	int i = 0;
	// inherit all children of curthread to init 
	for(; i < MAX_PID; i++){ 
		if(PCBs[i] != NULL){
			if(PCBs[i]->parent == current_pid)
			{
				PCBs[i]->parent = 1;
			}
		}
	}

	*retval = 0;

	splx(result);

	thread_exit(); //here handles address space deletion !
	return 0;
}
	

/* 
	Basic Idea:
	1. check if the supplied pid IS a child of the calling process. If not, return error
	2. check if the child process has already exited:
			(1). if so, delete its PCB.
			(2). if not, sleep until it is waken up
*/

int sys_waitpid(int child_pID, int status, int *retval) {
	int spl = splhigh();
	int result;
	int* status_user = &status;

	u_int32_t* desired_pID = NULL;

	int status_kernel = 0;
	if(PCBs[curthread->pID] == NULL)
		panic("Bad access to PCBs !");

	if (status_user == NULL ) {
		splx(spl);
		return EFAULT;
	}

	if (child_pID < MIN_PID || child_pID > MAX_PID) {
		splx(spl);
		return EINVAL;
	}


	if (child_pID == curthread->pID) {
		splx(spl);
		return EINVAL;
	}



	//check if the process with the child_pID belongs to the current process:
	if(PCBs[child_pID]->parent != curthread->pID){
		splx(spl);
		return EINVAL;
	}
				
	if(PCBs[child_pID] == NULL){
			panic("wtf !");
		}

	//if child process has terminated...
	if(PCBs[child_pID]->exited == 1){ 
		assert(PCBs[child_pID] != NULL);
		status_kernel = PCBs[child_pID]->exit_code;
		// reap the child
		destroy_pcb_unit(child_pID);
		// result = copyout((const void *) &status_kernel, (userptr_t) status_user, sizeof(int));

		// if(result){
		// 	splx(spl);
		// 	return EFAULT;
		// }
		*retval = child_pID;
		splx(spl);
		return 0;
	}
	else{ //if child process is still running
		assert(PCBs[child_pID] != NULL);
		
			while(PCBs[child_pID]->exited != 1){
				thread_sleep(child_pID);
			}

		status_kernel = PCBs[child_pID]->exit_code;
		destroy_pcb_unit(child_pID);
		// result = copyout((const void *) &status_kernel, (userptr_t) status_user, sizeof(int));
		// if(result){
			splx(spl);
		// 	return EFAULT;
		// }
		*retval = child_pID; 
		return 0;
	}



}

void destroy_pcb_unit(u_int32_t pID)
{
	int result = splhigh();
	if(PCBs[pID] == NULL){
		splx(result);
		return;
	}
	else
	{
		if(PCBs[pID]->mutex != NULL){
			sem_destroy(PCBs[pID]->mutex);
			PCBs[pID]->mutex = NULL;
		}
		PCBs[pID]->this_thread = NULL;
		kfree(PCBs[pID]);
		PCBs[pID] = NULL;
		splx(result);
		return;
	}

}


int sys_read(struct trapframe * tf, int32_t* retval)
{
	int fd = tf->tf_a0;
	char * user_buf = (char*)tf->tf_a1;
	int read_count = tf->tf_a2;
	if(read_count == 0 || user_buf == NULL || fd != 0){

		return -1;
	}
	

	char kbuf;
	int result;
	kbuf = getch();
	result = copyout(&kbuf, (userptr_t) user_buf, 1);
	if(result != 0)
		return result;
	*retval = 1;
	return 0;	
}



int sys_write(int fd, char* c, size_t size, int* retval) {
	int spl = splhigh();
  	(void*) fd;
	if(size == 1){
		char buf[2];
	    buf[0] = *c;
	    buf[1] = '\0';
	    kprintf("%s",buf);
	    splx(spl);
	    return 0;
	} else {
		char * kernel_buf = kmalloc(sizeof(char) * (size + 1));
		copyin((const_userptr_t)c, kernel_buf, size);
		kernel_buf[size] = '\0';
		int i = 0;
		for(; i < size; i++){
			// kprintf("%c", kernel_buf[i]);	
			putch(kernel_buf[i]);
		}
		*retval = size;
		kfree(kernel_buf);
		splx(spl);
	    return 0;
	}
}


#define MAX_PATH_LEN 128
int sys_execv(const char *prog_path, char **args) {
	int spl = splhigh();
	// the prog_path is in user space, we need to copy it into kernel space
	if(prog_path == NULL) {
		return EINVAL;
	}
	// frist the program path
	char* program = (char *) kmalloc(MAX_PATH_LEN * sizeof(char));
	int size;
	int result = copyinstr((const_userptr_t) prog_path, program, MAX_PATH_LEN, &size);
	if(result) {
		kfree(program);
		return EFAULT;
	}
	// arguments
	char** argv = (char**) kmalloc(sizeof(char**));
	// char** argv;
	result = copyin((const_userptr_t)args, argv, sizeof(char **));
	if(result) {
		kfree(program);
		kfree(argv);
		return EFAULT;
	}
	// count the number of arguments
	int i = 0;
	while(args[i] != NULL){
		argv[i] = (char*) kmalloc(sizeof(char) * MAX_ARG_LEN);
		if(copyinstr((const_userptr_t) args[i], argv[i], MAX_ARG_LEN, &size) != 0) {
			return EFAULT;
		}
		i++;
	}
	argv[i] = NULL;
	// runprogram_exev_syscall(program, argv, i);
	/*********************** runprogram starts*****************/

	struct vnode *v;
	vaddr_t entrypoint, stackptr;	

	result = vfs_open(program, O_RDONLY, &v);

	if (result) {
		return result;
	}

	// destroy the old addrspace
	if(curthread->t_vmspace != NULL){
		as_destroy(curthread->t_vmspace);
		curthread->t_vmspace = NULL;
	}

	assert(curthread->t_vmspace == NULL);

	// Create a new address space. 
	curthread->t_vmspace = as_create();
	if (curthread->t_vmspace==NULL) {
		vfs_close(v);
		return ENOMEM;
	}

	// Activate it. 
	as_activate(curthread->t_vmspace);

	// Load the executable. 
	result = load_elf(v, &entrypoint); // Load an ELF executable user program into the current address space and
	// returns the entry point (initial PC) for the program in ENTRYPOINT.
	if (result) {
		vfs_close(v);
		return result;
	}
	vfs_close(v);

	result = as_define_stack(curthread->t_vmspace, &stackptr);
	if (result) {
		// thread_exit destroys curthread->t_vmspace 
		return result;
	}

	int narg = i - 1;
	int j;
	for(j = 0; j < narg; ++j){
	    int len = 1 + strlen(argv[j]);
	    len = ROUNDUP(len, 8);
		stackptr -= len;

		result = copyoutstr(argv[j],(userptr_t)stackptr, len, &len); 
		if(result){
			return result;
		}	
		argv[j] = (char*)stackptr;
    }

    argv[i] = NULL;
    size_t arg_size = (narg + 1) * sizeof(char*);
    arg_size = ROUNDUP(arg_size, 8);
    // align the stackptr to 8 byte aligned
    stackptr -= arg_size;
    // stackptr -= stackptr % 8;

    copyout(argv, stackptr, (narg + 1) * 4);
	md_usermode(i, stackptr, stackptr, entrypoint); 
	panic("md_usermode returned\n");
	return EINVAL;
}


int sys_sbrk(int incr, int32_t* retval) {
	// Nore incr can be negative
	struct addrspace* as = curthread->t_vmspace;
	if (as->heap_end + incr < as->heap_start) {
		// incr too negative... falling off the cliff
		return EINVAL;
	}
	if (as->heap_end + incr >= USERSTACK - 24 * 4096){
		return ENOMEM;
	}
	// make it 4 bytes aligned
	// incr = ROUNDUP(incr, sizeof(void*));
	// assert(incr % 4 == 0);
	*retval = as->heap_end;
	as->heap_end += incr;
	return 0;
}
