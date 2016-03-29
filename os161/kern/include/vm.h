#ifndef _VM_H_
#define _VM_H_

#include <machine/vm.h>

/*
 * VM system-related definitions.
 *
 * You'll probably want to add stuff here.
 */


/* Fault-type arguments to vm_fault() */
#define VM_FAULT_READ        0    /* A read was attempted */
#define VM_FAULT_WRITE       1    /* A write was attempted */
#define VM_FAULT_READONLY    2    /* A write to a readonly page was attempted*/


/* Initialization function */
void vm_bootstrap(void);

/* Fault handling function called by trap code */
int vm_fault(int faulttype, vaddr_t faultaddress);

/* Allocate/free kernel heap pages (called by kmalloc/kfree) */
vaddr_t alloc_kpages(int npages);
void free_kpages(vaddr_t addr);

/************************************************************************************/
#define FIRST_LEVEL_PN 0xffc00000 /* mask to get the 1st-level pagetable index from vaddr (first 10 bits) */
#define SEC_LEVEL_PN 0x003fc000	/* mask to get the 2nd-level pagetable index from vaddr (mid 10 bits) */
#define PAGE_FRAME 0xfffff000	/* mask to get the page number from vaddr (first 20 bits) */
#define PHY_PAGENUM 0xfffff000  /* Redundancy here :) */
#define INVALIDATE_PTE 0xfffff3ff  /* invalidate PTE by setting PRESENT and SWAPPED bits to zero */
#define CLEAR_PAGE_FRAME 0x00000fff


u_int32_t* get_PTE(struct thread* addrspace_owner, vaddr_t va);

u_int32_t* get_PTE_from_addrspace (struct addrspace* as, vaddr_t va);

int evict_or_swap();

void swap_out(int frame_id, off_t pos);

void load_page(struct thread* owner_thread, vaddr_t vaddr, int frame_id);
#endif /* _VM_H_ */
