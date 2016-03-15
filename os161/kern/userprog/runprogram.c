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
/*
int 
runprogram_exev(char *progname, char* argv[], int nargs)
{
	struct vnode *v;
	vaddr_t entrypoint, stackptr, temp;
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
		 thread_exit destroys curthread->t_vmspace 
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
	// int counter = 0;
	// while (1) {
	// 	if (result) {
	// 		result += result;
	// 	} 
	// 	counter ++;
	// 	if (counter == 100) {
	// 		break;
	// 	}
	// }

	if(nargs > 1) {
	// Copy arguments to user stack 


	int i, str_len, size;
	for(i = nargs - 1; i >= 0; i++){
	    str_len = 1 + strlen(argv[i]); 		

		stackptr -= str_len;
		
		result = copyoutstr(argv[i], (userptr_t)stackptr, str_len, &str_len); //copy the content from argv[i] to stackptr.
		if(result){
			panic("failed copycoutstr");
		}
		argv[i] = (char*)stackptr; 
    }


    result = 0;
    while (1) {
		if (result) {
			result += result;
		} 
		counter ++;
		if (counter == 100) {
			break;
		}
	}

	 // kprintf("1. the stackptr is at %x",stackptr);


    // after copying the strings we adjust the stack pointer
    
    int arg_bytes = (nargs + 1) * sizeof(char*); 

    argv[nargs] = NULL;

    stackptr -= stackptr % 8;
    
    stackptr -= arg_bytes;

	// stackptr -= ((stackptr - arg_bytes)%8);

     kprintf("2. the stackptr is at %x",stackptr);


   // stackptr -= (stackptr- arg_bytes)%8;

    #define DIVROUNDUP(a,b) (((a)+(b)-1)/(b))
	#define ROUNDUP(a,b)    (DIVROUNDUP(a,b)*b)


    stackptr = ROUNDUP(stackptr,8);
  
    size_t diff = temp - stackptr;
  
    if(diff < arg_size){
  	  stackptr -= ROUNDUP(arg_size - diff,8);
    }
    temp = stackptr+arg_size;
    


    

    kprintf("3. the stackptr is at %x",stackptr);

    temp = stackptr;
    for(i = 0; i < nargs; i++){
    	// temp -= sizeof(char*);
      	result = copyout(&argv[i],(userptr_t)temp, sizeof(char*));
      	if(result){
      		panic("copyout failed");
      	}
      	temp += sizeof(char*);
      	kprintf("the temp is at %x",temp);
    }
  	

	md_usermode(nargs //argc,  stackptr//userspace addr of argv,
		    stackptr, entrypoint); // go to user mode after loading an executable.
	panic("md_usermode returned\n");
	return EINVAL;
	}
	else {
		
	md_usermode(0 ,  NULL,
		    stackptr, entrypoint); // go to user mode after loading an executable.
	panic("md_usermode returned\n");
	return EINVAL;		
	}
}

*/


int 
runprogram_exev(char *progname, char* args[], int nargs)
{
	struct vnode *v;
	int narg = nargs;
vaddr_t entrypoint, stackptr,tmpptr;	int result;
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
	int j;
   for(j = 0; j < narg; ++j){
	//	kprintf("args-input: %s\n", args[i]);
	    size_t len = 1 + strlen(args[j]);
		size_t ssize = len*sizeof(char);
		stackptr-=ssize;
		  //  kprintf("ssize: %d\nstackptr:%x\n", ssize,stackptr);

		result = copyoutstr(args[j],(userptr_t)stackptr,ssize,&ssize); 
		if(result){return result;}
		args[j] = (char*)stackptr;
		//kprintf("args-address:%x\n", (unsigned int)args[i]);
    }
    args[narg] = NULL;

    tmpptr = stackptr;
    size_t arg_size = (narg+1)*sizeof(char*);
    stackptr-=arg_size;
   // kprintf("stackptr:%x\n", stackptr);
    stackptr = ROUNDUP(stackptr,8);
   // kprintf("afterround-stackptr:%x\n", stackptr);
    size_t diff = tmpptr-stackptr;
   // kprintf("diff: %d\n arg_size:%d\n", diff,arg_size);
    if(diff < arg_size){
  	  stackptr-=ROUNDUP(arg_size-diff,8);
    }
    tmpptr = stackptr+arg_size;
    int i;
    for( i = narg;i >= 0;i--){
      tmpptr-=sizeof(char*);

      result = copyout(&args[i],(userptr_t)tmpptr,sizeof(char*));
      if(result){return result;}
    }

	md_usermode(nargs ,  stackptr, stackptr, entrypoint); 
	panic("md_usermode returned\n");
	return EINVAL;
	
}