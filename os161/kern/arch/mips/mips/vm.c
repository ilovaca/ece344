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
	pa = getppages(npages);
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


	//Go through the link list of regions to check which region does this faultaddress fall into. Note that those regions don't include user stack!
	as_section current_section = as->as_section_start;
	vaddr_t fault_page_number = 0;
	unsigned int prermission_bit = 0;

	vaddr_t vbase,vtop;

	while(current_section != 0){
		assert(current_section->as_vbase != 0);
		assert(current_section->as_npages != 0);
		assert((as->as_vbase & PAGE_FRAME) == as->as_vbase); //check as_vbase is page aligned. i.e.the as_vbase is divisible by 4K.

		vbase = current_section->as_vbase;
		vtop = vbase + current_section->as_npages*PAGE_SIZE;
		//we check if the faultaddress falls within this range:
		if(faultaddress >= vbase && faultaddress < vtop){
			fault_page_number = faultaddress;
			prermission_bit = current_section->as_permissions;
			break;
		}
		//if we still haven't found it, keep checking
		current_section = current_section->as_next_section;
	}

	//if we didn't fall into any region, we check if it falles into the user stack.
	assert(fault_page_number == 0);
	vtop = USERSTACK;
	vbase = vtop - VM_STACKPAGES * PAGE_SIZE; //need to define VM_STACKPAGES later.... it should be a variable.
	if(faultaddress >= vbase && faultaddress < vtop)
	{
		fault_page_number = faultaddress;
		prermission_bit |= (PF_W | PF_R); //stack is readable,writable but not executable ! 
	}

	//If faultaddress is still not within any range of the region and stack, then we return error code.
	if(fault_page_number == 0) //Note: Since the top 1 GB is used for kernel space, so the user stack won't not start with 0x00000000, but 0x80000000.
	{
		return EFAULT;
	}

	/* Once we found faultaddress falls into certan regions, then we do the masking to obtain level1_index and level2_index. 
	Note that for 2-level page table: the top 10 bits are used as level1_index, and the immediate 10 bits are used as level2_index. And
	the last 12 bits is the offset */

	#define FIRST_LEVEL_PN 0xffc00000
	#define SEC_LEVEL_PN 0x003fc000
	unsigned int level1_index,level2_index;
	level1_index = (faultaddress & FIRST_LEVEL_PN) >> 22; 
	level2_index = (faultaddress & SEC_LEVEL_PN) >> 12;

	vaddr_t * first_level_PTE = (vaddr_t*) (as->as_pagetable + level1_index * 4); // first-level PT contains a pointer to the 2nd level PT
	if(*first_level_PTE != NULL) {
		vaddr_t * second_level_PTE = (vaddr_t*) (*first_level_PTE + level2_index * 4);
		// If the mapping exits in page table,
        // get the address stores in PTE, 
        // translate it into physical address, 
        // check writeable flag,
        // and prepare the physical address for TLBLO
        #define PTE_VALID
		if (*second_level_PTE & PTE_VALID) {
			/* the PTE is valid */
			vaddr = *second_level_PTE & PAGE_FRAME; // get the physical page number
			// do the virtual -> physical translation
			paddr = KVADDR_TO_PADDR(vaddr);
			if (prermission_bit & PF_W) {
				// if we intend to write, we set the TLB dirty bit
				paddr |= TLBLO_DIRTY;
			}
		} else {
			// PTE is invalid 
		}


	}



	
}
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
