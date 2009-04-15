// Simple command-line kernel monitor useful for
// controlling the kernel and exploring the system interactively.

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/memlayout.h>
#include <inc/assert.h>
#include <inc/error.h>
#include <inc/x86.h>

#include <kern/console.h>
#include <kern/monitor.h>
#include <kern/trap.h>
#include <kern/kdebug.h>
#include <kern/pmap.h>
#include <kern/env.h>

#define CMDBUF_SIZE	80	// enough for one VGA text line

// TODO:
//   (1) Step an instruction
//   (2) Continue execution
//   (3) Online kernel debugger, in assemble level
//   (4) Shortcut for each command

struct Command {
	const char *name;
	const char *desc;
	// return -1 to force monitor to exit
	int (*func)(int argc, char** argv, struct Trapframe* tf);
};

static struct Command commands[] = {
	{ "help", "Display this list of commands", mon_help },
	{ "kerninfo", "Display information about the kernel", mon_kerninfo },
	{ "backtrace", "Traceback the call stack", mon_backtrace },
	{ "showmapping", "Dump virtual memory mapping", mon_showmapping },
	{ "switch", "Switch address space", mon_switch },
	{ "allocpage", "Allocating memory", mon_allocpage },
	{ "freepage", "Freeing memory", mon_freepage },
	{ "dumpva", "Dump virtual memory contents", mon_dumpva },
	{ "dumppa", "Dump physical memory contents", mon_dumppa },
	{ "buddyinfo", "Free memory information", mon_buddyinfo },
};
#define NCOMMANDS (sizeof(commands)/sizeof(commands[0]))

unsigned read_eip();

/***** Implementations of basic kernel monitor commands *****/

int
mon_help(int argc, char **argv, struct Trapframe *tf)
{
	int i;

	for (i = 0; i < NCOMMANDS; i++)
		cprintf("%s - %s\n", commands[i].name, commands[i].desc);
	return 0;
}

int
mon_kerninfo(int argc, char **argv, struct Trapframe *tf)
{
	extern char _start[], etext[], edata[], end[];

	cprintf("Special kernel symbols:\n");
	cprintf("  _start %08x (virt)  %08x (phys)\n", _start, _start - KERNBASE);
	cprintf("  etext  %08x (virt)  %08x (phys)\n", etext, etext - KERNBASE);
	cprintf("  edata  %08x (virt)  %08x (phys)\n", edata, edata - KERNBASE);
	cprintf("  end    %08x (virt)  %08x (phys)\n", end, end - KERNBASE);
	cprintf("Kernel executable memory footprint: %dKB\n",
		(end-_start+1023)/1024);
	return 0;
}

// Call stack overview on i386 architecture:
//
//	|		| <-- Last EBP
//	~~~~~~~~~~~~~~~~~
//	:	.	:
//	:	.	:
//	~~~~~~~~~~~~~~~~~
//	| Arg 1		|
//	+---------------+
//	| Arg 0		|
//	+---------------+
//	| Return addr	|
//	+---------------+
//	| Last EBP	| <-- current EBP
//	+---------------+
//	| local var 0	|
//	+---------------+
//	| local var 1 	| <-- current ESP
//
//
int
mon_backtrace(int argc, char **argv, struct Trapframe *tf)
{
	uint32_t *ebp = (uint32_t *)read_ebp();
	uint32_t current_pc = read_eip();

	// We stop at top level function.
	// EBP is set to zero at the kernel entry point.
	cprintf("Stack Backtace:\n");
	while ((uint32_t) ebp)
	{
		int i;
		struct Eipdebuginfo info;
		char name[40];

		if (debuginfo_eip(current_pc, &info) < 0)
			panic("debuginfo_eip failed\n");

		strncpy(name, info.eip_fn_name, info.eip_fn_namelen);
		name[info.eip_fn_namelen] = '\0';
		cprintf("%s:%d: %s+%d\n", 
			info.eip_file,
			info.eip_line,
			name,
			current_pc - info.eip_fn_addr
			);
		cprintf("ebp %08x eip %08x args %s", ebp, current_pc,
			info.eip_fn_narg ? "" : "(none)");
		for (i = 0; i < info.eip_fn_narg; i++) 
			cprintf("%08x ", *(ebp + 2 + i));
		cprintf("\n");

		// get last pc
		current_pc = *(ebp + 1) - 4;

		// get last ebp
		ebp = (uint32_t *) *ebp;
	}

	return 0;
}

int
mon_showmapping(int argc, char **argv, struct Trapframe *tf)
{
	if (argc != 3) {
		cprintf("usage: %s <from> <to>\n", argv[0]);
		return 0;
	}

	uintptr_t addr[3];
	int i;

	for (i = 1; i < 3; i++)
		addr[i] = (uintptr_t) strtol(argv[i], NULL, 0);

	if (addr[2] < addr[1]) return 0;

	dump_mapping(addr[1], addr[2]);

	return 0;
}

int mon_allocpage(int argc, char **argv, struct Trapframe *tf)
{
	struct Page *pp;
	int order;
	if (argc != 2) {
		cprintf("usage: %s <order>\n", argv[0]);
		return 0;
	}

	order = strtol(argv[1], NULL, 0);
	
	if (-E_NO_MEM == pages_alloc(&pp, order)) {
		cprintf("allocting failed\n");
		return 0;
	}
	cprintf("kvaddr: %x, ppn: %x, order: %d\n", 
		page2kva(pp), page2ppn(pp), order);

	return 0;
}

int mon_freepage(int argc, char **argv, struct Trapframe *tf)
{
	struct Page *pp;
	int vaddr, order;

	if (argc != 3) {
		cprintf("usage: %s <kaddr> <order>\n", argv[0]);
		return 0;
	}

	vaddr = strtol(argv[1], NULL, 0);
	order = strtol(argv[2], NULL, 0);

	if (vaddr < KERNBASE) {
		cprintf("you give wrong address\n");
		return 0;
	}

	pp = pa2page(PADDR(vaddr));
	pages_free(pp, order);
	return 0;
}

int mon_dumpva(int argc, char **argv, struct Trapframe *tf)
{
	uintptr_t vaddr;
	size_t len;
	size_t word;

	if (argc != 4) {
		cprintf("usage: %s <vaddr> <len> <word>\n", argv[0]);
		return 0;
	}

	vaddr = strtol(argv[1], NULL, 0);
	len = strtol(argv[2], NULL, 0);
	word = strtol(argv[3], NULL, 0);

	dump_virt(vaddr, len, word);

	return 0;
}

int mon_dumppa(int argc, char **argv, struct Trapframe *tf)
{
	uintptr_t vaddr;
	size_t len;
	size_t word;

	if (argc != 4) {
		cprintf("usage: %s <paddr> <len> <word>\n", argv[0]);
		return 0;
	}

	vaddr = strtol(argv[1], NULL, 0);
	len = strtol(argv[2], NULL, 0);
	word = strtol(argv[3], NULL, 0);

	dump_phys(vaddr, len, word);

	return 0;
}

int mon_buddyinfo(int argc, char **argv, struct Trapframe *tf)
{
	buddy_info();
	return 0;
}

int mon_switch(int argc, char **argv, struct Trapframe *tf)
{
	int r;
	struct Env *env;
	if (argc != 2) {
		cprintf("usage: %s <envid>\n", argv[0]);
		return 0;
	}
	envid_t target_id = (envid_t)strtol(argv[1], NULL, 0);

	if (envid2env(target_id, &env, 0) < 0) {
		cprintf("No such environment\n");
		return 0;
	}
	lcr3(env->env_cr3);
	cprintf("Switched to environment: %x\n", env->env_id);
	return 0;
}

/***** Kernel monitor command interpreter *****/

#define WHITESPACE "\t\r\n "
#define MAXARGS 16

static int
runcmd(char *buf, struct Trapframe *tf)
{
	int argc;
	char *argv[MAXARGS];
	int i;

	// Parse the command buffer into whitespace-separated arguments
	argc = 0;
	argv[argc] = 0;
	while (1) {
		// gobble whitespace
		while (*buf && strchr(WHITESPACE, *buf))
			*buf++ = 0;
		if (*buf == 0)
			break;

		// save and scan past next arg
		if (argc == MAXARGS-1) {
			cprintf("Too many arguments (max %d)\n", MAXARGS);
			return 0;
		}
		argv[argc++] = buf;
		while (*buf && !strchr(WHITESPACE, *buf))
			buf++;
	}
	argv[argc] = 0;

	// Lookup and invoke the command
	if (argc == 0)
		return 0;
	for (i = 0; i < NCOMMANDS; i++) {
		if (strcmp(argv[0], commands[i].name) == 0)
			return commands[i].func(argc, argv, tf);
	}
	cprintf("Unknown command '%s'\n", argv[0]);
	return 0;
}

void
monitor(struct Trapframe *tf)
{
	char *buf;

	cprintf("Welcome to the JOS kernel monitor!\n");
	cprintf("Type 'help' for a list of commands.\n");

	if (tf != NULL)
		print_trapframe(tf);

	while (1) {
		buf = readline("K> ");
		if (buf != NULL)
			if (runcmd(buf, tf) < 0)
				break;
	}
}

// return EIP of caller.
// does not work if inlined.
// putting at the end of the file seems to prevent inlining.
unsigned
read_eip()
{
	uint32_t callerpc;
	__asm __volatile("movl 4(%%ebp), %0" : "=r" (callerpc));
	return callerpc;
}
