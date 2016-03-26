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
#define PTE_SWAPPED 0xfffff7ff

/********************************** Extern Declarations************************************/
extern struct thread* curthread;
/*************** Structure representing the physical pages/frames in memory ***************/
typedef struct frame {
	// int pid;	// the process it belongs to
	struct thread* owner_thread;
	int frame_id;	// position in array
	paddr_t frame_start; // the starting address of this page, needed for address translation
	vaddr_t mapped_vaddr; // the virtual address this frame is mapped to
	enum frame_state state; 
	int num_pages_allocated; // number of contiguous pages in a single allocation (e.g., large kmalloc)
} frame;

enum frame_state {
	FREE,
	FIXED, // some critical pages shall remain on physical memory
	ALLOCATED, // TODO: to be replaced by other states
	DIRTY,
	ClEAN,
};

/*********************************** The coremap *****************************************/
frame* coremap;
size_t num_frames;
size_t num_fixed_page;
/*********************************** Swap file *******************************************/
struct vnode * swap_file;
off_t cur_pos;
struct bitmap* disk_map;
#define MAX_SWAPFILE_SLOTS 16384 // TODO: we support upto 16384 pages on disk
 /**************************** Convenience Function **************************************/
int swap_out(int frame_id);

int load_page(int pid, vaddr_t vaddr);

u_int32_t* get_PTE (struct thread*, vaddr_t va);
/*****************************************************************************************/

void swapping_init(){
	// for swapping subsystem
	int err = vfs_open("swap_file", O_RDWR|O_CREAT, &swap_file);
	if (err) {
		panic("vfs_open on swap_file failed");
	}
	cur_pos = 0;
	disk_map = bitmap_create(MAX_SWAPFILE_SLOTS);
}


void
vm_bootstrap(void)
{
	u_int32_t start, end, start_adjusted;

	size_t num_pages = 0;
	// get the amount of physical memory
	ram_getsize(&start, &end);
	// align start
	start += start - start % PAGE_SIZE;
	// TODO: THIS ALIGNMENT IS PROBABLY NOT RIGHT
	num_pages = (end - start/ PAGE_SIZE);
	kprintf("start: %x, end: %x\n", start, end);
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
		// coremap[i].pid = -1;
		coremap[i].owner_thread = curthread;
		coremap[i].frame_id = i;
		coremap[i].frame_start = start + PAGE_SIZE * i;
		// the fixed pages are mapped to kernel addr space
		coremap[i].mapped_vaddr = PADDR_TO_KVADDR(coremap[i].frame_start);
		coremap[i].frame_state = FIXED;
		coremap[i].num_pages_allocated = 0;
	}
	// mark the pages in [start_adjusted, end] as free 
	free_pages = num_pages - fixed_pages;
	assert(num_pages == (fixed_pages + free_pages));

	for (; i < free_pages; i++) {
		// coremap[i].pid = -1;
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
	/**************************************** END of init ******************************************/
	// TODO: we may want to set some flags to indicate that vm has already bootstrapped, 
}


/*
	Function to get a pointer to the PTE corresponding to the virtual address va.
*/
u_int32_t* get_PTE(struct thread* addrspace_owner, vaddr_t va) {
	int level1_index = (va & FIRST_LEVEL_PN) >> 22; 
	int level2_index = (va & SEC_LEVEL_PN) >> 12;
	struct as_pagetable* level2_pagetable = addrspace_owner->t_vmspace->as_master_pagetable[level1_index];
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
	@return the id of the freed frame
	TODO: an optimization: find clean pages if possible.
*/
int evict_or_swap(){
	assert(curspl > 0);
	int kicked_ass_page;
	do{
		kicked_ass_page = random() % num_frames;
	} while (coremap[kicked_ass_page].frame_state != ALLOCATED);

	/* TODO need to swap the page to disk and update the PTE */
	// check the state of this page...
	if (coremap[kicked_ass_page].frame_state == DIRTY) {
		int disk_slot;
		bitmap_alloc(disk_map, &disk_slot);
		off_t disk_addr = disk_slot * PAGE_SIZE;
		swap_out(kicked_ass_page, disk_addr);
		return kicked_ass_page;
	} else {
		// page clean, no need for swap, just update the PTE and return
	}
	// update PTE of the *swapped* page and return frame id for loading/allocation
	u_int32_t *pte = get_PTE(coremap[kicked_ass_page].owner_thread,
							 coremap[kicked_ass_page].mapped_vaddr);
	// set the swapped bit (PRESENT BIT = 0)
	*pte = (*pte & PTE_SWAPPED);
	return kicked_ass_page;
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
			coremap[i].owner_thread = curthread;
			coremap[i].frame_state = ALLOCATED;
			coremap[i].num_pages_allocated = 1; 
			return PADDR_TO_KVADDR(coremap[i].frame_start);
		}
	}

	// no free page available right now --> we need to evict/swap a page
	int kicked_ass_page = evict_or_swap();
	// now do the allocation
	coremap[kicked_ass_page].owner_thread = curthread;
	coremap[kicked_ass_page].frame_state = ALLOCATED;
	coremap[kicked_ass_page].mapped_vaddr = PADDR_TO_KVADDR(coremap[kicked_ass_page].frame_start);
	coremap[kicked_ass_page].num_pages_allocated = 1;
	// return the physical page number 
	return (coremap[kicked_ass_page].mapped_vaddr);
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
			coremap[j].owner_thread = curthread;
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
			coremap[kicked_ass_page + i].owner_thread = curthread;
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
	vaddr_t ret = (npages == 1) ? alloc_one_page() : alloc_npages(npages);
	splx(spl);
	return ret; 
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

	// as_region cur = as->as_regions_start;
	vaddr_t faulting_PN = 0;
	unsigned int permission_bit = 0;
	vaddr_t vbase, vtop;
	//Go through the array of regions to check which region does this faultaddress fall into. Note that those regions don't include user stack!

	int i = 0;
	int found = 0;
	for (; i < array_getnum(as->as_regions); i++) {

		struct as_region * cur = array_getguy(as->as_regions, i);
		vbase = cur->vbase;
		vtop = vbase + cur->npages * PAGE_SIZE;
		// base addr should be page aligned
		assert((cur->vbase & PAGE_FRAME) == cur->as_vbase);  

		// find which region the faulting address belongs to
		if(faultaddress >= vbase && faultaddress < vtop){
			// faulting_PN = faultaddress;
			// permission_bit = cur->region_permis;
			found = 1;
			int err = handle_vaddr_fault(faultaddress, paddr, cur->region_permis); 
			splx(spl);
			return err;
			/**********************************************************/
		}
	}

	//if we didn't fall into any region, we check if it falls into the user stack.
	// TODO: don't check stack for now
	/*if(!found){
		vtop = USERSTACK;
		vbase = vtop - VM_STACKPAGES * PAGE_SIZE; // TODO need to define VM_STACKPAGES later.... it should be a variable.
		if(faultaddress >= vbase && faultaddress < vtop)
		{
			faulting_PN = faultaddress;
			//Here we set the user stack to be both readable and writable. 
			permission_bit |= (PF_W | PF_R); //stack is readable,writable but not executable ! 
			return handle_vaddr_fault(faultaddress, paddr, permission_bit);
		}
	}*/
	// cannot find the faulting address, probably a segfault
	if(faulting_PN == 0){
		splx(spl);
		return EFAULT;
	}
}

	#define FIRST_LEVEL_PN 0xffc00000
	#define SEC_LEVEL_PN 0x003fc000
    #define PTE_VALID //TODO
	#define PAGE_FRAME 0xfffff000	/* mask for getting page number from addr FIRST 20 bits*/
	#define PHY_PAGENUM 0xfffff000 
	// #define KVADDR_TO_PADDR(vaddr) ((vaddr)-MIPS_KSEG0)

int handle_vaddr_fault (vaddr_t faultaddress, paddr_t paddr, unsigned int permission_bit) {
	
	vaddr_t vaddr;
	paddr_t physical_PN;
	u_int32_t ehi,elo;

	int level1_index = (faultaddress & FIRST_LEVEL_PN) >> 22; 
	int level2_index = (faultaddress & SEC_LEVEL_PN) >> 12;

	struct as_pagetable *level2_pagetable = curthread->t_vmspace->as_master_pagetable[level1_index];

	if(level2_pagetable != NULL) {
		// If the second level page table exits,
        // translate it into physical address, 
        // check writeable flag,
        // and prepare the physical address for TLBLO
        // get the address stores in PTE, 
		u_int32_t pte = level2_pagetable[level2_index];

		if (pte & PTE_PRESENT) {
			// page is present in physical memory
			physical_PN = pte & PHY_PAGENUM; // get the physical page number

			if (prermission_bit & PF_W) {
				// if we intend to write, we set the TLB dirty bit
				paddr |= TLBLO_DIRTY; //set DIRTY bit to 1. 
			}

		} else {
			/* page not in physical memory */
			// If not exists, do the mapping, 
        	// update the PTE of the second page table,
        	// check writeable flag,

        /*	// and prepare the physical address for TLB
			vaddr = alloc_kpages(1); 
			assert(vaddr != 0);
			as_zero_region(vaddr,1); 
			*second_level_PTE | = (vaddr | PTE_VALID); 
			paddr = KVADDR_TO_PADDR(vaddr);
*/

			if (permission_bit & PF_W) {
				// if we intend to write, we set the TLB dirty bit
				paddr |= TLBLO_DIRTY; //set DIRTY bit to 1. 
			}
		}
	} else {
		// If second page table doesn't exists, create second page table --> demand paging
		level2_pagetable = kmalloc(sizeof(struct as_pagetable));
	    // allocate a page and do the mapping,
		paddr = alloc_kpages(1);

		
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
	// TODO udpate the page table entry 
	return;
}

/*
	Loads a page to physical memory at the specified frame_id
	@precondition: the physical page must be free for loading
*/
void load_page(int pid, vaddr_t vaddr, int frame_id) {
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
