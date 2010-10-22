#include "ns.h"
#include <inc/x86.h>

#define DIAG "output_thread: "

void
output(envid_t ns_envid) {
	int32_t req;
	envid_t whom;
	int perm;

	binaryname = "ns_output";

	// LAB 6: Your code here:
	// 	- read a packet from the network server
	//	- send the packet to the device driver
	int r;
	struct jif_pkt *ppkt;

	while (1) {
		extern uint8_t nsipcbuf[PGSIZE];
		req = ipc_recv((int32_t *) &whom, (void *) nsipcbuf, &perm);
		if (req < 0)
			panic("ipc_recv() failed: %e\n", req);
		cprintf(DIAG "Read message responsible for request %08x\n", req);

		ppkt = (struct jif_pkt *) nsipcbuf;
		while ((r = sys_frame_send(ppkt->jp_data, ppkt->jp_len)) < 0)
			sys_yield();
	}
}
