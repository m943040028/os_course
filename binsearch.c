#include <inc/stab.h>
//#include <stdio.h>


//      For example, given these N_SO stabs:
//              Index  Type   Address
//              0      SO     f0100000
//              13     SO     f0100040
//              117    SO     f0100176
//              118    SO     f0100178
//              555    SO     f0100652
//              556    SO     f0100654
//              657    SO     f0100849
struct Stab stabs[700] =
{
	[0] = {
	.n_type = N_SO,
	.n_value = 0xf0100000,
	},
	[13] = {
	.n_type = N_SO,
	.n_value = 0xf0100040,
	},
	[117] = {
	.n_type = N_SO,
	.n_value = 0xf0100176,
	},
	[118] = {
	.n_type = N_SO,
	.n_value = 0xf0100178,
	},
	[555] = {
	.n_type = N_SO,
	.n_value = 0xf0100652,
	},
	[556] = {
	.n_type = N_SO,
	.n_value = 0xf0100654,
	},
	[657] = {
	.n_type = N_SO,
	.n_value = 0xf0100849,
	},
};

static void
stab_binsearch(const struct Stab *stabs, int *region_left, int *region_right,
		int type, uintptr_t addr)
{
	int l = *region_left, r = *region_right, any_matches = 0;

	while (l <= r) {
		int true_m = (l + r) / 2, m = true_m;

		// search for earliest stab with right type
		while (m >= l && stabs[m].n_type != type)
			m--;
		if (m < l) {    // no match in [l, m]
			l = true_m + 1;
			continue;
		}

		// actual binary search
		any_matches = 1;
		if (stabs[m].n_value < addr) {
			*region_left = m;
			l = true_m + 1;
		} else if (stabs[m].n_value > addr) {
			*region_right = m - 1;
			r = m - 1;
		} else {
			// exact match for 'addr', but continue loop to find
			// *region_right
			*region_left = m;
			l = m;
			addr++;
		}
	}

	if (!any_matches)
		*region_right = *region_left - 1;
	else {
		// find rightmost region containing 'addr'
		for (l = *region_right;
				l > *region_left && stabs[l].n_type != type;
				l--)
			/* do nothing */;
		*region_left = l;
	}
}

int
main()
{
	int left = 0, right = 657;
	stab_binsearch(stabs, &left, &right, N_SO, 0xf0100184);

	printf("left = %d, right = %d\n", left, right);
	return 0;
}
