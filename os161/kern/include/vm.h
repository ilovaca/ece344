#ifndef _VM_H_
#define _VM_H_

#include <machine/vm.h>
#include <thread.h>
/*
 * VM system-related definitions.
 *
 * You'll probably want to add stuff here.
 */
/*************** Structure representing the physical pages/frames in memory ***************/

typedef enum FRAME_STATE {
	FREE,  // 
	FIXED, // kernel pages shall remain in physical memory, so does coremap itself
	DIRTY, // newly allocated user pages shall be dirty
	CLEAN, // never modified since swapped in
} frame_state;

typedef struct Frame {
	struct thread* owner_thread; // could be the addrspace object as well
	int frame_id;	// position in coremap
	paddr_t frame_start; // the starting physical address of this page, needed for address translation
	vaddr_t mapped_vaddr; // the virtual address this frame is mapped to
	frame_state state; // see below 
	int num_pages_allocated; // number of contiguous pages in a single allocation (e.g., large kmalloc)
} frame;


/**************************** Fault-type arguments to vm_fault() ********************************/
#define VM_FAULT_READ        0    /* A read was attempted */
#define VM_FAULT_WRITE       1    /* A write was attempted */
#define VM_FAULT_READONLY    2    /* A write to a readonly page was attempted*/

/*********************************** Swap file Related *******************************************/

#define MAX_SWAPFILE_SLOTS 65536 // TODO: we support up to 65536 pages on disk
#define SWAPFILE_OFFSET 0xfffff000 /* When a page is swapped out, we put the disk slot # 
							in the first 20 bits (replacing the physical page numebr)*/


/* Initialization function */
void vm_bootstrap(void);

/* Fault handling function called by trap code */
int vm_fault(int faulttype, vaddr_t faultaddress);

/* Allocate/free kernel heap pages (called by kmalloc/kfree) */
vaddr_t alloc_kpages(int npages);
void free_kpages(vaddr_t addr);

/******************************* Paging Related Macros   ******************************************/
#define FIRST_LEVEL_PN 0xffc00000 /* mask to get the 1st-level pagetable index from vaddr (first 10 bits) */
#define SEC_LEVEL_PN 0x003ff000	/* mask to get the 2nd-level pagetable index from vaddr (mid 10 bits) */
#define PAGE_FRAME 0xfffff000	/* mask to get the page number from vaddr (first 20 bits) */
#define PHY_PAGENUM 0xfffff000  /* Redundancy here :) */
#define INVALIDATE_PTE 0xfffff3ff  /* invalidate PTE by setting PRESENT and SWAPPED bits to zero */
#define CLEAR_PAGE_FRAME 0x00000fff /* Clear the first 20 bits */

/******************************** Misc Functions **************************************************/
u_int32_t* get_PTE(struct thread* addrspace_owner, vaddr_t va);

u_int32_t* get_PTE_from_addrspace (struct addrspace* as, vaddr_t va);

int evict_or_swap();

void swap_out(int frame_id, off_t pos);

void load_page(struct thread* owner_thread, vaddr_t vaddr, int frame_id);

void swapping_init();

void as_zero_page(paddr_t paddr, size_t num_pages);

int handle_vaddr_fault (vaddr_t faultaddress, unsigned int permissions);

#endif /* _VM_H_ */
