#include <inc/assert.h>

#include <kern/env.h>
#include <kern/pmap.h>
#include <kern/monitor.h>

<<<<<<< HEAD:kern/sched.c
=======
#define KDEBUG
#include <kern/kdebug.h>
>>>>>>> master:kern/sched.c

// Choose a user environment to run and run it.
void
sched_yield(void)
{
	// Implement simple round-robin scheduling.
	// Search through 'envs' for a runnable environment,
	// in circular fashion starting after the previously running env,
	// and switch to the first such environment found.
	// It's OK to choose the previously running env if no other env
	// is runnable.
	// But never choose envs[0], the idle environment,
	// unless NOTHING else is runnable.

<<<<<<< HEAD:kern/sched.c
	// LAB 4: Your code here.
=======
	int target, count = NENV;
>>>>>>> master:kern/sched.c

<<<<<<< HEAD:kern/sched.c
=======
	// If no current running environment, searching started from
	// envs[1]
	if (curenv)
		target = ENVX(curenv->env_id);
	else
		target = 0;

	while (count-- > 0 &&
		(envs[target = (++target)%NENV].env_status != ENV_RUNNABLE ||
		!target));

	if (count >= 0) {
		DBG(C_SCHED, KDEBUG_FLOW, "picking environment id %x\n",
				envs[target].env_id);
		env_run(&envs[target]);
	}

	DBG(C_SCHED, KDEBUG_FLOW,
		"Nothing else is runnable, picking idle environment\n");
>>>>>>> master:kern/sched.c
	// Run the special idle environment when nothing else is runnable.
	if (envs[0].env_status == ENV_RUNNABLE)
		env_run(&envs[0]);
	else {
		cprintf("Destroyed all environments - nothing more to do!\n");
		while (1)
			monitor(NULL);
	}
}
