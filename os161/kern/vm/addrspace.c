#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <addrspace.h>
#include <vm.h>

/*
 * Note! If OPT_DUMBVM is set, as is the case until you start the VM
 * assignment, this file is not compiled or linked or in any way
 * used. The cheesy hack versions in dumbvm.c are used instead.
 */

extern size_t num_frames;
extern frame* coremap;

/*
	in as_create, we just allocate a addrspace structure using kmalloc, and allocate a physical 
	page (using page_alloc) as page directory and store it's address (either KVADDR or PADDR is OK, 
	but you can just choose one).
*/
struct addrspace *
as_create(void)
{
	struct addrspace *as = kmalloc(sizeof(struct addrspace));
	if (as == NULL) {
		return NULL;
	}
	
	// allocate the array of regions
	as->regions = array_create();
	if (as->as_regions == NULL) {
		return NULL;
	}
	// initiailize first level page table
	int i = 0;
	for (; i < FIRST_LEVEL_PT_SIZE; i++){
		as->as_master_pagetable[i] = NULL;
	}

	return as;
}

int
as_copy(struct addrspace *old, struct addrspace **ret)
{
	struct addrspace *newas;

	newas = as_create();
	if (newas == NULL) {
		return ENOMEM;
	}

	(void)old;
	
	*ret = newas;
	return 0;
}


/*
	destroy all pages in physical memory and all swapped pages
*/
void
as_destroy(struct addrspace *as)
{
	int spl = splhigh();
	int i = 0;
	// free all coremap entries
	for (; i < num_frames; i++) {
		if(coremap[i].owner_thread->t_vmspace == as){
			coremap[i].owner_thread = NULL;
			coremap[i].mapped_vaddr = 0xdeadbeef;
			coremap[i].state = FREE;
			coremap[i].num_pages_allocated = 0;
		}
	}
	// free all pages in swap file
	for(; i < array_getnum(as->as_regions); i++) {
		struct as_region* cur = (struct as_region*)array_getguy(as->as_regions);
		assert(cur->vbase % PAGE_SIZE == 0);
		// destroy all pages within this region
		int j = cur->npages;
		for (; j < npages; j++) {

		}
	}	

	array_destroy(as->as_regions);
	// free 2nd level page tables
	for(i = 0; i < SECOND_LEVEL_PT_SIZE; i++) {
		if(as->as_master_pagetable[i] != NULL)
			kfree(as->as_master_pagetable[i]);
	}
	kfree(as);
	splx(spl);
	return;
}

void
as_activate(struct addrspace *as)
{
	int i, spl;

	(void)as;

	spl = splhigh();

	for (i=0; i<NUM_TLB; i++) {
		TLB_Write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}

	splx(spl);
}

/*
 * Set up a *segment* at virtual address VADDR of size MEMSIZE. The
 * segment in memory extends from VADDR up to (but not including)
 * VADDR+MEMSIZE.
 *
 * The READABLE, WRITEABLE, and EXECUTABLE flags are set if read,
 * write, or execute permission should be set on the segment. At the
 * moment, these are ignored. When you write the VM system, you may
 * want to implement them.
 */
int
as_define_region(struct addrspace *as, vaddr_t vaddr, size_t sz,
		 int readable, int writeable, int executable)
{
	size_t npages; 

	/* Align the region. First, the base... */
	assert((vaddr & PAGE_FRAME) == vaddr);
	sz += vaddr & ~(vaddr_t)PAGE_FRAME;	 
	vaddr &= PAGE_FRAME;

	/* ...and now the length. */
	sz = (sz + PAGE_SIZE - 1) & PAGE_FRAME;
	// sz must be page aligned
	assert(sz % PAGE_SIZE == 0);
	npages = sz / PAGE_SIZE;

	// create and insert the region 
	struct as_region *new_region = kmalloc(sizeof(struct as_region));
	new_region->vbase = vaddr;
	new_region->npages = npages;
	new_region->region_permis = (readable | writeable | executable);
	array_add(as->as_regions, new_region);

	return EUNIMP;
}

int
as_prepare_load(struct addrspace *as)
{
	/*
	 * Write this.
	 */

	(void)as;
	return 0;
}

int
as_complete_load(struct addrspace *as)
{
	/*
	 * Write this.
	 */

	(void)as;
	return 0;
}

int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
	/*
	 * Write this.
	 */

	(void)as;

	/* Initial user-level stack pointer */
	*stackptr = USERSTACK;

	return 0;
}

