/* See COPYRIGHT for copyright information. */

#include <inc/x86.h>
#include <inc/mmu.h>
#include <inc/error.h>
#include <inc/string.h>
#include <inc/assert.h>

#include <kern/pmap.h>
#include <kern/kclock.h>
#include <kern/env.h>
#include <kern/buddy.h>

#define KDEBUG
#include <kern/kdebug.h>


// physical contiguous page freelist sized from 2^0 to 2^MAX_ORDER
static struct Page_list page_free_list[MAX_ORDER + 1];

// These variables are set by i386_detect_memory()
static physaddr_t maxpa;	// Maximum physical address
size_t npage;			// Amount of physical memory (in pages)
static size_t basemem;		// Amount of base memory (in bytes)
static size_t extmem;		// Amount of extended memory (in bytes)

// These variables are set in i386_vm_init()
pde_t* boot_pgdir;		// Virtual address of boot time page directory
physaddr_t boot_cr3;		// Physical address of boot time page directory
static char* boot_freemem;	// Pointer to next byte of free mem

struct Page* pages;		// Virtual address of physical page array

// Global descriptor table.
//
// The kernel and user segments are identical (except for the DPL).
// To load the SS register, the CPL must equal the DPL.  Thus,
// we must duplicate the segments for the user and the kernel.
//
struct Segdesc gdt[] =
{
	// 0x0 - unused (always faults -- for trapping NULL far pointers)
	SEG_NULL,

	// 0x8 - kernel code segment
	[GD_KT >> 3] = SEG(STA_X | STA_R, 0x0, 0xffffffff, 0),

	// 0x10 - kernel data segment
	[GD_KD >> 3] = SEG(STA_W, 0x0, 0xffffffff, 0),

	// 0x18 - user code segment
	[GD_UT >> 3] = SEG(STA_X | STA_R, 0x0, 0xffffffff, 3),

	// 0x20 - user data segment
	[GD_UD >> 3] = SEG(STA_W, 0x0, 0xffffffff, 3),

	// 0x28 - tss, initialized in idt_init()
	[GD_TSS >> 3] = SEG_NULL
};

struct Pseudodesc gdt_pd = {
	sizeof(gdt) - 1, (unsigned long) gdt
};

static int
nvram_read(int r)
{
	return mc146818_read(r) | (mc146818_read(r + 1) << 8);
}

void
i386_detect_memory(void)
{
	// CMOS tells us how many kilobytes there are
	basemem = ROUNDDOWN(nvram_read(NVRAM_BASELO)*1024, PGSIZE);
	extmem = ROUNDDOWN(nvram_read(NVRAM_EXTLO)*1024, PGSIZE);

	// Calculate the maximum physical address based on whether
	// or not there is any extended memory.  See comment in <inc/mmu.h>.
	if (extmem)
		maxpa = EXTPHYSMEM + extmem;
	else
		maxpa = basemem;

	npage = maxpa / PGSIZE;

	cprintf("Physical memory: %dK available, ", (int)(maxpa/1024));
	cprintf("base = %dK, extended = %dK\n", (int)(basemem/1024), (int)(extmem/1024));
}

// --------------------------------------------------------------
// Set up initial memory mappings and turn on MMU.
// --------------------------------------------------------------

static void check_boot_pgdir(void);
static void boot_map_segment(pde_t *pgdir, uintptr_t la, size_t size, physaddr_t pa, int perm);

//
// A simple physical memory allocator, used only a few times
// in the process of setting up the virtual memory system.
// page_alloc() is the real allocator.
//
// Allocate n bytes of physical memory aligned on an 
// align-byte boundary.  Align must be a power of two.
// Return kernel virtual address.  Returned memory is uninitialized.
//
// If we're out of memory, boot_alloc should panic.
// This function may ONLY be used during initialization,
// before the page_free_list has been set up.
// 
static void*
boot_alloc(uint32_t n, uint32_t align)
{
	extern char end[];
	void *v;

	// Initialize boot_freemem if this is the first time.
	// 'end' is a magic symbol automatically generated by the linker,
	// which points to the end of the kernel's bss segment -
	// i.e., the first virtual address that the linker
	// did _not_ assign to any kernel code or global variables.
	if (boot_freemem == 0)
		boot_freemem = end;

	// LAB 2: Your code here:
	//	Step 1: round boot_freemem up to be aligned properly
	//	Step 2: save current value of boot_freemem as allocated chunk
	//	Step 3: increase boot_freemem to record allocation
	//	Step 4: return allocated chunk
	boot_freemem = ROUNDUP(boot_freemem, align);

	v = boot_freemem;
	boot_freemem += n;

	if (v > KADDR(maxpa - 1) || boot_freemem > (char *)KADDR(maxpa - 1))
		panic("We run out of boot memory\n");

	return v;
}

//
// Given pgdir, a pointer to a page directory,
// walk the 2-level page table structure to find
// the page table entry (PTE) for linear address la.
// Return a pointer to this PTE.
//
// If the relevant page table doesn't exist in the page directory:
//	- If create == 0, return 0.
//	- Otherwise allocate a new page table, install it into pgdir,
//	  and return a pointer into it.
//        (Questions: What data should the new page table contain?
//	  And what permissions should the new pgdir entry have?
//	  Note that we use the 486-only "WP" feature of %cr0, which
//	  affects the way supervisor-mode writes are checked.)
//
// This function abstracts away the 2-level nature of
// the page directory by allocating new page tables
// as needed.
// 
// boot_pgdir_walk may ONLY be used during initialization,
// before the page_free_list has been set up.
// It should panic on failure.  (Note that boot_alloc already panics
// on failure.)
//
static pte_t*
boot_pgdir_walk(pde_t *pgdir, uintptr_t la, int create)
{
	pde_t *pde = &pgdir[PDX(la)];
	pte_t *pte;

	if (*pde & PTE_P)
		return (pte_t *)KADDR(PTE_ADDR(*pde));
	else
		if (create) {
			pte = boot_alloc(PGSIZE, PGSIZE);
			memset(pte, 0, PGSIZE);

			// enalbe all permissions, to let next level page table
			// control permission accordingly.
			*pde = (physaddr_t) PADDR(pte)|PTE_W|PTE_U|PTE_P;
			return pte;
		}

	return 0;
}

//
// Map [la, la+size) of linear address space to physical [pa, pa+size)
// in the page table rooted at pgdir.  Size is a multiple of PGSIZE.
// Use permission bits perm|PTE_P for the entries.
//
// This function may ONLY be used during initialization,
// before the page_free_list has been set up.
//
static void
boot_map_segment(pde_t *pgdir, uintptr_t la, size_t size, physaddr_t pa, int perm)
{
	pte_t *pt;
	int remain;

	int first = 1;

	// size should be at PGSIZE boundary
	assert(PGOFF(size) == 0);

	for (; size; la += PTSIZE) {
		int i;
		int entries = (PPN(size) > NPTENTRIES) ? NPTENTRIES : PPN(size);
		int end = PTX(la) + entries;

		// If la is not at PTSIZE boundary, this may happen
		if (first)
			if (end > PPN(PDX(la) + PTSIZE))
				entries -= end - PPN(PDX(la) + PTSIZE);

		// Never fault; if we ran out of boot memory,
		// it'll panic at boot_alloc()
		pt = boot_pgdir_walk(pgdir, la, 1);

		for (i = PTX(la); i < end; i++, pa += PGSIZE)
			pt[i] = pa|perm|PTE_P;

		size -= entries * PGSIZE;

		// la may not be at PTSIZE boundary at first time, 
		// but it should be at next interation
		if (first) {
			la = ROUNDDOWN(la, PTSIZE);
			first = 0;
		}
	}
}

// Set up a two-level page table:
//    boot_pgdir is its linear (virtual) address of the root
//    boot_cr3 is the physical adresss of the root
// Then turn on paging.  Then effectively turn off segmentation.
// (i.e., the segment base addrs are set to zero).
// 
// This function only sets up the kernel part of the address space
// (ie. addresses >= UTOP).  The user part of the address space
// will be setup later.
//
// From UTOP to ULIM, the user is allowed to read but not write.
// Above ULIM the user cannot read (or write). 
void
i386_vm_init(void)
{
	pde_t* pgdir;
	int i;
	physaddr_t paddr;

	int page_array_size, env_array_size;
	uint32_t cr0, cr4;
	size_t n;

	//////////////////////////////////////////////////////////////////////
	// create initial page directory.
	pgdir = boot_alloc(PGSIZE, PGSIZE);
	memset(pgdir, 0, PGSIZE);
	boot_pgdir = pgdir;
	boot_cr3 = PADDR(pgdir);

	//////////////////////////////////////////////////////////////////////
	// Recursively insert PD in itself as a page table, to form
	// a virtual page table at virtual address VPT.
	// (For now, you don't have understand the greater purpose of the
	// following two lines.)

	// Permissions: kernel RW, user NONE
	pgdir[PDX(VPT)] = PADDR(pgdir)|PTE_W|PTE_P;

	// same for UVPT
	// Permissions: kernel R, user R 
	pgdir[PDX(UVPT)] = PADDR(pgdir)|PTE_U|PTE_P;

	//////////////////////////////////////////////////////////////////////
	// Map the kernel stack (symbol name "bootstack").  The complete VA
	// range of the stack, [KSTACKTOP-PTSIZE, KSTACKTOP), breaks into two
	// pieces:
	//     * [KSTACKTOP-KSTKSIZE, KSTACKTOP) -- backed by physical memory
	//     * [KSTACKTOP-PTSIZE, KSTACKTOP-KSTKSIZE) -- not backed => faults
	//     Permissions: kernel RW, user NONE
	boot_map_segment(pgdir, KSTACKTOP-KSTKSIZE, KSTKSIZE, 
			PADDR(bootstack), PTE_W);

	//////////////////////////////////////////////////////////////////////
	// Map all of physical memory at KERNBASE. 
	// Ie.  the VA range [KERNBASE, 2^32) should map to
	//      the PA range [0, 2^32 - KERNBASE)
	// We might not have 2^32 - KERNBASE bytes of physical memory, but
	// we just set up the mapping anyway.
	// Permissions: kernel RW, user NONE
	// We use a 4MB superpage to save memory.
	// Super page is valid when 
	//   1) PAE(Page Address Extension) in CR4 is disabled,
	//   2) PSE(Page Size Extension) in CR4 is enabled.
	paddr = 0x0;
	for (i = PDX(KERNBASE); i <= PDX(MAXADDR); i++, paddr += PTSIZE)
		pgdir[i] = paddr|PTE_W|PTE_P|PTE_PS;

	//////////////////////////////////////////////////////////////////////
	// Make 'pages' point to an array of size 'npage' of 'struct Page'.
	// The kernel uses this structure to keep track of physical pages;
	// 'npage' equals the number of physical pages in memory.  User-level
	// programs get read-only access to the array as well.
	// You must allocate the array yourself.
	// Map this array read-only by the user at linear address UPAGES
	// (ie. perm = PTE_U | PTE_P)
	// Permissions:
	//    - pages -- kernel RW, user NONE
	//    - the read-only version mapped at UPAGES -- kernel R, user R
	page_array_size = ROUNDUP(sizeof(struct Page) * npage, PGSIZE);
	pages = boot_alloc(page_array_size, PGSIZE);

	// Initially, mark every page as allocated, everything that should be freed
	// would be freed at page_init();
	memset(pages, 0xff, sizeof(struct Page) * npage);

	boot_map_segment(pgdir, UPAGES, page_array_size, PADDR(pages), PTE_U);

	//////////////////////////////////////////////////////////////////////
	// Make 'envs' point to an array of size 'NENV' of 'struct Env'.
	// Map this array read-only by the user at linear address UENVS
	// (ie. perm = PTE_U | PTE_P).
	// Permissions:
	//    - envs itself -- kernel RW, user NONE
	//    - the image of envs mapped at UENVS  -- kernel R, user R
	env_array_size = ROUNDUP(sizeof(struct Env) * NENV, PGSIZE);
	envs = boot_alloc(env_array_size, PGSIZE);
	memset(envs, 0x0, sizeof(struct Env) * NENV);
	boot_map_segment(pgdir, UENVS, env_array_size, PADDR(envs), PTE_U);

	// Check that the initial page directory has been set up correctly.
	check_boot_pgdir();

	//////////////////////////////////////////////////////////////////////
	// On x86, segmentation maps a VA to a LA (linear addr) and
	// paging maps the LA to a PA.  I.e. VA => LA => PA.  If paging is
	// turned off the LA is used as the PA.  Note: there is no way to
	// turn off segmentation.  The closest thing is to set the base
	// address to 0, so the VA => LA mapping is the identity.

	// Current mapping: VA KERNBASE+x => PA x.
	//     (segmentation base=-KERNBASE and paging is off)

	// From here on down we must maintain this VA KERNBASE + x => PA x
	// mapping, even though we are turning on paging and reconfiguring
	// segmentation.

	// Map VA 0:4MB same as VA KERNBASE, i.e. to PA 0:4MB.
	// (Limits our kernel to <4MB)
	pgdir[0] = pgdir[PDX(KERNBASE)];

	// Install page table.
	lcr3(boot_cr3);

	// Enable PSE.
	cr4 = rcr4();
	cr4 |= CR4_PSE;
	lcr4(cr4);

	// Turn on paging.
	cr0 = rcr0();
	cr0 |= CR0_PE|CR0_PG|CR0_AM|CR0_WP|CR0_NE|CR0_TS|CR0_EM|CR0_MP;
	cr0 &= ~(CR0_TS|CR0_EM);
	lcr0(cr0);

	// Current mapping: KERNBASE+x => x => x.
	// (x < 4MB so uses paging pgdir[0])

	// Reload all segment registers.
	asm volatile("lgdt gdt_pd");
	asm volatile("movw %%ax,%%gs" :: "a" (GD_UD|3));
	asm volatile("movw %%ax,%%fs" :: "a" (GD_UD|3));
	asm volatile("movw %%ax,%%es" :: "a" (GD_KD));
	asm volatile("movw %%ax,%%ds" :: "a" (GD_KD));
	asm volatile("movw %%ax,%%ss" :: "a" (GD_KD));
	asm volatile("ljmp %0,$1f\n 1:\n" :: "i" (GD_KT));  // reload cs
	asm volatile("lldt %%ax" :: "a" (0));

	// Final mapping: KERNBASE+x => KERNBASE+x => x.

	// This mapping was only used after paging was turned on but
	// before the segment registers were reloaded.
	pgdir[0] = 0;

	// Flush the TLB for good measure, to kill the pgdir[0] mapping.
	lcr3(boot_cr3);
}

//
// Checks that the kernel part of virtual address space
// has been setup roughly correctly(by i386_vm_init()).
//
// This function doesn't test every corner case,
// in fact it doesn't test the permission bits at all,
// but it is a pretty good sanity check. 
//
static physaddr_t check_va2pa(pde_t *pgdir, uintptr_t va);

static void
check_boot_pgdir(void)
{
	uint32_t i, n;
	pde_t *pgdir;

	pgdir = boot_pgdir;

	// check pages array
	n = ROUNDUP(npage*sizeof(struct Page), PGSIZE);
	for (i = 0; i < n; i += PGSIZE)
		assert(check_va2pa(pgdir, UPAGES + i) == PADDR(pages) + i);
	
	// check envs array (new test for lab 3)
	n = ROUNDUP(NENV*sizeof(struct Env), PGSIZE);
	for (i = 0; i < n; i += PGSIZE)
		assert(check_va2pa(pgdir, UENVS + i) == PADDR(envs) + i);

	// check phys mem
	for (i = 0; KERNBASE + i != 0; i += PTSIZE)
		assert(check_va2pa(pgdir, KERNBASE + i) == i);

	// check kernel stack
	for (i = 0; i < KSTKSIZE; i += PGSIZE)
		assert(check_va2pa(pgdir, KSTACKTOP - KSTKSIZE + i) == PADDR(bootstack) + i);

	// check for zero/non-zero in PDEs
	for (i = 0; i < NPDENTRIES; i++) {
		switch (i) {
		case PDX(VPT):
		case PDX(UVPT):
		case PDX(KSTACKTOP-1):
		case PDX(UPAGES):
		case PDX(UENVS):
			assert(pgdir[i]);
			break;
		default:
			if (i >= PDX(KERNBASE))
				assert(pgdir[i]);
			else
				assert(pgdir[i] == 0);
			break;
		}
	}
	cprintf("check_boot_pgdir() succeeded!\n");
}

// This function returns the physical address of the page containing 'va',
// defined by the page directory 'pgdir'.  The hardware normally performs
// this functionality for us!  We define our own version to help check
// the check_boot_pgdir() function; it shouldn't be used elsewhere.

static physaddr_t
check_va2pa(pde_t *pgdir, uintptr_t va)
{
	pte_t *p;

	pgdir = &pgdir[PDX(va)];
	if (!(*pgdir & PTE_P))
		return ~0;
	else if (*pgdir & PTE_PS)
		return PTE_ADDR(*pgdir);

	p = (pte_t*) KADDR(PTE_ADDR(*pgdir));
	if (!(p[PTX(va)] & PTE_P))
		return ~0;
	return PTE_ADDR(p[PTX(va)]);
}
		
// --------------------------------------------------------------
// Tracking of physical pages.
// The 'pages' array has one 'struct Page' entry per physical page.
// Pages are reference counted, and free pages are kept on a linked list.
// --------------------------------------------------------------

//  
// Initialize page structure and memory free list.
// After this point, ONLY use the functions below
// to allocate and deallocate physical memory via the page_free_list,
// and NEVER use boot_alloc() or the related boot-time functions above.
//
void
page_init(void)
{
	// The example code here marks all pages as free.
	// However this is not truly the case.  What memory is free?
	//  1) Mark page 0 as in use.
	//     This way we preserve the real-mode IDT and BIOS structures
	//     in case we ever need them.  (Currently we don't, but...)
	//  2) Mark the rest of base memory as free.
	//  3) Then comes the IO hole [IOPHYSMEM, EXTPHYSMEM).
	//     Mark it as in use so that it can never be allocated.      
	//  4) Then extended memory [EXTPHYSMEM, ...).
	//     Some of it is in use, some is free. Where is the kernel?
	//     Which pages are used for page tables and other data structures?
	//
	// Change the code to reflect this.
	int i, npages = 0;
	struct Page *pp = &pages[0];
	for (i = 0; i < MAX_ORDER; i++)
		LIST_INIT(&page_free_list[i]);

#ifdef KDEBUG
	k_debug_msg_off();
#endif

	// base useable memory
	for (i = 1; i < PPN(basemem); i++) {
		pages[i].pp_ref = 0;
		page_free(&pages[i]);
		npages++;
	}

        //
        //    Current Physical Memory Layout:
        //
        //    | start of useable mem  |
        //    +-----------------------+ <-- boot_freemem
        //    |                       |
        //    |      mempry pool      |                
        //    |     for boot_alloc    |
        //    +-----------------------+ <-- end
        //    |        .bss           |
        //    +-----------------------+ <-- edata
        //    |        .data          |
        //    +-----------------------+
        //    |        .stabstr       |
        //    +-----------------------+ <-- __STABSTR_BEGIN__
        //    |        .stab          |
        //    +-----------------------+ <-- __STAB_BEGIN__
        //    |        .text          |
        //    +-----------------------+ <-- EXTPHYSMEM (eXtended memory)

	boot_freemem = ROUNDUP(boot_freemem, PGSIZE);
	for (i = PPN(PADDR(boot_freemem)); i < PPN(maxpa); i++) {
		pages[i].pp_ref = 0;
		page_free(&pages[i]);
		npages++;
	}

#ifdef KDEBUG
	k_debug_msg_on();
#endif
	cprintf("Total usable memory: %d KB\n", npages * PGSIZE / 1024);
}

//
// Initialize a Page structure.
// The result has null links and 0 refcount.
// Note that the corresponding physical page is NOT initialized!
//
static void
page_initpp(struct Page *pp)
{
	memset(pp, 0, sizeof(*pp));
}

//
// Allocates contiguous physical pages.
// Does NOT set the contents of the physical page to zero -
// the caller must do that if necessary.
//
// *pp_store -- is set to point to the Page struct of the newly allocated
// page
//
// RETURNS 
//   0 -- on success
//   -E_NO_MEM -- otherwise 
//
// Hint: use LIST_FIRST, LIST_REMOVE, and page_initpp
// Hint: pp_ref should not be incremented 
int
pages_alloc(struct Page **pp_store, int order)
{
	int i;
	int cur_order = order;

	assert(order <= MAX_ORDER);
	assert(pp_store != 0);
	DBG(C_MEM_ALLOC, KDEBUG_FLOW, " ---- allocating, order %d ----\n", order);

	*pp_store = LIST_FIRST(&page_free_list[order++]);
	if (!*pp_store) 
		// We ran out of object of current order, borrow one from next order
		while (order <= MAX_ORDER)
		{
			DBG(C_MEM_ALLOC, KDEBUG_VERBOSE, 
				"borrow one object from order %d\n", order);

			*pp_store = LIST_FIRST(&page_free_list[order]);
			if (!*pp_store) {
				order++;
				continue;
			}

			// We borrowed one object from bigger order, but we don't need 
			// such big an object. Split the big one, and insert a half onto
			// current order. Loop until the order we requested is reached.
			while (cur_order < order)
			{
				struct Page *buddy;		

				LIST_REMOVE(*pp_store, pp_link);
				order--;
				buddy = BUDDY_OF(*pp_store, order);
				LIST_INSERT_HEAD(&page_free_list[order], buddy, pp_link);
			}
			break;
		}
	else 
		LIST_REMOVE(*pp_store, pp_link);

	if (!*pp_store)
		return -E_NO_MEM;

	for (i = 0; i < (1 << cur_order); i++) {
		page_initpp(*pp_store + i);
		PAGE_MARK_ALLOC(*pp_store + i);
	}

	return 0;
}

//
// Return contiguous pages to the free list.
// (This function should only be called when pp->pp_ref reaches 0.)
//
void
pages_free(struct Page *pp, int order)
{
	int i;
	struct Page *buddy;

	assert(pp->pp_ref == 0);

	// sanity check:
	// 1. PPN of freed page should be algined on (1 << order) boundary
	// 2. each page should already be allocated 
	assert((page2ppn(pp) & ((1 << order) - 1)) == 0);
	for (i = 0; i < (1 << order); i++) {
		assert(PAGE_ALLOCATED(pp + i));
		PAGE_MARK_FREE(pp + i);
	}

	assert(order <= MAX_ORDER);
	DBG(C_MEM_ALLOC, KDEBUG_FLOW, " ---- freeing ppn %x, order %d ----\n", 
		page2ppn(pp), order);

	while (order < MAX_ORDER)
	{
		buddy = BUDDY_OF(pp, order);
		
		// try to merge if we can
		for (i = 0; i < (1 << order); i++) 
			if (PAGE_ALLOCATED(buddy + i))
				goto cannot_merge;

		// we can merge the buddies
		order++;
		DBG(C_MEM_ALLOC, KDEBUG_VERBOSE, "merging ppn %x and its buddy %x\n", 
			page2ppn(pp), page2ppn(buddy));
		LIST_REMOVE(buddy, pp_link);
		pp = (pp > buddy) ? buddy : pp;
	}
cannot_merge:
	DBG(C_MEM_ALLOC, KDEBUG_VERBOSE, "cannot merge, insert to order %d\n", order);
	LIST_INSERT_HEAD(&page_free_list[order], pp, pp_link);
}

inline int 
get_order(unsigned long size)
{
	int order;

	size = (size-1) >> (PGSHIFT-1);
	order = -1;
	do {
		size >>= 1;
		order++;
	} while (size);
	return order;
}

void
buddy_info(void)
{
	int order = 0;
	int npages = 0;

	while (order <= MAX_ORDER)
	{
		int num = 0;
		struct Page *pp= LIST_FIRST(&page_free_list[order]);
		cprintf("Number of free pages on order %02d: ", order);

		while (pp) {
			num++;
			pp = LIST_NEXT(pp, pp_link);
		}
		cprintf("%d\n", num);
		npages += ((1 << order) * num);
		order++;
	}
	cprintf("Avalible memory: %d KB\n", npages * PGSIZE / 1024);
}

//
// Decrement the reference count on a page,
// freeing it if there are no more refs.
//
void
page_decref(struct Page* pp)
{
	if (--pp->pp_ref == 0)
		page_free(pp);
}

// Given 'pgdir', a pointer to a page directory, pgdir_walk returns
// a pointer to the page table entry (PTE) for linear address 'va'.
// This requires walking the two-level page table structure.
//
// If the relevant page table doesn't exist in the page directory, then:
//    - If create == 0, pgdir_walk returns NULL.
//    - Otherwise, pgdir_walk tries to allocate a new page table
//	with page_alloc.  If this fails, pgdir_walk returns NULL.
//    - Otherwise, pgdir_walk returns a pointer into the new page table.
//    - If the request address is in Remapped Physical Memory
//      (addr > KERNBASE, which has PS flag set to reduce memory consumption)
//      return corresponding page directory entry.
//
// This is boot_pgdir_walk, but using page_alloc() instead of boot_alloc().
// Unlike boot_pgdir_walk, pgdir_walk can fail.
//
// Hint: you can turn a Page * into the physical address of the
// page it refers to with page2pa() from kern/pmap.h.
//
// Hint 2: the x86 MMU checks permission bits in both the page directory
// and the page table, so it's safe to leave permissions in the page
// more permissive than strictly necessary.
pte_t *
pgdir_walk(pde_t *pgdir, const void *va, int create)
{
	pde_t *pde = &pgdir[PDX(va)];
	struct Page *new;

	// If PTE_PS is used for physical memory remapping, the vaddr
	// larger than KERNBASE should be already mapped, return
	// pde entry the address belongs to.
	if ((uintptr_t)va >= KERNBASE) {
		assert(*pde & PTE_PS);
		return (pte_t *)pde;
	}

	if (*pde & PTE_P)
		return (pte_t *)KADDR(PTE_ADDR(*pde)) + PTX(va);
	else
		if (create) {
			physaddr_t p;

			if (page_alloc(&new) == -E_NO_MEM)
				goto pgdir_walk_fail;

			DBG(C_VM, KDEBUG_FLOW,
				"create new page table(ppn: 0x%x) at va 0x%08x [%x]\n",
				page2ppn(new), PDX(va) * PTSIZE, PADDR(pgdir));
			memset(page2kva(new), 0, PGSIZE);

			new->pp_ref++;

			// enalbe all permissions, to let next level page table
			// control permission accordingly.
			*pde = (p = page2pa(new))|PTE_U|PTE_W|PTE_P;
			return (pte_t *)KADDR(p) + PTX(va);
		}

pgdir_walk_fail:
	return NULL;
}

//
// Map the physical page 'pp' at virtual address 'va'.
// The permissions (the low 12 bits) of the page table
//  entry should be set to 'perm|PTE_P'.
//
// Details
//   - If there is already a page mapped at 'va', it is page_remove()d.
//   - If necessary, on demand, allocates a page table and inserts it into
//     'pgdir'.
//   - pp->pp_ref should be incremented if the insertion succeeds.
//   - The TLB must be invalidated if a page was formerly present at 'va'.
//
// RETURNS: 
//   0 on success
//   -E_NO_MEM, if page table couldn't be allocated
//   -E_INVAL, if invalid argument given
//
// Hint: The TA solution is implemented using pgdir_walk, page_remove,
// and page2pa.
//
int
page_insert(pde_t *pgdir, struct Page *pp, void *va, int perm) 
{
	int ret = 0;
	pte_t *pte;

	// If PTE_PS is used for physical memory remapping, this function
	// can not be called with va >= KERNBASE, since two level page
	// table is assumed, and those mappings should not be modified.
	assert((uintptr_t)va < KERNBASE);

	// TODO: check perm flag
	if (pp == NULL)
		return -E_INVAL;

	// get the page, to prevent it to be freed at
	// next page_remove() call
	pp->pp_ref++;

	// remove previous mapping, if exist
	page_remove(pgdir, va);

	pp->pp_ref--;

	DBG(C_VM, KDEBUG_FLOW,
		"insert a page(ppn: 0x%x) onto va 0x%08x [%x]\n",
		page2ppn(pp), va, PADDR(pgdir));

	if ( (pte = pgdir_walk(pgdir, va, 1))) {

		pp->pp_ref++;
		*pte = page2pa(pp)|perm|PTE_P;
	} else
		// pgdir_walk failed when page_alloc() returned -E_NO_MEM
		ret = -E_NO_MEM;

	return ret;
}

//
// Return the page mapped at virtual address 'va'.
// If pte_store is not zero, then we store in it the address
// of the pte for this page.  This is used by page_remove
// but should not be used by other callers.
//
// Return 0 if there is no page mapped at va.
//
// Hint: the TA solution uses pgdir_walk and pa2page.
//
struct Page *
page_lookup(pde_t *pgdir, void *va, pte_t **pte_store)
{
	pte_t *pte;

	// for kernal address, use kva2page() instead
	assert((uintptr_t)va < KERNBASE);

	if ( (pte = pgdir_walk(pgdir, va, 0))) {

		if (pte_store)
			*pte_store = pte;

		if (*pte & PTE_P) 
			return pa2page(PTE_ADDR(*pte));
		else
			return NULL;
	}

	// current va doesn't have a page table
	return NULL;
}

//
// Unmaps the physical page at virtual address 'va'.
// If there is no physical page at that address, silently does nothing.
//
// Details:
//   - The ref count on the physical page should decrement.
//   - The physical page should be freed if the refcount reaches 0.
//   - The pg table entry corresponding to 'va' should be set to 0.
//     (if such a PTE exists)
//   - The TLB must be invalidated if you remove an entry from
//     the pg dir/pg table.
//
// Hint: The TA solution is implemented using page_lookup,
// 	tlb_invalidate, and page_decref.
//
void
page_remove(pde_t *pgdir, void *va)
{
	struct Page *target;
	pte_t *pte;

	target = page_lookup(pgdir, va, &pte);
	if (target) {
		DBG(C_VM, KDEBUG_FLOW,
			"remove a page(ppn: 0x%x) from va 0x%08x [%x]\n",
			page2ppn(target), va, PADDR(pgdir));
		page_decref(target);
		if (*pte)
			*pte = 0; // clear the mapping
		tlb_invalidate(pgdir, va);
	}

	//current va is not backed with a page, do nothing
}

//
// Map [la, la+size) of linear address space to physical [pa, pa+size)
// in the page table rooted at pgdir. Previous mapping will be removed.
// Use permission bits perm|PTE_P for the entries.
//
int
page_map_segment(pde_t *pgdir, struct Page *pp, void *va, size_t size, int perm)
{
	va = ROUNDDOWN(va, PGSIZE);
	size = ROUNDUP(size, PGSIZE);

	int npages = size / PGSIZE;
	int ret;

	while (npages) {
		assert(PAGE_ALLOCATED(pp));

		if ( (ret = page_insert(pgdir, pp, va, perm)) < 0)
			return ret;
		pp++;
		va += PGSIZE;
		npages--;
	}

	return 0;
}

//
// Invalidate a TLB entry, but only if the page tables being
// edited are the ones currently in use by the processor.
//
void
tlb_invalidate(pde_t *pgdir, void *va)
{
	// Flush the entry only if we're modifying the current address space.
	if (!curenv || curenv->env_pgdir == pgdir)
		invlpg(va);
}

static uintptr_t user_mem_check_addr;

//
// Check that an environment is allowed to access the range of memory
// [va, va+len) with permissions 'perm | PTE_P'.
// Normally 'perm' will contain PTE_U at least, but this is not required.
// 'va' and 'len' need not be page-aligned; you must test every page that
// contains any of that range.  You will test either 'len/PGSIZE',
// 'len/PGSIZE + 1', or 'len/PGSIZE + 2' pages.
//
// A user program can access a virtual address if (1) the address is below
// ULIM, and (2) the page table gives it permission.  These are exactly
// the tests you should implement here.
//
// If there is an error, set the 'user_mem_check_addr' variable to the first
// erroneous virtual address.
//
// Returns 0 if the user program can access this range of addresses,
// and -E_FAULT otherwise.
//
// Hint: The TA solution uses pgdir_walk.
//
int
user_mem_check(struct Env *env, const void *va, size_t len, int perm)
{
	const void *end = va + len;

	if ((uintptr_t)va >= ULIM)
		goto check_failed;

	end = ROUNDUP(end, PGSIZE);

	while (va < end) {
		pte_t *p;

		if ( !(p = pgdir_walk(curenv->env_pgdir, va, 0)) ||
			(*p & (perm|PTE_P)) != (perm|PTE_P))
			goto check_failed;
		va += PGSIZE;
	}

	return 0;

check_failed:
	user_mem_check_addr = (uintptr_t) va;
	return -E_FAULT;
}

//
// Checks that environment 'env' is allowed to access the range
// of memory [va, va+len) with permissions 'perm | PTE_U'.
// If it can, then the function simply returns.
// If it cannot, 'env' is destroyed.
//
void
user_mem_assert(struct Env *env, const void *va, size_t len, int perm)
{
	if (user_mem_check(env, va, len, perm | PTE_U) < 0) {
		cprintf("[%08x] user_mem_check assertion failure for "
			"va %08x\n", curenv->env_id, user_mem_check_addr);
		env_destroy(env);	// may not return
	}
}

//
// print the content of given page table/directory entry
// retrun 1 if associated page/page table is present
//
static int
print_entry(uintptr_t va, uint32_t entry, int pde)
{
	uint32_t user;
	int ps = 0;

	if (entry & PTE_P) {

		if (!pde)
			cprintf("  * ");

		else if (entry & PTE_PS)
			ps = 1;

		user = entry & PTE_U;

		cprintf("VA:0x%08x -> PA:0x%08x: %c%c/%c%c %s\n",
				va, PTE_ADDR(entry),
				'R', entry & PTE_W ? 'W' : '-',
				user ? 'R' : '-',
				user ? (entry & PTE_W) ? 'W' : '-' :  '-',
				ps ? "PS" : "");

		if (ps)
			return 0;

		return 1;
	}
	else 
		return 0;
}

// 
// Dump the virtual memory mapping for the region [from, to]
// `from' and `to' are round-down to PGSIZE boundary.
// To simplify the case, if `to' is located at PTSIZE boundary,
// we change dumping region to [from, to)
//
// It the given PD/PT entry is not present, it'll be ignored.
//
void
dump_mapping(uintptr_t from, uintptr_t to)
{
	int remain, offset, i;
	pde_t *pgdir = (pde_t *) KADDR(rcr3());

	// finding the offset in first page directory,
	// and the remaining entries in last page directory
	offset = from - ROUNDDOWN(from, PTSIZE);
	from -= offset; 

	offset = PTX(offset);
	remain = PTX((to - from) % PTSIZE);

	to = ROUNDUP(to, PTSIZE);
	if (to == 0)
		to = MAXADDR;
	
	for (;; from += PTSIZE)
	{
		int last = 0;
		if (from + PTSIZE == to || from + PTSIZE == 0)
			last = 1;

		pde_t *pde = &pgdir[PDX(from)];

		if (print_entry(from, (uint32_t)*pde, 1)) 
		{
			// if remain == 0, `to' is located at PTSIZE boundary
			// otherwise use `remain + 1' in order to print 
			// last entry we specified
			uint16_t entries = last ? 
				(remain ? remain + 1 : NPTENTRIES) : NPTENTRIES;

			pte_t *pte = KADDR(PTE_ADDR(*pde));

			for (i = offset; i < entries; i++)
				print_entry(from + i * PGSIZE, (uint32_t)pte[i], 0);
			offset = 0;
		}

		if (last) break;
	}
}

// print memory content of virtual memory start from vaddr, 
// when print_phys == 1, print physical label instead of
// virtual one.
//
// For example 
// dump_content(KERNBASE, 1, 0x100, 4);
// should print:
// 0x00000000: 0xXXXXXXXX 0xXXXXXXXX 0xXXXXXXXX 0xXXXXXXXX
// 0x00000010: ...
//
// dump_content(KERNBASE, 0 ,0x100, 2);
// should print:
// 0xf0000000: 0xXXXX 0xXXXX 0xXXXX 0xXXXX 0xXXXX 0xXXXX 0xXXXX 0xXXXX
// 0xf0000010: ...
//
// vaddr should be page-aligned
// word can be 1, 2, 4

static void
dump_content(uintptr_t vaddr, int print_phys, size_t len, size_t word)
{

	int i;
	uint32_t print_addr;

	vaddr = ROUNDDOWN(vaddr, 0x10);
	print_addr = print_phys ? PADDR(vaddr) : vaddr;

	do {
		int remain = MIN(len ,16);

		cprintf("%08x: ", print_addr);
		for (i = 0; i < remain; i += word) {
			void *p = (void *)vaddr + i;

			if (remain - i < word) break;

			switch (word) 
			{
			case 1:
				if (i == 8)
					cprintf("\n%08x: ", print_addr + 8);
				cprintf("0x%02x ", *(uint8_t *)p);
				break;
			case 2:
				cprintf("0x%04x ", *(uint16_t *)p);
				break;
			case 4:
				cprintf("0x%08x ", *(uint32_t *)p);
				break;
			default:
				panic("You give wrong word value\n");
			}
		}

		len -= remain;
		vaddr += 16;
		print_addr += 16;
		cprintf("\n");

	} while(len);

}

void
dump_virt(uintptr_t vaddr, size_t len, size_t word)
{
	pte_t *pte;
	pde_t *pgdir = KADDR(rcr3());

	while (len) {
		size_t dump_len = PGSIZE;

		pte = pgdir_walk(pgdir, (void *)vaddr, 0);
		if (pte) {
			if (*pte & PTE_PS)
				dump_len = PTSIZE;

			dump_len = MIN(dump_len, len);
			dump_content(vaddr, 0, dump_len, word);
		}
		else {
			cprintf("virtial address 0x%08x does not"
				" backed with physical memory\n", vaddr);
			dump_len = MIN(dump_len, len);
		}

		len -= dump_len;
		vaddr += dump_len;
	}
}

void
dump_phys(physaddr_t paddr, size_t len, size_t word)
{
	uintptr_t vaddr;

	if (paddr > maxpa || paddr + len > maxpa) {
		cprintf("out of memory range\n");
		return;
	}

	vaddr  = (uintptr_t) KADDR(paddr);

	dump_content(vaddr, 1, len, word);
}

void

page_check(void)
{
	struct Page *pp, *pp0, *pp1, *pp2;
	struct Page_list fl[MAX_ORDER + 1];
	struct Page *saved_pages;
	pte_t *ptep, *ptep1;
	void *va;
	int i;
	int page_array_size = sizeof(struct Page) * npage;

	// should be able to allocate three pages
	pp0 = pp1 = pp2 = 0;
	assert(page_alloc(&pp0) == 0);
	assert(page_alloc(&pp1) == 0);
	assert(page_alloc(&pp2) == 0);

	assert(pp0);
	assert(pp1 && pp1 != pp0);
	assert(pp2 && pp2 != pp1 && pp2 != pp0);

	// mark all pages allocated
	pages_alloc(&saved_pages, get_order(page_array_size));
	memmove(page2kva(saved_pages), pages, page_array_size);
	for (i = 0; i < npage; i++)
		PAGE_MARK_ALLOC(&pages[i]);

	// temporarily steal the rest of the free pages
	memmove(fl, page_free_list, sizeof(fl));
	for (i = 0; i <= MAX_ORDER; i++)
		LIST_INIT(&page_free_list[i]);

	// should be no free memory
	assert(page_alloc(&pp) == -E_NO_MEM);

	// there is no page allocated at address 0
	assert(page_lookup(boot_pgdir, (void *) 0x0, &ptep) == NULL);

	// there is no free memory, so we can't allocate a page table 
	assert(page_insert(boot_pgdir, pp1, 0x0, 0) < 0);

	// free pp0 and try again: pp0 should be used for page table
	page_free(pp0);
	assert(page_insert(boot_pgdir, pp1, 0x0, 0) == 0);
	assert(PTE_ADDR(boot_pgdir[0]) == page2pa(pp0));
	assert(check_va2pa(boot_pgdir, 0x0) == page2pa(pp1));
	assert(pp1->pp_ref == 1);
	assert(pp0->pp_ref == 1);

	// should be able to map pp2 at PGSIZE because pp0 is already allocated for page table
	assert(page_insert(boot_pgdir, pp2, (void*) PGSIZE, 0) == 0);
	assert(check_va2pa(boot_pgdir, PGSIZE) == page2pa(pp2));
	assert(pp2->pp_ref == 1);

	// should be no free memory
	assert(page_alloc(&pp) == -E_NO_MEM);

	// should be able to map pp2 at PGSIZE because it's already there
	assert(page_insert(boot_pgdir, pp2, (void*) PGSIZE, 0) == 0);
	assert(check_va2pa(boot_pgdir, PGSIZE) == page2pa(pp2));
	assert(pp2->pp_ref == 1);

	// pp2 should NOT be on the free list
	// could happen in ref counts are handled sloppily in page_insert
	assert(page_alloc(&pp) == -E_NO_MEM);

	// check that pgdir_walk returns a pointer to the pte
	ptep = KADDR(PTE_ADDR(boot_pgdir[PDX(PGSIZE)]));
	assert(pgdir_walk(boot_pgdir, (void*)PGSIZE, 0) == ptep+PTX(PGSIZE));

	// should be able to change permissions too.
	assert(page_insert(boot_pgdir, pp2, (void*) PGSIZE, PTE_U) == 0);
	assert(check_va2pa(boot_pgdir, PGSIZE) == page2pa(pp2));
	assert(pp2->pp_ref == 1);
	assert(*pgdir_walk(boot_pgdir, (void*) PGSIZE, 0) & PTE_U);
	assert(boot_pgdir[0] & PTE_U);
	
	// should not be able to map at PTSIZE because need free page for page table
	assert(page_insert(boot_pgdir, pp0, (void*) PTSIZE, 0) < 0);

	// insert pp1 at PGSIZE (replacing pp2)
	assert(page_insert(boot_pgdir, pp1, (void*) PGSIZE, 0) == 0);
	assert(!(*pgdir_walk(boot_pgdir, (void*) PGSIZE, 0) & PTE_U));

	// should have pp1 at both 0 and PGSIZE, pp2 nowhere, ...
	assert(check_va2pa(boot_pgdir, 0) == page2pa(pp1));
	assert(check_va2pa(boot_pgdir, PGSIZE) == page2pa(pp1));
	// ... and ref counts should reflect this
	assert(pp1->pp_ref == 2);
	assert(pp2->pp_ref == 0);

	// pp2 should be returned by page_alloc
	assert(page_alloc(&pp) == 0 && pp == pp2);

	// unmapping pp1 at 0 should keep pp1 at PGSIZE
	page_remove(boot_pgdir, 0x0);
	assert(check_va2pa(boot_pgdir, 0x0) == ~0);
	assert(check_va2pa(boot_pgdir, PGSIZE) == page2pa(pp1));
	assert(pp1->pp_ref == 1);
	assert(pp2->pp_ref == 0);

	// unmapping pp1 at PGSIZE should free it
	page_remove(boot_pgdir, (void*) PGSIZE);
	assert(check_va2pa(boot_pgdir, 0x0) == ~0);
	assert(check_va2pa(boot_pgdir, PGSIZE) == ~0);
	assert(pp1->pp_ref == 0);
	assert(pp2->pp_ref == 0);

	// so it should be returned by page_alloc
	assert(page_alloc(&pp) == 0 && pp == pp1);

	// should be no free memory
	assert(page_alloc(&pp) == -E_NO_MEM);
	
#if 0
	// should be able to page_insert to change a page
	// and see the new data immediately.
	memset(page2kva(pp1), 1, PGSIZE);
	memset(page2kva(pp2), 2, PGSIZE);
	page_insert(boot_pgdir, pp1, 0x0, 0);
	assert(pp1->pp_ref == 1);
	assert(*(int*)0 == 0x01010101);
	page_insert(boot_pgdir, pp2, 0x0, 0);
	assert(*(int*)0 == 0x02020202);
	assert(pp2->pp_ref == 1);
	assert(pp1->pp_ref == 0);
	page_remove(boot_pgdir, 0x0);
	assert(pp2->pp_ref == 0);
#endif

	// forcibly take pp0 back
	assert(PTE_ADDR(boot_pgdir[0]) == page2pa(pp0));
	boot_pgdir[0] = 0;
	assert(pp0->pp_ref == 1);
	pp0->pp_ref = 0;
	
	// check pointer arithmetic in pgdir_walk
	page_free(pp0);
	va = (void*)(PGSIZE * NPDENTRIES + PGSIZE);
	ptep = pgdir_walk(boot_pgdir, va, 1);
	ptep1 = KADDR(PTE_ADDR(boot_pgdir[PDX(va)]));
	assert(ptep == ptep1 + PTX(va));
	boot_pgdir[PDX(va)] = 0;
	pp0->pp_ref = 0;
	
	// check that new page tables get cleared
	memset(page2kva(pp0), 0xFF, PGSIZE);
	page_free(pp0);
	pgdir_walk(boot_pgdir, 0x0, 1);
	ptep = page2kva(pp0);
	for(i=0; i<NPTENTRIES; i++)
		assert((ptep[i] & PTE_P) == 0);
	boot_pgdir[0] = 0;
	pp0->pp_ref = 0;

	// give free list back
	memmove(page_free_list, fl, sizeof(fl));
	memmove(pages, page2kva(saved_pages), page_array_size);

	pages_free(saved_pages, get_order(page_array_size));

	// free the pages we took
	page_free(pp0);
	page_free(pp1);
	page_free(pp2);

	cprintf("page_check() succeeded!\n");
}
