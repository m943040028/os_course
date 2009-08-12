// LAB 6: Your driver code here
#include <dev/e100.h>

static struct e100_private e100_private;

static void e100_reset(void) {
	int i;

	outl(e100_private.io_base + CSR_PORT, CSR_PORT_RESET);
	// wait for 10us, read port 0x84 take 1.25us
	for (i = 0; i < 8; i++)
		inb(0x84);
}

// After DMA ring is created
// (1). The device driver tells the CU where to find the ring by sending the CU 
//      the physical address of the first buffer in the ring.
// (2). The CU examines the CB by accessing the buffer in main memory directly. 
//      If the CB does not contain any commands, the CU goes into suspend mode 
//      waiting to be reactivated.
// (3). The CU caches the physical address of the CB it stopped at in a local 
//      register. When restarted, the CU will use this cached physical address 
//      to access the DMA ring. It is therefore critical that the links in the DMA
//      ring are rarely (if at all) changed. 
// (4). When the driver wants to transmit a packet, it places the packet into the
//      next available buffer in the ring and restarts the 82559ER's CU.
// (5). The CU reads the packet from main memory into an internal buffer.
//      (using PCI bursts) Once the packet is in the card's buffer, it can transmit
//      it over the wire.
// (6). When the packet is sent, the CU can set the CB status bits to indicate success
//      and mark the buffer in the ring as empty. The CU then follows the link pointer
//      to the next buffer in the ring and repeats the above process until it encounters
//      a buffer with the suspend bit set in the control entry of the CB. 


static void alloc_tx_ring(struct e100_private *data) {
	int i, j;
	uint32_t *last_link = 0;

	for (i = 0; i < NR_TX_RING_PAGES; i++) {
		struct cb *cb;
		struct Page *pp;

		if (page_alloc(&pp) < 0)
			panic("Cannot allocate tx ring(%d)\n", i);
		data->tx_ring[i] = pp;

		if (last_link)
			*last_link = (uint32_t)page2pa(pp);

		for (cb = (struct cb *)page2kva(pp), j = 0; 
			j < NR_CB_PER_PAGE; cb++, j++) {
			// initial with last element and suspend
			cb->cmd = CB_CMD_NOP|CB_CMD_EL|CB_CMD_S;
			cb->link = PADDR(cb + 1);
			last_link = (uint32_t *)&cb->link;
		}
	}

	*last_link = (uint32_t)page2pa(data->tx_ring[0]);
}

static void tx_ring_walk(struct e100_private *data) {
	struct Page *pp = data->tx_ring[0];
	struct cb *first = page2kva(pp), *curr = first;

	do {
		cprintf("CB is at %08p\n", curr);
		curr = (struct cb*) KADDR(curr->link);
	} while (curr != first);
}

// PCI attach function
int e100_attach(struct pci_func *pcif)
{
	struct e100_private *data = &e100_private;
	cprintf("%s called\n", __func__);
	pci_func_enable(pcif);

	// BAR[1] is i/o address
	// TODO: the pci_func structure should add a bit
	// to distinguish between i/o address and mmio address
	data->io_base = (uint16_t) pcif->reg_base[1];
	data->io_size = (uint16_t) pcif->reg_size[1];

	e100_reset();
	alloc_tx_ring(data);
	tx_ring_walk(data);

	return 0;
}
