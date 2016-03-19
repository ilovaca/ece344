#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <thread.h>
#include <curthread.h>
#include <addrspace.h>
#include <vm.h>
#include <machine/spl.h>
#include <machine/tlb.h>

/*
 * Dumb MIPS-only "VM system" that is intended to only be just barely
 * enough to struggle off the ground. You should replace all of this
 * code while doing the VM assignment. In fact, starting in that
 * assignment, this file is not included in your kernel!
 */

/* under dumbvm, always have 48k of user stack */
#define DUMBVM_STACKPAGES    12

void
vm_bootstrap(void)
{

}

static
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

/* Allocate/free some kernel-space virtual pages */
vaddr_t 
alloc_kpages(int npages)
{
	paddr_t pa;
	pa = getppages(npages); //we obtain the physical address of allocated pages from memory.
	if (pa==0) {
		return 0;
	}
	return PADDR_TO_KVADDR(pa);
}

void 
free_kpages(vaddr_t addr)
{
	/* nothing */

	(void)addr;
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


	as_section cur = as->as_section_start;
	vaddr_t faulting_PN = 0;
	unsigned int prermission_bit = 0;

	vaddr_t vbase,vtop;
	//Go through the link list of regions to check which region does this faultaddress fall into. Note that those regions don't include user stack!

	for (; cur != 0; cur = cur->as_next_section) {

		assert(cur->as_vbase != 0);
		assert(cur->as_npages != 0);
		//check if the base address is page aligned. i.e.the as_vbase is divisible by 4K.
		assert((as-> as_vbase & PAGE_FRAME) == as->as_vbase); 
		// get the region bounds
		vbase = cur-> as_vbase;
		vtop = vbase + cur-> as_npages * PAGE_SIZE;
		// check if the faultaddress falls within this region:
		if(faultaddress >= vbase && faultaddress < vtop){
			faulting_PN = faultaddress;
			// TODO: WHAT IS THIS?
			prermission_bit = cur->as_permissions;
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
		prermission_bit |= (PF_W | PF_R); //stack is readable,writable but not executable ! 

	}

	//If faultaddress is still not within any range of the region and stack, then we return error code.
	if(faulting_PN == 0) //Note: Since the top 1 GB is used for kernel space, so the user stack won't not start with 0x00000000, but 0x80000000.
	{
		return EFAULT;
	}

	int err;
	if (err = handle_vaddr_fault(faultaddress,paddr,prermission_bit)) {
		return err;
	}
}

	#define FIRST_LEVEL_PN 0xffc00000
	#define SEC_LEVEL_PN 0x003fc000
    #define PTE_VALID //TODO
	#define PAGE_FRAME 0xfffff000	/* mask for getting page number from addr FIRST 20 bits*/
	#define KVADDR_TO_PADDR(vaddr) ((vaddr)-MIPS_KSEG0)

void as_zero_region(vaddr_t vaddr, size_t num_pages) {
	vaddr_t * cur = vaddr;
	int i = 0;
	for (; i < num_pages*PAGE_SIZE; i++) {
		*cur = 0;
		 cur++;
	} 
}


int handle_vaddr_fault (vaddr_t faultaddress,paddr_t paddr, unsigned int prermission_bit) {
	/* Once we found faultaddress falls into certan regions, then we do the masking to obtain level1_index and level2_index. 
	Note that for 2-level page table: the top 10 bits are used as level1_index, and the immediate 10 bits are used as level2_index. And
	the last 12 bits is the offset */
	unsigned int level1_index, level2_index;
	vaddr_t vaddr;
	u_int32_t ehi,elo;


	level1_index = (faultaddress & FIRST_LEVEL_PN) >> 22; 
	level2_index = (faultaddress & SEC_LEVEL_PN) >> 12;

	vaddr_t * first_level_PTE = (vaddr_t*) (as->as_pagetable + level1_index * 4); // first-level PT contains a pointer to the 2nd level PT
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

			if (prermission_bit & PF_W) {
				// if we intend to write, we set the TLB dirty bit
				paddr |= TLBLO_DIRTY; //set DIRTY bit to 1. 
			}
		}
	}
	else
	{
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
		as_zero_region(temp,1);

		//assign the newly allocated viraddr with VALID bit setting to 1 to second_level_PTE.
		*second_level_PTE = (temp | PTE_VALID);

		//now we set up the corresponding physical address for second_level_PTE.
		paddr = KVADDR_TO_PADDR(temp);
		if(prermission_bit & PF_W){
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
	ehi = faultaddressl
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

struct addrspace *
as_create(void)
{
	struct addrspace *as = kmalloc(sizeof(struct addrspace));
	if (as==NULL) {
		return NULL;
	}

	as->as_vbase1 = 0;
	as->as_pbase1 = 0;
	as->as_npages1 = 0;
	as->as_vbase2 = 0;
	as->as_pbase2 = 0;
	as->as_npages2 = 0;
	as->as_stackpbase = 0;

	return as;
}

void
as_destroy(struct addrspace *as)
{
	kfree(as);
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

int
as_define_region(struct addrspace *as, vaddr_t vaddr, size_t sz,
		 int readable, int writeable, int executable)
{
	size_t npages; 

	/* Align the region. First, the base... */
	sz += vaddr & ~(vaddr_t)PAGE_FRAME;
	vaddr &= PAGE_FRAME;

	/* ...and now the length. */
	sz = (sz + PAGE_SIZE - 1) & PAGE_FRAME;

	npages = sz / PAGE_SIZE;

	/* We don't use these - all pages are read-write */
	(void)readable;
	(void)writeable;
	(void)executable;

	if (as->as_vbase1 == 0) {
		as->as_vbase1 = vaddr;
		as->as_npages1 = npages;
		return 0;
	}

	if (as->as_vbase2 == 0) {
		as->as_vbase2 = vaddr;
		as->as_npages2 = npages;
		return 0;
	}

	/*
	 * Support for more than two regions is not available.
	 */
	kprintf("dumbvm: Warning: too many regions\n");
	return EUNIMP;
}

int
as_prepare_load(struct addrspace *as)
{
	assert(as->as_pbase1 == 0);
	assert(as->as_pbase2 == 0);
	assert(as->as_stackpbase == 0);

	as->as_pbase1 = getppages(as->as_npages1);
	if (as->as_pbase1 == 0) {
		return ENOMEM;
	}

	as->as_pbase2 = getppages(as->as_npages2);
	if (as->as_pbase2 == 0) {
		return ENOMEM;
	}

	as->as_stackpbase = getppages(DUMBVM_STACKPAGES);
	if (as->as_stackpbase == 0) {
		return ENOMEM;
	}

	return 0;
}

int
as_complete_load(struct addrspace *as)
{
	(void)as;
	return 0;
}

int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
	assert(as->as_stackpbase != 0);

	*stackptr = USERSTACK;
	return 0;
}

int
as_copy(struct addrspace *old, struct addrspace **ret)
{
	struct addrspace *new;

	new = as_create();
	if (new==NULL) {
		return ENOMEM;
	}

	new->as_vbase1 = old->as_vbase1;
	new->as_npages1 = old->as_npages1;
	new->as_vbase2 = old->as_vbase2;
	new->as_npages2 = old->as_npages2;

	if (as_prepare_load(new)) {
		as_destroy(new);
		return ENOMEM;
	}

	assert(new->as_pbase1 != 0);
	assert(new->as_pbase2 != 0);
	assert(new->as_stackpbase != 0);

	memmove((void *)PADDR_TO_KVADDR(new->as_pbase1),
		(const void *)PADDR_TO_KVADDR(old->as_pbase1),
		old->as_npages1*PAGE_SIZE);

	memmove((void *)PADDR_TO_KVADDR(new->as_pbase2),
		(const void *)PADDR_TO_KVADDR(old->as_pbase2),
		old->as_npages2*PAGE_SIZE);

	memmove((void *)PADDR_TO_KVADDR(new->as_stackpbase),
		(const void *)PADDR_TO_KVADDR(old->as_stackpbase),
		DUMBVM_STACKPAGES*PAGE_SIZE);
	
	*ret = new;
	return 0;
}
