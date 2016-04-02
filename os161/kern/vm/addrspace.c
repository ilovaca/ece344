#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <addrspace.h>
#include <vm.h>
#include <bitmap.h>
#include <machine/tlb.h>
#include <elf.h>

/*
 * Note! If OPT_DUMBVM is set, as is the case until you start the VM
 * assignment, this file is not compiled or linked or in any way
 * used. The cheesy hack versions in dumbvm.c are used instead.
 */

extern size_t num_frames;
extern frame* coremap;
extern struct bitmap* swapfile_map;

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
	as->as_regions = array_create();
	if (as->as_regions == NULL) {
		return NULL;
	}
	// we'll have to wait until the user bss segment is
	// defined before we know the start of heap
	as->heap_start = 0;
	as->heap_end = 0;
	// initiailize first level page table
	int i = 0;
	for (; i < FIRST_LEVEL_PT_SIZE; i++){
		as->as_master_pagetable[i] = NULL;
	}

	return as;
}

/*
	Two things to copy:
		1. all pages present in physical memory
		2. all pages in swap file
	Note: do deep copy
*/
int
as_copy(struct addrspace *old, struct addrspace **ret)
{
	int spl = splhigh();
	struct addrspace *newas;

	newas = as_create();
	if (newas == NULL) {
		return ENOMEM;
	}
	/******************** copy internal fields ***************/
	// first all regions
	unsigned int i;
	for (i = 0; i < array_getnum(old->as_regions); i++) {
		struct as_region* temp = kmalloc(sizeof(struct as_region));
		*temp = *((struct as_region*)array_getguy(old->as_regions, i));
		array_add(newas->as_regions, temp);
	}

	newas->heap_start = old->heap_start;
	newas->heap_end = old->heap_end;
	newas->temp_text_permis = old->temp_text_permis;
	newas->temp_bss_permis = old->temp_bss_permis;

	// then both the first and second page table
	for (i = 0; i < FIRST_LEVEL_PT_SIZE; i++) {
		if(old->as_master_pagetable[i] != NULL) {
			newas->as_master_pagetable[i] = (struct as_pagetable*)kmalloc(sizeof(struct as_pagetable));
			// what the fuck am i doing?
			// the right thing to do here is to go through all PTEs of the old addrspace, if there's a 
			// valid PTE, meaning there's a page, be it PRESENT or SWAPPED, belonging to this addrspace.
			struct as_pagetable *src_pt = old->as_master_pagetable[i];
			struct as_pagetable *dest_pt = newas->as_master_pagetable[i];
			unsigned int j = 0;
			for (; j < SECOND_LEVEL_PT_SIZE; j++) {
					dest_pt->PTE[j] = 0;

				if(src_pt->PTE[j] & PTE_PRESENT) {
					// this source page is PRESENT in memory, we just allocate a page for 
					// the destination addrspace and copy src->dest and do update PTE
					paddr_t src_paddr = (src_pt->PTE[j] & PAGE_FRAME);
					vaddr_t dest_vaddr = (i << 22) + (j << 12);
					// allocate a page for the destination addrspace, while making sure
					// that both the source and destination page are in memory
					paddr_t dest_paddr = alloc_page_userspace_with_avoidance(dest_vaddr, src_paddr);
					// sanity check
					// do the copy :)
					memmove((void *) PADDR_TO_KVADDR(dest_paddr),
					(const void*)PADDR_TO_KVADDR(src_paddr), PAGE_SIZE) ;
					// update the PTE of the destination pagetable
					// dest_pt->PTE[j] &= CLEAR_PAGE_FRAME;
					dest_pt->PTE[j] |= dest_paddr;
					dest_pt->PTE[j] &= PTE_PRESENT;

				} else if (src_pt->PTE[j] & PTE_SWAPPED){
					// this source page is SWAPPED, we load it back to mem :)
					vaddr_t src_vaddr = (i << 22) + (j << 12);
					vaddr_t dest_vaddr = src_vaddr;
					paddr_t src_paddr = fetch_page(old, src_vaddr);
					// now allocate a user page, but becareful not to swap out the 
					// source page we just brought in...
					paddr_t dest_paddr = alloc_page_userspace_with_avoidance(dest_vaddr, src_paddr);
					// do the copy
					memmove((void *) PADDR_TO_KVADDR(dest_paddr),
					(const void*)PADDR_TO_KVADDR(src_paddr), PAGE_SIZE) ;
					// update the PTE of the destination pagetable
					// dest_pt->PTE[j] &= CLEAR_PAGE_FRAME;
					dest_pt->PTE[j] |= dest_paddr;
					dest_pt->PTE[j] &= PTE_PRESENT;
				} else {
					// this source page is neither PRESENT nor SWAPPED, meaning this
					// page does not exist, we've got nothing to do in this case, nice :)
					dest_pt->PTE[j] = 0;
				}
			}
		} else {
			newas->as_master_pagetable[i] = NULL;
		} 
	}

	/********************* Copy pages :) ***********************/
	// go through all regions to copy user pages
/*	for (i = 0; i < array_getnum(old->as_regions); i++) {
		struct as_region* dest = array_getguy(newas->as_regions, i);
		struct as_region* src = array_getguy(old->as_regions, i);
		// copy each page in this region
		int j = 0;
		for (; j < src->npages; j++){
			vaddr_t vbase = dest->vbase + j * PAGE_SIZE;
			assert(vbase % PAGE_SIZE == 0);
			// make sure both the pages are in physical memory
			paddr_t src_addr = fetch_page(old, vbase);
			paddr_t dest_addr = fetch_page(newas, vbase);
			fetch_two_pages(old, vbase, newas, vbase);
			assert(src_addr != dest_addr);
			memmove((void *) PADDR_TO_KVADDR(dest_addr),
					(const void*)PADDR_TO_KVADDR(src_addr),
					PAGE_SIZE) ;
		}
	}
	
	// now stack pages
	vaddr_t stackbase = USERSTACK - MAX_STACK_PAGES * PAGE_SIZE;
	assert(stackbase % PAGE_SIZE == 0);
	for (i = 0; i < MAX_STACK_PAGES; i++) {
		vaddr_t vbase = stackbase + i * PAGE_SIZE;
		paddr_t src_addr = fetch_page(old, vbase);
		paddr_t dest_addr = fetch_page(newas, vbase);
		memmove((void *) PADDR_TO_KVADDR(dest_addr),
				(const void*)PADDR_TO_KVADDR(src_addr),
				PAGE_SIZE);
	}
	memmove((void *) stackbase, (const void *) stackbase, MAX_STACK_PAGES);
	// now the heap
	size_t heap_size = ROUNDUP((newas->heap_end - newas->heap_start), PAGE_SIZE);
	assert(heap_size % PAGE_SIZE == 0);
	for(i = 0; i < heap_size / PAGE_SIZE; i++) {
		vaddr_t vbase = old->heap_start + i * PAGE_SIZE;
		paddr_t src_addr = fetch_page(old, vbase);
		paddr_t dest_addr = fetch_page(newas, vbase);
		memmove((void *) PADDR_TO_KVADDR(dest_addr),
				(const void*)PADDR_TO_KVADDR(src_addr),
				PAGE_SIZE) ;
	}
 	*/
	*ret = newas;
	splx(spl);
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
		if(coremap[i].state != FREE && coremap[i].addrspace == as){
			coremap[i].addrspace = NULL;
			coremap[i].mapped_vaddr = 0;
			coremap[i].state = FREE;
			coremap[i].num_pages_allocated = 0;
		}
	}
	// free all pages in the swap file
	for(; i < array_getnum(as->as_regions); i++) {
		struct as_region* cur = (struct as_region*)array_getguy(as->as_regions, i);
		assert(cur->vbase % PAGE_SIZE == 0);
		// destroy all pages belonging to this region
		int j = 0;
		for (; j < cur->npages; j++) {
			vaddr_t page = cur->vbase + j * PAGE_SIZE;
			assert((page & PAGE_FRAME) == page);
			u_int32_t *pte = get_PTE_from_addrspace(as, page);
			if (((*pte & PTE_PRESENT) == 0) && ((*pte | PTE_SWAPPED) != 0)) {
				// if this page is in swap file...
				off_t file_slot = (*pte & SWAPFILE_OFFSET) >> 12;
				// the occupied bit must be set
				assert(bitmap_isset(swapfile_map, file_slot) != 0);
				bitmap_unmark(swapfile_map, file_slot);
			}
		}
	}	
	// free regions
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
	sz += vaddr & ~(vaddr_t)PAGE_FRAME;	 
	vaddr &= PAGE_FRAME;
	assert((vaddr & PAGE_FRAME) == vaddr);

	/* ...and now the length. */
	sz = (sz + PAGE_SIZE - 1) & PAGE_FRAME;
	// sz must be page aligned
	assert(sz % PAGE_SIZE == 0);
	npages = sz / PAGE_SIZE;

	// create and insert the region 
	struct as_region *new_region = kmalloc(sizeof(struct as_region));
	new_region->vbase = vaddr;
	new_region->npages = npages;
	// the region permission is the lower 3 bits R|W|X
	new_region->region_permis = 0;
	new_region->region_permis = (readable | writeable | executable);
	array_add(as->as_regions, new_region);

	// High calibre style alert! after the user bss segment is defined,
	// we know the start(vbase) of the user heap
	if(array_getnum(as->as_regions) == 2){
		// fuck... this is horribly inelegant, gotta find a better way
		// to do this
		as->heap_start = vaddr + npages * PAGE_SIZE;
		as->heap_end = as->heap_start; // heap is empty at start, to be increased 
									   // by the sbrk()
	}
	return 0;
}

int
as_prepare_load(struct addrspace *as)
{

	struct as_region* text = (struct as_region*)array_getguy(as->as_regions, 0); 
	struct as_region* bss = (struct as_region*)array_getguy(as->as_regions, 1); 
	// save the original permission
	as->temp_text_permis = text->region_permis;
	as->temp_bss_permis = bss->region_permis;
	//change each region's permission to READ/WRITE
	text->region_permis |= (PF_R | PF_W);
	bss->region_permis |= (PF_R | PF_W);
	return 0;
}

int
as_complete_load(struct addrspace *as)
{
	// restore the original permission
	struct as_region* text = (struct as_region*)array_getguy(as->as_regions, 0); 
	struct as_region* bss = (struct as_region*)array_getguy(as->as_regions, 1); 
	// save the original permission
	text->region_permis = as->temp_text_permis;
	bss->region_permis = as->temp_bss_permis;
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

