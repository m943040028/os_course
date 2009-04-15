#ifndef JOS_KERN_SYSCALL_H
#define JOS_KERN_SYSCALL_H
#ifndef JOS_KERNEL
# error "This is a JOS kernel header; user programs should not #include it"
#endif

#include <inc/syscall.h>

int32_t syscall(uint32_t num, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5);

static void 	sys_cputs(const char *s, size_t len);
static int	sys_cgetc(void);
static envid_t	sys_getenvid(void);
static int	sys_env_destroy(envid_t envid);

#endif /* !JOS_KERN_SYSCALL_H */
