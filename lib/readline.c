#include <inc/stdio.h>
#include <inc/error.h>

#define BUFLEN 1024
static char buf[BUFLEN];

char *
readline(const char *prompt)
{
	int i, c, echoing;

<<<<<<< HEAD:lib/readline.c
#if JOS_KERNEL
=======
>>>>>>> master:lib/readline.c
	if (prompt != NULL)
		cprintf("%s", prompt);
<<<<<<< HEAD:lib/readline.c
#else
	if (prompt != NULL)
		fprintf(1, "%s", prompt);
#endif
=======
>>>>>>> master:lib/readline.c

	i = 0;
	echoing = iscons(0);
	while (1) {
		c = getchar();
		if (c < 0) {
<<<<<<< HEAD:lib/readline.c
			if (c != -E_EOF)
				cprintf("read error: %e\n", c);
=======
			cprintf("read error: %e\n", c);
>>>>>>> master:lib/readline.c
			return NULL;
		} else if (c >= ' ' && i < BUFLEN-1) {
			if (echoing)
				cputchar(c);
			buf[i++] = c;
		} else if (c == '\b' && i > 0) {
			if (echoing)
				cputchar(c);
			i--;
		} else if (c == '\n' || c == '\r') {
			if (echoing)
				cputchar(c);
			buf[i] = 0;
			return buf;
		}
	}
}

