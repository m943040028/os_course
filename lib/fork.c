// implement fork from user space

#include <inc/string.h>
#include <inc/lib.h>

// PTE_COW marks copy-on-write page table entries.
// It is one of the bits explicitly allocated to user processes (PTE_AVAIL).
#define PTE_COW		0x800

//
// Custom page fault handler - if faulting page is copy-on-write,
// map in our own private writable copy.
//
static void
pgfault(struct UTrapframe *utf)
{
	void *addr = (void *) utf->utf_fault_va;
	uint32_t err = utf->utf_err;
	int r;
	pte_t pte;

	// Check that the faulting access was (1) a write, and (2) to a
	// copy-on-write page.  If not, panic.
	// Hint:
	//   Use the read-only page table mappings at vpt
	//   (see <inc/memlayout.h>).
	assert(vpd[VPD(addr)] != 0x0);

	pte = vpt[VPN(addr)];

	if ( !(pte & PTE_COW))
		panic("write access to non copy-on-write page\n");

	// Allocate a new page, map it at a temporary location (PFTEMP),
	// copy the data from the old page to the new page, then move the new
	// page to the old page's address.
	// Hint:
	//   You should make three system calls.
	//   No need to explicitly delete the old page's mapping.
	
	addr = ROUNDDOWN(addr, PGSIZE);
	if ( (r = sys_page_alloc(0, PFTEMP,
		PTE_U|PTE_P|PTE_W)) < 0)
		panic("sys_page_alloc: %e\n", r);

	// copy the data from the old page
	memmove(PFTEMP, addr, PGSIZE);

	// move the new page to old page's address
	if ((r = sys_page_map(0, PFTEMP, 0,
		addr, PTE_P|PTE_U|PTE_W)) < 0)
		panic("sys_page_map: %e", r);

	// unmap the temporary location
	if ((r = sys_page_unmap(0, PFTEMP)) < 0)
		panic("sys_page_unmap: %e", r);
}

//
// Map our virtual page pn (address pn*PGSIZE) into the target envid
// at the same virtual address.  If the page is writable or copy-on-write,
// the new mapping must be created copy-on-write, and then our mapping must be
// marked copy-on-write as well.  (Exercise: Why mark ours copy-on-write again
// if it was already copy-on-write?)
//
// Returns: 0 on success, < 0 on error.
// It is also OK to panic on error.
// 
static int
duppage(envid_t envid, unsigned pn)
{
	int r;
	void *addr;
	pte_t pte;

	addr = (void *) (pn * PGSIZE);
	pte = vpt[pn];

	// Omit unnecessary fileds, 
	// PTE_P(bit0), PTE_W(bit1), PTE_U(bit2) is what we cared about
	pte &= PTE_USER;
	assert((pte & (PTE_P|PTE_U)) == (PTE_P|PTE_U));

	if (pte & PTE_COW || pte & PTE_W) {
		pte = (pte & ~PTE_W) | PTE_COW;
	}

	if ( (r = sys_page_map(0, (void *)addr, envid, 
		(void *)addr, pte)) < 0)
		panic("child: sys_page_map: %e", r);

	// also, fix our page table entry
	if ( (r = sys_page_map(0, (void *)addr, 0, 
		(void *)addr, pte)) < 0)
		panic("parent: sys_page_map: %e", r);

	return 0;
}

//
// User-level fork with copy-on-write.
// Set up our page fault handler appropriately.
// Create a child.
// Copy our address space and page fault handler setup to the child.
// Then mark the child as runnable and return.
//
// Returns: child's envid to the parent, 0 to the child, < 0 on error.
// It is also OK to panic on error.
//
// Hint:
//   Use vpd, vpt, and duppage.
//   Remember to fix "env" and the user exception stack in the child process.
//   Neither user exception stack should ever be marked copy-on-write,
//   so you must allocate a new page for the child's user exception stack.
//
envid_t
fork(void)
{
	envid_t child;
	extern unsigned char end[];
	uint8_t *addr;
	int r;

	extern void _pgfault_upcall(void);
	extern void (*_pgfault_handler)(struct UTrapframe *utf);
	//assert(_pgfault_handler == NULL);
	_pgfault_handler = pgfault;

	child = sys_exofork();
	if (child < 0)
		return (int)child;

	// common code for parent and child,
	// allocate a clean exception stack for both environments
	if ( (r = sys_page_alloc(0, (void *)UXSTACKTOP-PGSIZE,
		PTE_U|PTE_P|PTE_W)) < 0)
		panic("uxstack: sys_page_alloc: %e\n", r);

	if ( (r = sys_env_set_pgfault_upcall(0, _pgfault_upcall)) < 0)
		panic("sys_env_set_pgfault_upcall: %e\n", r);

	if (!child) {
		// We are child, update env and exit
		env = &envs[ENVX(sys_getenvid())];
		return 0;
	}

	// We are parent, dup our address space to child's using COW
	for (addr = (uint8_t *)UTEXT; addr < end; addr += PGSIZE)
		duppage(child, PPN(addr));

	// Share user stack by two environment is nonsense,
	// a page fault will occur immediately when the child
	// returns from sys_exofork.
	// So, we create a new user stack for child
	if ( (r = sys_page_alloc(child, (void *)USTACKTOP-PGSIZE,
		PTE_U|PTE_P|PTE_W)) < 0)
		panic("dupstack: sys_page_alloc: %e\n", r);

	if ( (r = sys_page_map(child, (void *)USTACKTOP-PGSIZE, 0, 
		UTEMP, PTE_P|PTE_U|PTE_W)) < 0)
		panic("dupstack: sys_page_map: %e", r);

	// dup our stack content to child's
	memmove(UTEMP, (void *)USTACKTOP-PGSIZE, PGSIZE);
	if ( (r = sys_page_unmap(0, UTEMP)) < 0)
		panic("dupstack: sys_page_unmap: %e", r);

	// Start the child environment running
	if ( (r = sys_env_set_status(child, ENV_RUNNABLE)) < 0)
		panic("sys_env_set_status: %e\n", r);

	sys_yield();

	return child;
}

// Challenge!
int
sfork(void)
{
	panic("sfork not implemented");
	return -E_INVAL;
}
