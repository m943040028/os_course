// The page corresponding to 0xDEADBEEF is not present, SYS_CPUTS refuses
// such operation. On the other hand,
// cprintf() will not printf the content at given address(0xDEADBEEF), 
// instead, it copys the content of given address to a local buffer
// (see vcprintf() in lib/printf.c), then print the buffer using SYS_CPUTS

#include <inc/lib.h>

void
handler(struct UTrapframe *utf)
{
	int r;
	void *addr = (void*)utf->utf_fault_va;

	cprintf("fault %x\n", addr);
	if ((r = sys_page_alloc(0, ROUNDDOWN(addr, PGSIZE),
				PTE_P|PTE_U|PTE_W)) < 0)
		panic("allocating at %x in page fault handler: %e", addr, r);
	snprintf((char*) addr, 100, "this string was faulted in at %x", addr);
}

void
umain(void)
{
	set_pgfault_handler(handler);
	sys_cputs((char*)0xDEADBEEF, 4);
}
