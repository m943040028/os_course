/* See COPYRIGHT for copyright information. */

#include <inc/mmu.h>
#include <inc/memlayout.h>
#include <inc/trap.h>

#include <kern/picirq.h>

.extern idt
###################################################################
# exceptions/interrupts
###################################################################

/* The TRAPHANDLER macro defines a globally-visible function for handling
 * a trap.  It pushes a trap number onto the stack, then jumps to _alltraps.
 * Use TRAPHANDLER for traps where the CPU automatically pushes an error code.
 */ 
#define TRAPHANDLER(name, num, dpl)					\
	.globl name;		/* define global symbol for 'name' */	\
	.type name, @function;	/* symbol type is function */		\
	.align 2;		/* align function definition */		\
name:				/* function starts here */		\
	/* mask interrupt, kernel should run with interrupt		\
	 * disabled. CPU will not disable interrupt if the trap		\
	 * is through trap gate						\
	 */								\
	cli;								\
	pushl $(num);							\
	jmp _alltraps;							\
	TABLE_GENERATOR(name, num, dpl, 0xf);				\

/* Use TRAPHANDLER_NOEC for traps where the CPU doesn't push an error code.
 * It pushes a 0 in place of the error code, so the trap frame has the same
 * format in either case.
 */
#define TRAPHANDLER_NOEC(name, num, dpl)				\
	.globl name;							\
	.type name, @function;						\
	.align 2;							\
name:									\
	cli;								\
	pushl $0;							\
	pushl $(num);							\
	jmp _alltraps;							\
	TABLE_GENERATOR(name, num, dpl, 0xf);				\

#define INTERRUPT_HANDLER(name, num)					\
	.globl name;							\
	.type name, @function;						\
	.align 2;							\
name:									\
	pushl $0;							\
	pushl $(num);							\
	jmp _alltraps;							\
	TABLE_GENERATOR(name, num, 0, 0xe);				\

#define TABLE_GENERATOR(name, num, dpl, type)				\
_##name:								\
	.long	name;							\
_##name##_entry:							\
	.long	idt + 8 * (num);					\
name##_patcher:	/* create idt entry for this handler automatically */	\
	push	%ebp;							\
	mov	%esp,%ebp;						\
	push	%eax;							\
	push	%ebx;							\
	movl	_##name, %eax;						\
	movl	_##name##_entry, %ebx;					\
	movw	%ax, (%ebx);	/* low 16bits of offset */		\
	movl	$GD_KT, 2(%ebx);/* segment selector */			\
	shrl	$16, %eax;						\
	movb	$0, 4(%ebx);						\
	movb	$(1<<7 | dpl<<5 | type), 5(%ebx);			\
	movw	%ax, 6(%ebx);	/* high 16bits of offset */		\
	pop	%ebx;							\
	pop	%eax;							\
	leave;								\
	ret;								\
.section .idt_patcher, "ax";						\
	.long	name##_patcher;						\
.previous

/*
 * Lab 3: Your code here for generating entry points for the different traps.
 */
.text
TRAPHANDLER_NOEC(trap_divide_error, 	T_DIVIDE, 	0)
TRAPHANDLER(trap_debug_exception,	T_DEBUG,  	0)
TRAPHANDLER(trap_nmi, 			T_NMI,		0)
TRAPHANDLER_NOEC(trap_breakpoint, 	T_BRKPT, 	3)
TRAPHANDLER(trap_overflow, 		T_OFLOW, 	0)
TRAPHANDLER(trap_bound_check, 		T_BOUND, 	0)
TRAPHANDLER(trap_invalid_opcode, 	T_ILLOP, 	0)
TRAPHANDLER(trap_divide_not_available, 	T_DEVICE, 	0)
TRAPHANDLER(trap_double_fault, 		T_DBLFLT, 	0)
TRAPHANDLER(trap_invalid_tss, 		T_TSS, 		0)
TRAPHANDLER(trap_segment_not_present, 	T_SEGNP, 	0)
TRAPHANDLER(trap_stack_exception, 	T_STACK, 	0)
TRAPHANDLER(trap_general_protection, 	T_GPFLT, 	0)
TRAPHANDLER(trap_page_fault, 		T_PGFLT, 	0)
TRAPHANDLER(trap_floating_point, 	T_FPERR, 	0)
TRAPHANDLER(trap_align_fault, 		T_ALIGN, 	0)
TRAPHANDLER(trap_machine_check, 	T_MCHK, 	0)
TRAPHANDLER(trap_simd_floating_point, 	T_SIMDERR, 	0)

TRAPHANDLER_NOEC(trap_sys_call,		T_SYSCALL,	3)

INTERRUPT_HANDLER(irq_timer,		IRQ_OFFSET+IRQ_TIMER)
INTERRUPT_HANDLER(irq_kbd,		IRQ_OFFSET+IRQ_KBD)
INTERRUPT_HANDLER(irq_spurious,		IRQ_OFFSET+IRQ_SPURIOUS)
INTERRUPT_HANDLER(irq_ide,		IRQ_OFFSET+IRQ_IDE)
INTERRUPT_HANDLER(irq_error,		IRQ_OFFSET+IRQ_ERROR)

/*
 * Lab 3: Your code here for _alltraps
 */
_alltraps:
	/* to finish a trapframe */
	pushl 	%ds
	pushl 	%es
	pushal

	/* switch to kernel data segment */
	movw	$GD_KD, %ax
	movw	%ax, %ds
	movw	%ax, %es

	/* call trap with current sp as argument */
	pushl	%esp
	call	trap
	/* should never return */
	jmp	.
