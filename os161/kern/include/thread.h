#ifndef _THREAD_H_
#define _THREAD_H_

/*
 * Definition of a thread.
 */

/* Get machine-dependent stuff */
#include <machine/pcb.h>
#include <synch.h>
#define MAX_PID 512
#define MIN_PID 1 //the following macros will be used in syscall.c and runprogram.c
#define MAX_ARG_LEN 256
#define MAX_ARGC 16

struct addrspace;
// #define CV_IMPL 1

typedef struct PCB_T {
	int exited;
	int exit_code;
	struct thread* this_thread;
	int parent; //parent process's pID.
	//#ifndef CV_IMPL
	struct semaphore * mutex;
	//#else
	//struct cv* cv;
	//#endif
} pcb_t;


/******************** Global Process Table Lock ********************/ 
struct lock * lock;

/******************** Kernel Process Table ********************/ 
pcb_t* PCBs [MAX_PID];

struct thread {
	/**********************************************************/
	/* Private thread members - internal to the thread system */
	/**********************************************************/
	
	struct pcb t_pcb;
	char *t_name;
	const void *t_sleepaddr;
	char *t_stack;
	u_int32_t pID;
	/**********************************************************/
	/* Public thread members - can be used by other code      */
	/**********************************************************/
	
	/*
	 * This is public because it isn't part of the thread system,
	 * and will need to be manipulated by the userprog and/or vm
	 * code.
	 */
	struct addrspace *t_vmspace;
	/*
	 * This is public because it isn't part of the thread system,
	 * and is manipulated by the virtual filesystem (VFS) code.
	 */
	struct vnode *t_cwd;
};

/* Call once during STARTUP to allocate data structures. */
struct thread *thread_bootstrap(void);

/* Call during panic to stop other threads in their tracks */
void thread_panic(void);

/* Call during SHUTDOWN to clean up (must be called by initial thread) */
void thread_shutdown(void);

/*
 * Make a new thread, which will start executing at "func".  The
 * "data" arguments (one pointer, one integer) are passed to the
 * function.  The current thread is used as a prototype for creating
 * the new one. If "ret" is non-null, the thread structure for the new
 * thread is handed back. (Note that using said thread structure from
 * the parent thread should be done only with caution, because in
 * general the child thread might exit at any time.) Returns an error
 * code.
 */
int thread_fork(const char *name, 
		void *data1, unsigned long data2, 
		void (*func)(void *, unsigned long),
		struct thread **ret);

/*
 * Cause the current thread to exit.
 * Interrupts need not be disabled.
 */
void thread_exit(void);

/*
 * Cause the current thread to yield to the next runnable thread, but
 * itself stay runnable.
 * Interrupts need not be disabled.
 */
void thread_yield(void);

/*
 * Cause the current thread to yield to the next runnable thread, and
 * go to sleep until wakeup() is called on the same address. The
 * address is treated as a key and is not interpreted or dereferenced.
 * Interrupts must be disabled.
 */
void thread_sleep(const void *addr);

void thread_join(struct thread *);

void thread_detach(struct thread *);

/*
 * Cause all threads sleeping on the specified address to wake up.
 * Interrupts must be disabled.
 */
void thread_wakeup(const void *addr);

void thread_wakeup_single (const void * addr);


/*
 * Return nonzero if there are any threads sleeping on the specified
 * address. Meant only for diagnostic purposes.
 */
int thread_hassleepers(const void *addr);


/*
 * Private thread functions.
 */

/* Machine independent entry point for new threads. */
void mi_threadstart(void *data1, unsigned long data2, 
		    void (*func)(void *, unsigned long));

/* Machine dependent context switch. */
void md_switch(struct pcb *old, struct pcb *nu);


int allocate_PID(unsigned int * to_pid);


#endif /* _THREAD_H_ */
