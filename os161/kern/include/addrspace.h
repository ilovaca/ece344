#ifndef _ADDRSPACE_H_
#define _ADDRSPACE_H_

#include <vm.h>
#include "opt-dumbvm.h"

#define FIRST_LEVEL_PT_SIZE 1024
#define SECOND_LEVEL_PT_SIZE 1024
struct vnode;

/*
	PTE format:
	|<----- 20 ----->|<--  1  -->|<---- 11 ---->|
		frame #        present?     permission bits
	  or disk addr
*/
#define PTE_PRESENT 0x00000800
#define PTE_SWAPPED 0xfffff7ff

/*************************************** 2nd level pagetable ************************************/
struct as_pagetable{
	u_int32_t PTE [SECOND_LEVEL_PT_SIZE];
};

/*************************************** User address space *************************************/
#define MAX_STACK_PAGES 24
/* 
 * Address space - data structure associated with the virtual memory
 * space of a process.
 */
 struct as_region{
 	vaddr_t vbase;
 	size_t npages;
 	unsigned int region_permis;
 	// struct as_region *as_next_section;
 };

struct addrspace {
#if OPT_DUMBVM
	vaddr_t as_vbase1;
	paddr_t as_pbase1;
	size_t as_npages1;
	vaddr_t as_vbase2;
	paddr_t as_pbase2;
	size_t as_npages2;
	paddr_t as_stackpbase;
#else
	/* Put stuff here for your VM system */
	struct array* as_regions;
	struct as_region heap; //TODO
    struct as_pagetable *as_master_pagetable[FIRST_LEVEL_PT_SIZE]; // first level page table
#endif
};

/*
 * Functions in addrspace.c:
 *
 *    as_create - create a new empty address space. You need to make 
 *                sure this gets called in all the right places. You
 *                may find you want to change the argument list. May
 *                return NULL on out-of-memory error.
 *
 *    as_copy   - create a new address space that is an exact copy of
 *                an old one. Probably calls as_create to get a new
 *                empty address space and fill it in, but that's up to
 *                you.
 *
 *    as_activate - make the specified address space the one currently
 *                "seen" by the processor. Argument might be NULL, 
 *		  meaning "no particular address space".
 *
 *    as_destroy - dispose of an address space. You may need to change
 *                the way this works if implementing user-level threads.
 *
 *    as_define_region - set up a region of memory within the address
 *                space.
 *
 *    as_prepare_load - this is called before actually loading from an
 *                executable into the address space.
 *
 *    as_complete_load - this is called when loading from an executable
 *                is complete.
 *
 *    as_define_stack - set up the stack region in the address space.
 *                (Normally called *after* as_complete_load().) Hands
 *                back the initial stack pointer for the new process.
 */

struct addrspace *as_create(void);
int               as_copy(struct addrspace *src, struct addrspace **ret);
void              as_activate(struct addrspace *);
void              as_destroy(struct addrspace *);

int               	as_define_region(struct addrspace *as, 
				   vaddr_t vaddr, size_t sz,
				   int readable, 
				   int writeable,
				   int executable);
int		  			as_prepare_load(struct addrspace *as);
int		  			as_complete_load(struct addrspace *as);
int              	as_define_stack(struct addrspace *as, vaddr_t *initstackptr);

/*
 * Functions in loadelf.c
 *    load_elf - load an ELF user program executable into the current
 *               address space. Returns the entry point (initial PC)
 *               in the space pointed to by ENTRYPOINT.
 */

int load_elf(struct vnode *v, vaddr_t *entrypoint);


#endif /* _ADDRSPACE_H_ */
