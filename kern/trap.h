/* See COPYRIGHT for copyright information. */

#ifndef JOS_KERN_TRAP_H
#define JOS_KERN_TRAP_H
#ifndef JOS_KERNEL
# error "This is a JOS kernel header; user programs should not #include it"
#endif

#include <inc/trap.h>
#include <inc/mmu.h>

/* The kernel's interrupt descriptor table */
extern struct Gatedesc idt[];

void idt_init(void);
void print_regs(struct PushRegs *regs);
void print_trapframe(struct Trapframe *tf);
<<<<<<< HEAD:kern/trap.h
=======
void break_point_handler(struct Trapframe *);
>>>>>>> master:kern/trap.h
void page_fault_handler(struct Trapframe *);
void backtrace(struct Trapframe *);
<<<<<<< HEAD:kern/trap.h
=======
void enable_sep(void);
>>>>>>> master:kern/trap.h

#endif /* JOS_KERN_TRAP_H */
