#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <thread.h>
#include <curthread.h>
#include <addrspace.h>
#include <vm.h>
#include <machine/spl.h>
#include <machine/tlb.h>

enum frame_state {
	FREE,
	FIXED, // some critical pages shall remain on physical memory
	ALLOCATED, // TODO: can be changed to other states
	DIRTY,
	ClEAN,
};
/*************** Structure representing the physical pages/frames in memory ***************/
typedef struct frame {
	int pid;	// the process it belongs to
	int frame_id;	// position in array
	paddr_t frame_start; // the starting address of this page, needed for address translation
	enum frame_state state; 
	int num_pages_allocated; // number of contiguous pages in a single allocation (e.g., large kmalloc)
} frame;

/*********************************** The coremap *****************************************/
frame* coremap;
size_t num_frames;
size_t num_fixed_page;
/*********************************** Swap file *******************************************/
struct vnode * swap_file;
off_t cur_pos;
/*********************************** Convenience Function ********************************/

int swap_out(int frame_id);

int load_page(int pid, vaddr_t vaddr);


/* Things to do:
 *	1. initialize the coremap, note we don't have vm yet.
 *		
 *
 */ 

void
vm_bootstrap(void)
{
	u_int32_t start, end, start_adjusted;

	size_t num_pages = 0;
	// get the amount of physical memory
	ram_getsize(&start, &end);
	// make sure they're page aligned
	// TODO: THIS ALIGNMENT IS PROBABLY NOT RIGHT
	num_pages = (end / PAGE_SIZE) * PAGE_SIZE;
	// kernel always looks at the virtual memory only
	coremap = (frame *)PADDR_TO_KVADDR(start);
	/************************************** ALLOCATE SPACE *****************************************/
	// first the coremap
	start_adjusted = start + num_pages * sizeof(struct frame); 
	// then the swap file
	/************************************** initialize coremap *************************************/
	int i = 0, fixed_pages, free_pages;
	fixed_pages = (start_adjusted - start) / PAGE_SIZE;
	// mark the pages in [start, start_adjusted] as fixed
	for (; i < fixed_pages; i++) { 
		coremap[i].pid = -1;
		coremap[i].frame_id = i;
		coremap[i].frame_start = start + PAGE_SIZE * i;
		coremap[i].frame_state = FIXED;
		coremap[i].num_pages_allocated = 0;
	}
	// mark the pages in [start_adjusted, end] as free 
	free_pages = num_pages - fixed_pages;
	assert(num_pages == (fixed_pages + free_pages));

	for (; i < free_pages; i++) {
		coremap[i].pid = -1;
		coremap[i].frame_id = i;
		coremap[i].frame_start = start_adjusted + PAGE_SIZE * i;
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
	// TODO: allocate space for the struct !
	int err = vfs_open("swap_file", O_RDWR|O_CREAT, &swap_file);
	if (err) {
		panic("vfs_open on swap_file failed");
	}
	cur_pos = 0;
	/**************************************** END of init ******************************************/
	// TODO: we may want to set some flags to indicate that vm has already bootstrapped, 
}


/*
	Function that makes room for a single page
	Page replacement policy (experimental) >>>>> randomly evict a page
	Depending on the state of the page, we either
		** evict the page, should the page be clean,
		or
	 	** swap to disk, should the page be dirty
	@return the id of the freed frame
*/
int evict_or_swap(){
	assert(curspl > 0);
	int kicked_ass_page;
	do{
		kicked_ass_page = random() % num_frames;
	} while (coremap[kicked_ass_page].frame_state != ALLOCATED);

	/* TODO need to swap the page to disk update the PTE */
	swap_out();
}


/*
	Allocates a single page
	@return starting address of the newly allocated page
*/
vaddr_t alloc_one_page() {
	assert(curspl > 0);
	int i = 0;
	// go through the coremap check if there's a free page
	for (; i < num_frames; i++) {
		if (coremap[i].frame_state == FREE) {
			coremap[i].frame_state = ALLOCATED;
			coremap[i].num_pages_allocated = 1; 
			return PADDR_TO_KVADDR(coremap[i].frame_start);
		}
	}

	// no free page available right now --> we need to evict/swap a page
	int kicked_ass_page = evict_or_swap();
	// now do the allocation
	coremap[kicked_ass_page].pid = curthread->pID;
	coremap[kicked_ass_page].frame_state = ALLOCATED;
	coremap[kicked_ass_page].num_pages_allocated = 1;
	return PADDR_TO_KVADDR(coremap[kicked_ass_page].frame_start);
}

/*
	Allocate npages.
*/
vaddr_t alloc_npages(int npages) {
	assert(curspl > 0);
	int num_continous = 0;
	int i = 0; 
	// find if there're npages in succession
	for (; i < num_frames - 1; i++){
		// as long as we've got enough, we break:)
		if (num_continous >= npages) break;
		if (coremap[i].frame_state == FREE && coremap[i + 1].frame_state == FREE) {
			num_continous++;
		} else {
			num_continous = 0;
		}
	}
	assert(num_continous == npages);
	if (num_continous >= npages) {
		// found n continous free pages
		int j = i;
		for (; j > i - npages; j--)
		{
			coremap[j].pid = curthread->pID;
			coremap[j].frame_state = ALLOCATED;
			// redundancy not a problem ;)
			coremap[j].num_pages_allocated = npages; 
		}
		return PADDR_TO_KVADDR(coremap[j].frame_start);

	} else {
		// not found, we evict n pages starting from a random page
		// make sure the n pages does not overflow the end of memory
		// 		--> search the first num_frames - npages, excluding the fixed pages
		int kicked_ass_page;
		int search_range = num_frames - npages - num_fixed_page;
		do{
			kicked_ass_page = (random() % search_range) + num_fixed_page;
		} while (coremap[kicked_ass_page].frame_state != ALLOCATED);

		assert(coremap[kicked_ass_page].frame_state == ALLOCATED);
		// sanity check
		int i = 0;
		for (; i < npages; i++) {
			if (coremap[i + kicked_ass_page].frame_state == FIXED) panic("alloc_npages contains a fixed page"); 
		}
		// evict/swap to disk
		for (i = 0; i < npages; i++) {
			/*
	
			 TODO: swap to disk 

			*/
			coremap[kicked_ass_page + i].pid = curthread->pID;
			coremap[kicked_ass_page + i].frame_state = ALLOCATED;
			coremap[kicked_ass_page + i].num_pages_allocated = npages; 
		}
		return PADDR_TO_KVADDR(coremap[kicked_ass_page].frame_start);
	}
}


/* Allocate/free some kernel-space virtual pages */
vaddr_t 
alloc_kpages(int npages)
{	
	int spl = splhigh();
	ret_addr = (npages == 1) ? alloc_one_page() : alloc_npages(npages);
	splx(spl);
	return; 
}

/*
	Function to free a certain number of pages given *ONLY* the starting address of the page.
	*** The given address is page aligned ***
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
	u_int32_t ehi, elo;
	struct addrspace *as;
	int spl;

	spl = splhigh();

	//Align faultaddress
	faultaddress &= PAGE_FRAME; //PAGE_FRAME: 0xfffff000 =  mask for getting page number from addr 

	//Upon this step, faultaddress is the page number from the passed address.


	DEBUG(DB_VM, "dumbvm: fault: 0x%x\n", faultaddress);

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

	//u_int32_t vaddr_t


	as_region cur = as->as_regions_start;
	vaddr_t faulting_PN = 0;
	unsigned int permission_bit = 0;

	vaddr_t vbase,vtop;
	//Go through the link list of regions to check which region does this faultaddress fall into. Note that those regions don't include user stack!


/* struct as_region{
 	vaddr_t vir_base;
 	size_t npages;
 	unsigned int as_permissions;
 	struct as_region *as_next_section;
 };
*/

	for (; cur != 0; cur = cur->as_next_section) {

	/*	assert(cur->as_vbase != 0);
		assert(cur->as_npages != 0);
		//check if the base address is page aligned. i.e.the as_vbase is divisible by 4K.

		assert((as-> as_vbase & PAGE_FRAME) == as->as_vbase);  */

		// check if the faultaddress falls within this region:
		if(faultaddress >= cur->vir_base && faultaddress < (vbase + npages * PAGE_SIZE)){
			faulting_PN = faultaddress;
			permission_bit = cur->as_permissions;
			break;
		}
	}

	//if we didn't fall into any region, we check if it falls into the user stack.
	assert(faulting_PN == 0);
	vtop = USERSTACK;
	vbase = vtop - VM_STACKPAGES * PAGE_SIZE; // TODO need to define VM_STACKPAGES later.... it should be a variable.
	if(faultaddress >= vbase && faultaddress < vtop)
	{
		faulting_PN = faultaddress;
		//Here we set the user stack to be both readable and writable. 
		permission_bit |= (PF_W | PF_R); //stack is readable,writable but not executable ! 

	}

	//If faultaddress is still not within any range of the region and stack, then we return error code.
	if(faulting_PN == 0) //Note: Since the top 1 GB is used for kernel space, so the user stack won't not start with 0x00000000, but 0x80000000.
	{
		return EFAULT;
	}

	int err;
	if (err = handle_vaddr_fault(faultaddress,paddr,permission_bit)) {
		return err;
	}
}

	#define FIRST_LEVEL_PN 0xffc00000
	#define SEC_LEVEL_PN 0x003fc000
    #define PTE_VALID //TODO
	#define PAGE_FRAME 0xfffff000	/* mask for getting page number from addr FIRST 20 bits*/
	#define KVADDR_TO_PADDR(vaddr) ((vaddr)-MIPS_KSEG0)





int handle_vaddr_fault (vaddr_t faultaddress,paddr_t paddr, unsigned int permission_bit) {
	/* Once we found faultaddress falls into certan regions, then we do the masking to obtain level1_index and level2_index. 
	Note that for 2-level page table: the top 10 bits are used as level1_index, and the immediate 10 bits are used as level2_index. And
	the last 12 bits is the offset */
	unsigned int level1_index, level2_index;
	vaddr_t vaddr;
	u_int32_t ehi,elo;


	level1_index = (faultaddress & FIRST_LEVEL_PN) >> 22; 
	level2_index = (faultaddress & SEC_LEVEL_PN) >> 12;

	vaddr_t * first_level_PTE = (vaddr_t*) (as->as_master_pagetable + level1_index * 4); // first-level PT contains a pointer to the 2nd level PT
	if(*first_level_PTE != NULL) {
		// If the second level page table exits,
        // get the address stores in PTE, 
        // translate it into physical address, 
        // check writeable flag,
        // and prepare the physical address for TLBLO
		vaddr_t * second_level_PTE = (vaddr_t*) (*first_level_PTE + level2_index * 4);
		if (*second_level_PTE & PTE_VALID) {
			/* the PTE is valid, meaning the requested page is currently in physical memory */
			vaddr = *second_level_PTE & PAGE_FRAME; // get the physical page number
			// do the virtual -> physical translation, we get the physical page number
			paddr = KVADDR_TO_PADDR(vaddr);

			if (prermission_bit & PF_W) {
				// if we intend to write, we set the TLB dirty bit
				paddr |= TLBLO_DIRTY; //set DIRTY bit to 1. 
			}
		} else {
			/* PTE invalid, we need */
			// If not exists, do the mapping, 
        	// update the PTE of the second page table,
        	// check writeable flag,

        	// and prepare the physical address for TLB
			vaddr = alloc_kpages(1); //allocate a new page in 2nd page table. Upon this function call, there already exists a mapped 
			//physical address.
			assert(vaddr != 0);
			as_zero_region(vaddr,1); //zero out the new page.
			*second_level_PTE | = (vaddr | PTE_VALID); //set the VALID bit of the new page to 1, and assign second_level_PTE to this new 
			//page's virtual address.
			paddr = KVADDR_TO_PADDR(vaddr);

			if (permission_bit & PF_W) {
				// if we intend to write, we set the TLB dirty bit
				paddr |= TLBLO_DIRTY; //set DIRTY bit to 1. 
			}
		}
	} else {
		// If second page table even doesn't exists, 
	    // create second page table,
	    // do the mapping,
	    // update the PTE,
	    // and prepare the physical address.

	    //Note: in both page tables, each entry stores virtual address, so each entry takes 4 bytes.

	    //we allocate a new entry to first_level_PTE in level1, whic is used to point to the new level2 page table.
		*first_level_PTE = alloc_kpages(1);
		assert(*first_level_PTE != 0);
		as_zero_region(*first_level_PTE,1); //zero out the new allocated page.

		//let second_level_PTE point to the specified entry in level2 page table.
		second_level_PTE = (vaddr_t *) (*first_level_PTE + level2_index*4);
		//create a temporary vaddr pointer to point to an newly allocated viraddr which is used for the specified level2 page table entry
		vaddr_t * temp = alloc_kpages(1);
		assert(temp != 0);
		as_zero_region(temp, 1);

		//assign the newly allocated viraddr with VALID bit setting to 1 to second_level_PTE.
		*second_level_PTE = (temp | PTE_VALID);

		//now we set up the corresponding physical address for second_level_PTE.
		paddr = KVADDR_TO_PADDR(temp);
		if(permission_bit & PF_W){
			paddr | = TLBLO_DIRTY; 
		}
	}

	/*once we are here, it means that we can guarantee that there exists physical address in page table for faultaddress.
	Now we need to update the TLB entry. Since TLB is global, we need to disable interrupts to manipute it. */

	/* Basic Idea:
		 if there still a empty TLB entry, insert new one in
    	if not, randomly select one, throw it, insert new one in */

	/* Note: #define NUM_TLB  64
		 TLB_Read: read a TLB entry out of the TLB into ENTRYHI and ENTRYLO. INDEX specifies which one to get.
 	void TLB_Read(u_int32_t *entryhi, u_int32_t *entrylo, u_int32_t index); */

	int spl = splhigh();

	int k = 0;
	
	//we loop through the TLB, iterator k is used as the index in TLB list.
	for(; k< NUM_TLB; k++){
		//we read kth TLB entry out of the TLB into ENTRYHI and ENTRYLO, and check if this entry is valid. If so, we keep iterating,
		TLB_Read(&ehi,&elo,k);
		if(elo & TLBLO_VALID){
			continue;
		}

		//recall: write the TLB entry specified by ENTRYHI and ENTRYLO
		//void TLB_Write(u_int32_t entryhi, u_int32_t entrylo, u_int32_t index);

		//once we find an empty entry, update it with ehi and elow.
		ehi = faultaddress;
		elo = paddr | TLBLO_VALID; // set the physical page frame 's  VALID bit to 1.
		TLB_Write(ehi,elo,k);
		splx(spl);
		return 0;
	}

	//if we are here, meaning that the TLB is full, and we need to use replacement policy to kick one page out.

	//dummy algorithm -----> randomly select a entry, and update it .
	ehi = faultaddress;
	elo = paddr | TLBLO_VALID;
	TLB_Random(ehi,elo);
	splx(spl);
	return 0;
}

// /* values for p_flags */
// #define	PF_R		0x4	/* Segment is readable */
// #define	PF_W		0x2	/* Segment is writable */
// #define	PF_X		0x1	/* Segment is executable */
//#define TLBLO_DIRTY   0x00000400


/*
	Function to clear the content of the page
*/
void as_zero_region(vaddr_t vaddr, size_t num_pages) {
	vaddr_t * cur = vaddr;
	int i = 0;
	for (; i < num_pages * PAGE_SIZE; i++) {
		*cur = 0;
		cur++;
	} 
}


/*
	Function to write a page to swap_file, and update the page table accordingly
	@param frame_id, pos is the starting offset for the write operation
*/
void swap_out(int frame_id, off_t pos) {
	struct uio u;
	paddr_t dest = coremap[frame_id].frame_start;
	// initialize the uio
	mk_kuio(&u, PADDR_TO_KVADDR(dest), PAGE_SIZE, pos, UIO_WRITE);
	// does the actual write
	int result = VOP_WRITE(swap_file, &u);
	if(result){
		panic("write page to disk failed");
	}
	// udpate the page table entry 
	return;
}

int load_page(int pid, vaddr_t vaddr, int frame_id) {
	// go through the swap file mapping for the swapped page


	off_t pos; // pos of the page in the swap file
	struct uio u;
	paddr_t dest = coremap[frame_id].frame_start;
	mk_kuio(&u, PADDR_TO_KVADDR(dest), PAGE_SIZE, pos, UIO_READ);
	int result = VOP_READ(swap_file, &u);
	if (result) {
		panic("load page from disk failed");
	}
	return;
}
