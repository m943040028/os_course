#include "ns.h"
#include <inc/lib.h>

void
input(envid_t ns_envid) {
	int ret;

	binaryname = "ns_input";

	// LAB 6: Your code here:
	// 	- read a packet from the device driver
	//	- send it to the network server
        struct jif_pkt *ppkt;

	while (1) {
		extern uint8_t nsipcbuf[PGSIZE];
		ppkt = (struct jif_pkt *) nsipcbuf;
		while ( (ppkt->jp_len = sys_frame_recv(ppkt->jp_data)) < 0)
			sys_yield();

		cprintf("%s: read a packet: size=%d\n", binaryname, ppkt->jp_len);
		ipc_send(ns_envid, NSREQ_INPUT, ppkt, PTE_P|PTE_W|PTE_U);
	}
}
