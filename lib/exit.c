
#include <inc/lib.h>

void
exit(void)
{
<<<<<<< HEAD:lib/exit.c
	close_all();
=======
>>>>>>> master:lib/exit.c
	sys_env_destroy(0);
}

