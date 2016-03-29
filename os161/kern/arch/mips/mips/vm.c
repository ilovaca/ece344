#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <thread.h>
#include <curthread.h>
#include <addrspace.h>
#include <vm.h>
#include <machine/spl.h>
#include <machine/tlb.h>
/******************************************************************************************/
#define PTE_PRESENT 0x00000800
#define PTE_SWAPPED 0x00000400
#define PTE_UNSET_PRESENT 0xfffff7ff
/********************************** Extern Declarations************************************/
extern struct thread* curthread;
/*************** Structure representing the physical pages/frames in memory ***************/
typedef struct frame {
	struct thread* owner_thread; // could be the addrspace object as well
	int frame_id;	// position in coremap
	paddr_t frame_start; // the starting physical address of this page, needed for address translation
	vaddr_t mapped_vaddr; // the virtual address this frame is mapped to
	enum frame_state state; // see below 
	int num_pages_allocated; // number of contiguous pages in a single allocation (e.g., large kmalloc)
} frame;

enum frame_state {
	FREE,  // 
	FIXED, // kernel pages shall remain in physical memory, so does coremap itself
	DIRTY, // newly allocated user pages shall be dirty
	ClEAN, // never modified since swapped in
};

/*********************************** The Coremap *****************************************/
frame* coremap;
size_t num_frames;
size_t num_fixed_page;
/*********************************** Swap file *******************************************/
struct vnode * swap_file;
off_t cur_pos;
struct bitmap* swapfile_map;
#define MAX_SWAPFILE_SLOTS 65536 // TODO: we support up to 65536 pages on disk
#define SWAPFILE_OFFSET 0xfffff000 /* When a page is swapped out, we put the disk slot # 
							in the first 20 bits (replacing the physical page numebr)*/

 /**************************** Convenience Function **************************************/
int swap_out(int frame_id, off_t pos);

int load_page(int pid, vaddr_t vaddr);

u_int32_t* get_PTE (struct thread*, vaddr_t va);
u_int32_t* get_PTE_from_addrspace (struct addrspace*, vaddr_t va);

/*****************************************************************************************/
int vm_bootstraped = 0;
/*****************************************************************************************/

void swapping_init(){
	// for swapping subsystem
	int err = vfs_open("swap_file", O_RDWR|O_CREAT, &swap_file);
	if (err) {
		panic("vfs_open on swap_file failed");
	}
	cur_pos = 0;
	swapfile_map = bitmap_create(MAX_SWAPFILE_SLOTS);
}

paddr_t
getppages(unsigned long npages)
{
	int spl;
	paddr_t addr;

	spl = splhigh();

	addr = ram_stealmem(npages);
	
	splx(spl);
	return addr;
}


void
vm_bootstrap(void)
{
	u_int32_t start, end, start_adjusted;

	size_t num_pages = 0;
	// get the amount of physical memory
	ram_getsize(&start, &end);
	// align start
	// start += start - start % PAGE_SIZE;
	start = ROUNDUP(start, PAGE_SIZE);
	// TODO: THIS ALIGNMENT IS PROBABLY NOT RIGHT
	num_pages = ((end - start)/ PAGE_SIZE);
	kprintf("start: %x, end: %x\n", start, end);
	// kernel always looks at the virtual memory only
	coremap = (frame *)PADDR_TO_KVADDR(start);
	/************************************** ALLOCATE SPACE *****************************************/
	// first the coremap
	start_adjusted = start + num_pages * sizeof(struct frame); 
	/************************************** initialize coremap *************************************/
	int i = 0, fixed_pages, free_pages;
	fixed_pages = (start_adjusted - start) / PAGE_SIZE;
	// mark the pages in [start, start_adjusted] as fixed
	for (; i < fixed_pages; i++) { 
		coremap[i].owner_thread = curthread;
		coremap[i].frame_id = i;
		coremap[i].frame_start = start + PAGE_SIZE * i;
		coremap[i].mapped_vaddr = PADDR_TO_KVADDR(coremap[i].frame_start);
		// the fixed pages are mapped to kernel addr space
		coremap[i].frame_state = FIXED;
		coremap[i].num_pages_allocated = 0;
	}
	// mark the pages in [start_adjusted, end] as free 
	free_pages = num_pages - fixed_pages;
	assert(num_pages == (fixed_pages + free_pages));

	for (; i < free_pages; i++) {
		coremap[i].owner_thread = NULL;
		coremap[i].frame_id = i;
		coremap[i].frame_start = start_adjusted + PAGE_SIZE * i;
		coremap[i].mapped_vaddr = 0xDEADBEEF; // LOL
		coremap[i].frame_state = FREE;
		coremap[i].num_pages_allocated = 0;
	}

	// sanity check
	for (i = 0; i < num_frames; i++) {
		if (coremap[i].state != FREE && coremap[i].state != FIXED) panic("error initializing the coremap"); 
	}
	// globals
	num_frames = num_pages;
	num_fixed_page = fixed_pages;
	// swapping subsystem init
	swapping_init();
	/**************************************** END of init ******************************************/
	// TODO: we may want to set some flags to indicate that vm has already bootstrapped, 
	vm_bootstraped = 1;
	// TODO: start the paging thread below
}


/*
	Function to get a pointer to the PTE corresponding to the virtual address va.
	Note, a PTE is uniquely identified among all processes by a tuple: <addrspace, vaddress>
*/
u_int32_t* get_PTE(struct thread* addrspace_owner, vaddr_t va) {
	return get_PTE_from_addrspace(addrspace_owner->t_vmspace, va);
}
u_int32_t* get_PTE_from_addrspace (struct addrspace* as, vaddr_t va){
	int level1_index = (va & FIRST_LEVEL_PN) >> 22; 
	int level2_index = (va & SEC_LEVEL_PN) >> 12;
	struct as_pagetable* level2_pagetable = as->as_master_pagetable[level1_index];
	// level2 pagetable shall not be null? TODO
	// assert(level2_pagetable != NULL);
	if (level2_pagetable == NULL) 
		return NULL;
	else 
		return &(level2_pagetable->PTE[level2_index]);
}


/*
	Function that makes room for a single page
	Page replacement policy (experimental) -> randomly evict a page
	Depending on the state of the page, we either
		** evict the page, should the page be clean,
		or
	 	** swap to disk, should the page be dirty
	@return the id of the evict/swapped frame
	TODO: an optimization: find clean pages if possible.
*/
int evict_or_swap(){
	assert(curspl > 0);
	int kicked_ass_page;
	do{
		kicked_ass_page = random() % num_frames;
	} while (coremap[kicked_ass_page].frame_state == FIXED 
				|| coremap[kicked_ass_page].frame_state == FREE);

	/* TODO need to swap the page to disk and update the PTE */
	// check the state of this page...
	if (coremap[kicked_ass_page].frame_state == DIRTY) {
		int disk_slot;
		bitmap_alloc(swapfile_map, &disk_slot);
		off_t disk_addr = disk_slot * PAGE_SIZE;
		swap_out(kicked_ass_page, disk_addr);
		return kicked_ass_page;
	} else {
		// page clean, no need for swap, just update the PTE and return
	}
	// update PTE of the *swapped* page and return frame id for loading/allocation
	u_int32_t *pte = get_PTE(coremap[kicked_ass_page].owner_thread,
							 coremap[kicked_ass_page].mapped_vaddr);
	// set the SWAPPED bit and unset the PRESENT bit
	*pte |= PTE_SWAPPED;
	*pte &= PTE_UNSET_PRESENT;
	return kicked_ass_page;
}


/*
	Same as alloc_one_page, but sets the coremap entries as user space
*/
paddr_t alloc_page_userspace(vaddr_t va) {
	assert(curspl > 0);
	// passed in virtual address shall be page-aligned
	assert((pa & PAGE_FRAME) == 0);
	int i = 0;
	int kicked_ass_page = -1;
	// go through the coremap check if there's a free page
	for (; i < num_frames; i++) {
		if (coremap[i].frame_state == FREE) {
			kicked_ass_page = i;
			break;
		}
	}
	if (kicked_ass_page == -1) {
		// no free page available right now --> we need to evict/swap a page
		// this makes room for the newly created page
		kicked_ass_page = evict_or_swap();
	}
	assert(kicked_ass_page >= 0);
	// now do the allocation
	coremap[kicked_ass_page].owner_thread = curthread;
	coremap[kicked_ass_page].frame_state = DIRTY; 
	coremap[kicked_ass_page].mapped_vaddr = va;
	coremap[kicked_ass_page].num_pages_allocated = 1;
	return coremap[kicked_ass_page].frame_start;
}

/*
	Allocates a single page
	@return starting address of the newly allocated page
*/
vaddr_t alloc_one_page() {
	assert(curspl > 0);
	int i = 0;
	// go through the coremap check if there's a free page
	int kicked_ass_page = -1;
	// go through the coremap check if there's a free page
	for (; i < num_frames; i++) {
		if (coremap[i].frame_state == FREE) {
			kicked_ass_page = i;
			break;
		}
	}
	if (kicked_ass_page == -1) {
		// no free page available right now --> we need to evict/swap a page
		// this makes room for the newly created page
		kicked_ass_page = evict_or_swap();
	}
	assert(kicked_ass_page >= 0);
	// now do the allocation
	coremap[kicked_ass_page].owner_thread = curthread;
	coremap[kicked_ass_page].frame_state = FIXED; // keep kernel pages in memory
	// if this is invoked by kmalloc, then the virtual address must be mapped by PADDR_TO_KVADDR
	coremap[kicked_ass_page].mapped_vaddr = PADDR_TO_KVADDR(coremap[kicked_ass_page].frame_start);
	coremap[kicked_ass_page].num_pages_allocated = 1;
	
	return (coremap[kicked_ass_page].mapped_vaddr);
}

/*
	Function to evict or swap multiple pages starting from starting_frame
*/
void evict_or_swap_multiple(int starting_frame, size_t npages){
	
	for (i = 0; i < npages; i++) {
		// come on, don't let me down...
		assert(coremap[starting_frame + i].frame_state != FREE);
		assert(coremap[starting_frame + i].frame_state != FIXED);

		if (coremap[starting_frame + i].frame_state == DIRTY) {
			int disk_slot;
			// find a slot on disk that is not used
			bitmap_alloc(swapfile_map, &disk_slot);
			off_t disk_addr = disk_slot * PAGE_SIZE;
			swap_out(starting_frame + i, disk_addr);
		} else {
		// page clean, no need for swap, just update the PTE and return
		}
		// update PTE of the *swapped* page and return frame id for loading/allocation
		u_int32_t *pte = get_PTE(coremap[starting_frame + i].owner_thread,
								 coremap[starting_frame + i].mapped_vaddr);
		// set the swapped bit (PRESENT BIT = 0)
		*pte = (*pte & PTE_SWAPPED);	 
	}
	return;
}

/*
	Allocate npages.
*/
vaddr_t alloc_npages(int npages) {
	assert(curspl > 0);
	int num_continous = 1;
	int i = 0; 
	// find if there're npages in succession
	for (; i < num_frames - 1; i++){
		// as long as we've got enough, we break:)
		if (num_continous >= npages) break;
		if (coremap[i].frame_state == FREE && coremap[i + 1].frame_state == FREE) {
			num_continous++;
		} else {
			num_continous = 1;
		}
	}
	if (num_continous >= npages) {
		// found n continous free pages
		int j = i;
		for (; j > i - npages; j--)
		{
			coremap[j].owner_thread = curthread;
			coremap[j].frame_state = FIXED;
			// redundancy not a problem ;)
			coremap[j].mapped_vaddr = PADDR_TO_KVADDR(coremap[j].frame_start);
			coremap[j].num_pages_allocated = npages; 
		}
		assert(coremap[j].mapped_vaddr == PADDR_TO_KVADDR(coremap[j].frame_start));
		return PADDR_TO_KVADDR(coremap[j].frame_start);

	} else {
		// not found, we evict n pages starting from a random page
		// make sure the n pages does not overflow the end of memory
		// 		--> search the first num_frames - npages, excluding the fixed pages
		int starting_frame;
		int search_range = num_frames - npages - num_fixed_page;
		do{
			starting_frame = (random() % search_range) + num_fixed_page;
		} while (coremap[starting_frame].frame_state != FIXED);

		// sanity check
		int i = 0;
		for (; i < npages; i++) {
			if (coremap[i + starting_frame].frame_state == FIXED) 
				panic("alloc_npages contains a fixed page"); 
		}
		// evict/swap all pages to disk
		evict_or_swap_multiple(starting_frame, npages);
		// allocation
		for (i = 0; i < npages; i++) {
			coremap[starting_frame + i].owner_thread = curthread;
			coremap[starting_frame + i].frame_state = FIXED;
			coremap[starting_frame + i].mapped_vaddr = PADDR_TO_KVADDR(coremap[starting_frame + i].frame_start);
			coremap[starting_frame + i].num_pages_allocated = npages; 
		}
		return PADDR_TO_KVADDR(coremap[starting_frame].frame_start);
	}
}


/* Allocate/free some kernel-space virtual pages */
vaddr_t 
alloc_kpages(int npages)
{	
	int spl = splhigh();
	if(vm_bootstraped == 0){
		addr = getppages(npages);
		splx(spl);
		return PADDR_TO_KVADDR(addr);
	} else {
		vaddr_t	ret = (npages == 1) ? alloc_one_page() : alloc_npages(npages);
		splx(spl);
		return ret; 
	}
}

/*
	Function to free a certain number of pages given *ONLY* the starting address of the page.
	*** The given address shall be page aligned ***
	To know the number of pages to free here, we need to store information in the page structure
	when we do the page allocation accordingly.
*/

void 
free_kpages(vaddr_t addr)
{	
	// the addr must be page aligned
	assert(addr % PAGE_SIZE == 0);

	int spl = splhigh();
	int i = 0;
	for (i = 0; i < num_frames; i++) {
		if (PADDR_TO_KVADDR(coremap[i].frame_start) == addr) {
			// found the starting page
			int numpage_to_free = coremap[i].num_pages_allocated;
			int j;
			for (j = 0; j < numpage_to_free; j++) {
				coremap[j + i].frame_state = FREE;
				coremap[j + i].num_pages_allocated = 0;
			}
			splx(spl);
			return;
		}
	}
	// not found 
	splx(spl);
	panic("invalid addr to free_kpages");
}

/*
 * When TLB miss happening, a page fault will be trigged.
 * The way to handle it is as follow:
 * 1. check what page fault it is, if it is READONLY fault, 
 *    then do nothing just pop up an exception and kill the process
 * 2. if it is a read fault or write fault
 *    1. first check whether this virtual address is within any of the regions
 *       or stack of the current addrspace. if it is not, pop up a exception and
 *       kill the process, if it is there, goes on. 
 *    2. then try to find the mapping in the page table, 
 *       if a page table entry exists for this virtual address insert it into TLB 
 *    3. if this virtual address is not mapped yet, mapping this address,
 *     update the pagetable, then insert it into TLB
 */

int
vm_fault(int faulttype, vaddr_t faultaddress)
{
	vaddr_t vbase1, vtop1, vbase2, vtop2, stackbase, stacktop;
	paddr_t paddr;
	int i;
	u_int32_t tlb_hi, tlb_low;
	struct addrspace *as;
	int spl;

	spl = splhigh();

	faultaddress &= PAGE_FRAME; 

	DEBUG(DB_VM, "vm: fault: 0x%x\n", faultaddress);

	switch (faulttype) {
	    case VM_FAULT_READONLY:
			return EFAULT;
	    case VM_FAULT_READ:
	    case VM_FAULT_WRITE:
		break;
	    default:
		splx(spl);
		return EINVAL;
	}

	as = curthread->t_vmspace;

	if (as == NULL) {
		/*
		 * No address space set up. This is probably a kernel
		 * fault early in boot. Return EFAULT so as to panic
		 * instead of getting into an infinite faulting loop.
		 */
		return EFAULT;
	}

	vaddr_t faulting_PN = 0;
	unsigned int permissions = 0;
	vaddr_t vbase, vtop;
	/*********************************** Check the validity of the faulting address ******************************/
	int i = 0;
	int found = 0;
	for (; i < array_getnum(as->as_regions); i++) {

		struct as_region * cur = array_getguy(as->as_regions, i);
		vbase = cur->vbase;
		vtop = vbase + cur->npages * PAGE_SIZE;
		// base addr should be page aligned
		assert((cur->vbase & PAGE_FRAME) == cur->as_vbase);  

		// find which region the faulting address btlb_lowngs to
		if(faultaddress >= vbase && faultaddress < vtop){
			found = 1;
			// get the permission of the region
			
			permissions |= (cur->region_permis);
			int err = handle_vaddr_fault(faultaddress, cur->region_permis); 
			splx(spl);
			return err;
		}
	}

	//if we didn't fall into any region, we check if it falls into the user stack.
	if(!found){
		vtop = USERSTACK;
		// hardcoded stack size
		vbase = vtop - MAX_STACK_PAGES * PAGE_SIZE; 
		if(faultaddress >= vbase && faultaddress < vtop){
			found = 1;
			permissions |= (PF_W | PF_R); 
			splx(spl);
			int err = handle_vaddr_fault(faultaddress, permissions);
			return err;
		}
	}
	// the last place we check is the heap
	if(!found){
		vbase = as->heap.vbase;
		vtop = vbase + as->heap.npages * PAGE_SIZE;
		if(faultaddress >= vbase && faultaddress < vtop){
			found = 1;
			// heap region is read/write of course
			permissions |= (PF_W | PF_R); 
			int err = handle_vaddr_fault(faultaddress, permissions);
			splx(spl);
			return err;
		}
	}
	// cannot find the faulting address, probably a segfault
	if(!found){
		splx(spl);
		return EFAULT;
	}
}

	#define FIRST_LEVEL_PN 0xffc00000 /* mask to get the 1st-level pagetable index from vaddr (first 10 bits) */
	#define SEC_LEVEL_PN 0x003fc000	/* mask to get the 2nd-level pagetable index from vaddr (mid 10 bits) */
	#define PAGE_FRAME 0xfffff000	/* mask to get the page number from vaddr (first 20 bits) */
	#define PHY_PAGENUM 0xfffff000  /* Redundancy here :) */
	#define INVALIDATE_PTE 0xfffff3ff  /* invalidate PTE by setting PRESENT and SWAPPED bits to zero */
	#define KVADDR_TO_PADDR(vaddr) ((vaddr)-MIPS_KSEG0) // not used for now
	#define CLEAR_PAGE_FRAME 0x00000fff

/*
	Do the right thing, since the faulting address has been validated
*/
int handle_vaddr_fault (vaddr_t faultaddress, unsigned int permissions) {

	assert(curspl > 0);
	vaddr_t vaddr;
	paddr_t physical_PN, paddr;

	int level1_index = (faultaddress & FIRST_LEVEL_PN) >> 22; 
	int level2_index = (faultaddress & SEC_LEVEL_PN) >> 12;
	// check if the 2nd level page table exists
	struct as_pagetable *level2_pagetable = curthread->t_vmspace->as_master_pagetable[level1_index];
	/************************************* 2nd Level Pagetable exists***************************************/
	if(level2_pagetable != NULL) {
	
		u_int32_t pte = level2_pagetable[level2_index];

		if (pte & PTE_PRESENT) {
			// page is present in physical memory, meaning this is merely a TLB miss,
			// so we just load the mapping into TLB
			assert((pte & PAGE_FRAME) != 0xdeadb);
			paddr = pte & PHY_PAGENUM; 

			if (permissions & PF_W) {
				// if we have the permission to write, we set the TLB dirty bit
				paddr |= TLBLO_DIRTY; //set DIRTY bit to 1. 
			}

		} else {
			// if page is not present, one case is that the page was swapped out...
			if (pte & PTE_SWAPPED) { 
				// find a free frame and load it back
				int i = 0, found = 0;
				for (; i < num_frames; i++) {
					if (coremap[i].frame_state == FREE){
						found = 1;
						load_page(curthread, vaddr, i);
						paddr = coremap[i].frame_start;
						break;
					}
				}
				// no free frame found, make one :)
				if(!found) {
					int free_frame = evict_or_swap();
					load_page(curthread, faultaddress, free_frame);
					paddr = coremap[free_frame].frame_start;
				}
			} else {
				//...the other case is that this page does not exist, so we allocate one
				paddr = alloc_page_userspace(faultaddress);
				// update the PTE to PRESENT
			    assert((pte & PTE_PRESENT) == 0);
			    assert((pte & PTE_SWAPPED) == 0); // come on, we just allocated it, can't be in the swapfile ;)
			    level2_pagetable[level2_index] |= PTE_PRESENT;
			}
			// once we're here, we have a valid pte and physical page in mem
			// so far we've got the desired page in memory
			// now udpate the PTE with the physical frame number and PRESENT bit
			*pte = (*pte & CLEAR_PAGE_FRAME);
			*pte = (*pte | paddr);
	    	*pte = (*pte | PTE_PRESENT);
			assert((paddr & PAGE_FRAME) == paddr);
			if (permissions & PF_W) {
				// if we intend to write, we set the TLB dirty bit
				paddr |= TLBLO_DIRTY; //set DIRTY bit to 1. 
			}
		}
	} else {
		/************************************* 2nd Level Pagetable DNE ***************************************/
		// If second page table doesn't exists, create second page table --> demand paging part
		level2_pagetable = kmalloc(sizeof(struct as_pagetable));
		// initialize the pagetable to invalid, by unsetting both the PRESENT and SWAPPED bits 
		int i = 0;
		for (; i < SECOND_LEVEL_PT_SIZE; i++) {
			level2_pagetable[i] &= INVALIDATE_PTE;
		}
	    // allocate a page and do the mapping
	    physical_PN = alloc_page_userspace(faultaddress);
	    assert(physical_PN % PAGE_SIZE == 0);
		
	    // update the PTE to present
	    u_int32_t* pte = get_PTE(curthread, faultaddress); 
	    assert((*pte & PTE_PRESENT) == 0);
	    *pte = (*pte | PTE_PRESENT);
	    assert(*pte == level2_pagetable[level2_index]);
	    // and prepare the physical address for loading the TLB
	    paddr = physical_PN;
		if(permissions & PF_W){
			paddr |= TLBLO_DIRTY; 
		}
	}

	/* once we are here, it means that we can guarantee that there exists PTE in page table for faultaddress.
	Now we need to load the mapping to TLB */
	
	u_int32_t tlb_hi, tlb_low;
	int k = 0;	
	for(; k< NUM_TLB; k++){
		TLB_Read(&tlb_hi, &tlb_low, k);
		// skip valid ones
		if(tlb_low & TLBLO_VALID){
			continue;
		}
		//once we find an empty entry, update it with tlb_hi and tlb_loww.
		tlb_hi = faultaddress;
		tlb_low = paddr | TLBLO_VALID; // set the physical page frame 's  VALID bit to 1.
		TLB_Write(tlb_hi, tlb_low, k);
		return 0;
	}
	// no invalid ones, so we randomly kicked out an entry
	tlb_hi = faultaddress;
	tlb_low = paddr | TLBLO_VALID;
	TLB_Random(tlb_hi, tlb_low);
	return 0;
}



/*
	Function to clear the content of a certain number of pages
*/
void as_zero_region(paddr_t paddr, size_t num_pages) {
	bzero((void *) PADDR_TO_KVADDR(vaddr), num_pages * PAGE_SIZE);
}


/*
	Function to write a page to swap_file, and update the page table accordingly
	@param frame_id, pos is the starting offset for the write operation
	Note: the position has already 
*/
void swap_out(int frame_id, off_t pos) {
	assert(curspl > 0);
	struct uio u;
	paddr_t dest = coremap[frame_id].frame_start;
	// initialize the uio
	mk_kuio(&u, PADDR_TO_KVADDR(dest), PAGE_SIZE, pos, UIO_WRITE);
	// does the actual write
	int result = VOP_WRITE(swap_file, &u);
	if(result){
		panic("write page to disk failed");
	}
	return;
}


/*
	Loads a page to physical memory at the specified frame_id
	@precondition: the physical page must be free for loading
*/
void load_page(struct thread* owner_thread, vaddr_t vaddr, int frame_id) {
	// go through the swap file mapping for the swapped page

	u_int32_t *pte = get_PTE(owner_thread, vaddr); 
	assert(pte != NULL);
	off_t pos = (*pte & SWAPFILE_OFFSET) * PAGE_SIZE; 
	struct uio u;
	paddr_t dest = coremap[frame_id].frame_start;
	mk_kuio(&u, PADDR_TO_KVADDR(dest), PAGE_SIZE, pos, UIO_READ);
	int result = VOP_READ(swap_file, &u);
	if (result) {
		panic("load page from disk failed");
	}
	return;
}


/*
	A demon thread that periodically evicts the dirty pages
	i.e. mark the physical pages as CLEAN
*/
void background_paging(void * args, unsigned int argc) {
	(void) args;
	(void) args;
	int spl = splhigh();
	int i = 0;
	for (; i < num_frames; i++) {
		if(coremap[i].frame_state == DIRTY) {
			// if this page already has a copy in swapfile, we update it
			vaddr_t va = coremap[i].mapped_vaddr;
			assert(va != 0xDEADBEEF);
			u_int32_t *pte = get_PTE();
			assert(pte != NULL); // 2nd level page table does not exist? u f**king kidding me right?
			assert((*pte & PTE_PRESENT) != 0); // come on, you must be present
			int disk_slot = 0;
			off_t disk_addr = 0;
			if (*pte & PTE_SWAPPED) {
				// if this page was ever swapped to the disk... 
				// shit, need to find it. the PTE does not have the  

			} else {
				// otherwise, we find a new slot in swapfile and do the swapping
				bitmap_alloc(swapfile_map, &disk_slot);
				disk_addr = disk_slot * PAGE_SIZE;
			}
			swap_out(i, disk_addr);
		}
	}
	splx(spl);
}