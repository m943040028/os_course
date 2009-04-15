#ifndef JOS_KERN_KDEBUG_H
#define JOS_KERN_KDEBUG_H

#include <inc/types.h>

// Debug information about a particular instruction pointer
struct Eipdebuginfo {
	const char *eip_file;		// Source code filename for EIP
	int eip_line;			// Source code linenumber for EIP

	const char *eip_fn_name;	// Name of function containing EIP
					//  - Note: not null terminated!
	int eip_fn_namelen;		// Length of function name
	uintptr_t eip_fn_addr;		// Address of start of function
	int eip_fn_narg;		// Number of function arguments
};

int debuginfo_eip(uintptr_t eip, struct Eipdebuginfo *info);

// Custom debug staff
#ifdef KDEBUG
void inline k_debug(uint8_t catelog, uint8_t level, char* fmt, ...);
#define DBG(catelog, level, fmt...) k_debug(catelog, level, fmt)
#else
#define DBG(catelog, level, fmt...) {}
#endif

void inline k_debug_msg_on(void);
void inline k_debug_msg_off(void);

// catelog type
#define C_MEM_ALLOC		(1<<0)
#define C_VM			(1<<1)
#define C_ENV			(1<<2)
#define C_SYS_CALL		(1<<3)
#define C_SCHED			(1<<4)

// debug level
#define KDEBUG_INFO		0x1
#define KDEBUG_FLOW		0x2
#define KDEBUG_VERBOSE		0x8

#endif
