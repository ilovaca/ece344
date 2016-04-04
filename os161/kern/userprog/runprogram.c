/*
 * Sample/test code for running a user program.  You can use this for
 * reference when implementing the execv() system call. Remember though
 * that execv() needs to do more than this function does.
 */

#include <types.h>
#include <kern/unistd.h>
#include <kern/errno.h>
#include <lib.h>
#include <addrspace.h>
#include <thread.h>
#include <curthread.h>
#include <vm.h>
#include <vfs.h>
#include <test.h>
#include <array.h>
#include <machine/spl.h>

extern pcb_t* PCBs[MAX_PID];
extern struct thread* curthread;

#define DIVROUNDUP(a,b) (((a)+(b)-1)/(b))
#define ROUNDUP(a,b)    (DIVROUNDUP(a,b)*b)
//extern unsigned int process_counter; 

/* Library call to load and run a program. This is called from the kernel menu,
 * and this function is called in a new thread... (shouldn't it be a new process?)
 * Load program "progname" and start running it in usermode, it doesn't return. 
 * Does not return except on error.
 *
 * Calls vfs_open on progname and thus may destroy it.
 */

/* idea: 
	* open the executable file with vnode v
	* create & activate vmspace for the current thread
	* load the executable
	* define the user stack for the address space
	* change to the user mode. 
*/

int allocate_PID(unsigned int * to_pid);
// argc and argv are in kernel, we need to copy it to the NEW addr space


int
runprogram(char *progname)
{
	struct vnode *v;
	vaddr_t entrypoint, stackptr;
	int result;

	// Open the file. 
	result = vfs_open(progname, O_RDONLY, &v);
	if (result) {
		return result;
	}

	// We should be a new thread. 
	assert(curthread->t_vmspace == NULL);

	// Create a new address space. 
	curthread->t_vmspace = as_create();
	if (curthread->t_vmspace==NULL) {
		vfs_close(v);
		return ENOMEM;
	}

	// Activate it. 
	as_activate(curthread->t_vmspace);
	
	result = load_elf(v, &entrypoint);

	// allocate pid for this forked thread
	if (result) {
		// thread_exit destroys curthread->t_vmspace 
		vfs_close(v);
		return result;
	}

	// Done with the file now. 
	vfs_close(v);

	// Define the user stack in the address space 
	result = as_define_stack(curthread->t_vmspace, &stackptr);
	if (result) {
		// thread_exit destroys curthread->t_vmspace 
		return result;
	}

	// Warp to user mode. 
	md_usermode(0 /*argc*/, NULL ,//userspace addr of argv,
		    stackptr, entrypoint);
	
	// md_usermode does not return 
	panic("md_usermode returned\n");
	return EINVAL;
}



int 
runprogram_exev(char *progname, char* args[], int nargs)
{
	struct vnode *v;
	int narg = nargs;
	vaddr_t entrypoint, stackptr;	
	int result;
	// Open the file. 
	result = vfs_open(progname, O_RDONLY, &v);
	if (result) {
		return result;
	}

	// We should be a new thread. 
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

	// Done with the file now. 
	vfs_close(v);

	// Define the user stack in the address space 
	result = as_define_stack(curthread->t_vmspace, &stackptr);
	if (result) {
		// thread_exit destroys curthread->t_vmspace 
		return result;
	}


	/************************** copy arguments to user stack *****************************/
	int j;
   	for(j = 0; j < narg; ++j){
	    int len = 1 + strlen(args[j]);
	    // len = ROUNDUP(len, 8);
		stackptr -= len;

		result = copyoutstr(args[j],(userptr_t)stackptr, len, &len); 
		if(result){
			return result;
		}	
		// this is fucking weird...
		args[j] = (char*)stackptr;
    }

    args[narg] = NULL;

    size_t arg_size = (narg + 1) * sizeof(char*);
    // align the stackptr to 8 byte aligned
    stackptr -= arg_size;
    stackptr -= stackptr % 8;

    copyout(args, stackptr, arg_size);
	md_usermode(nargs, stackptr, stackptr, entrypoint); 
	panic("md_usermode returned\n");
	return EINVAL;
	
}
int 
runprogram_exev_syscall(char *progname, char* args[], int nargs)
{
	struct vnode *v;
	int narg = nargs;
	vaddr_t entrypoint, stackptr;	
	int result;
	// Open the file. 
	result = vfs_open(progname, O_RDONLY, &v);
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

	// Done with the file now. 
	vfs_close(v);

	// Define the user stack in the address space 
	result = as_define_stack(curthread->t_vmspace, &stackptr);
	if (result) {
		// thread_exit destroys curthread->t_vmspace 
		return result;
	}


	/************************** copy arguments to user stack *****************************/
	int j;
   	for(j = 0; j < narg; ++j){
	    int len = 1 + strlen(args[j]);
		stackptr -= len;

		result = copyoutstr(args[j],(userptr_t)stackptr, len, &len); 
		if(result){
			return result;
		}
		// this is fucking weird...
		args[j] = (char*)stackptr;
    }

    args[narg] = NULL;

    size_t arg_size = (narg+1)*sizeof(char*);
    // align the stackptr to 8 byte aligned
    stackptr -= arg_size;
    stackptr -= stackptr % 8;

    copyout(args, stackptr, arg_size);
	md_usermode(nargs, stackptr, stackptr, entrypoint); 
	panic("md_usermode returned\n");
	return EINVAL;
	
}
