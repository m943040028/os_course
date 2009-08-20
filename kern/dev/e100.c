// LAB 6: Your driver code here
#include <dev/e100.h>
#include <inc/string.h>
#include <kern/trap.h>
#include <kern/picirq.h>

void e100_int_handler(struct Trapframe *tf)
{
	cprintf("int received\n");
}

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


static void 
alloc_tx_ring(struct e100_private *data) {
	int i;

	struct Page *pp;
	struct cb *cb;
	if (pages_alloc(&pp, get_order(NR_TX_RING_PAGES * PGSIZE)) < 0)
		panic("Cannot allocate tx ring\n");
	data->tx_ring = pp;
	for (i = 0, cb = (struct cb *)page2kva(pp);
		i < NR_TX_CB; i++, cb++)
	{
		// initial with no operation
		cb->cmd = CB_CMD_NOP|CB_CMD_S|CB_CMD_EL;
		cb->status = CB_STATUS_C|CB_STATUS_OK;
		cb->link = PADDR(cb + 1);
	};

	cb--;
	cb->link = (uint32_t)page2pa(data->tx_ring);
	data->tail_cb = data->cur_cb = (struct cb *)KADDR(cb->link);
	data->cb_count = 0;
}

static void
tx_ring_walk(struct e100_private *data) {
	struct Page *pp = data->tx_ring;
	struct cb *first = page2kva(pp), *curr = first;

	do {
		cprintf("CB is at %08p: [%c%c%c]\n", curr,
			(curr->status & CB_STATUS_C) ? 'C' : 'c',
			(curr->status & CB_STATUS_OK)? 'K' : 'k',
			(curr->status & CB_STATUS_U) ? 'U' : 'u'
			 );
		curr = (struct cb*) KADDR(curr->link);
	} while (curr != first);
}

uint8_t inline
e100_read_cu_state(struct e100_private *data) {
	uint32_t status = e100_read32(data, CSR_SCB_STATUS);

	return (status & CSR_SCB_STATUS_RU_STATE_MASK)
		>> CSR_SCB_STATUS_RU_STATE_SHIFT;
}

// Return values:
//   1: All CB is used, should block the requesting envronment.
//   0: Operation successed.
int
e100_tx(struct e100_private *data, void *data_ptr, size_t len) {
	struct cb **cur_cb = &data->cur_cb;

	// All CB is filled, block the environment
	if (data->cb_count == NR_TX_CB)
		return 1;

	(*cur_cb)->cmd = CB_CMD_TX;
	(*cur_cb)->status = 0;
	// use simplified mode
	(*cur_cb)->tx_packet.tbd_array_addr = 0xffffffff;
	(*cur_cb)->tx_packet.byte_count = len;
	// data should be accumulaed in the internal buffer
	// before being transmitted
	// this value is granularity of 8bytes
	(*cur_cb)->tx_packet.tx_threshold = 0x40;
	(*cur_cb)->tx_packet.tbd_number = 0;
	memmove((*cur_cb)->tx_packet.data, data_ptr, len);

	*cur_cb = KADDR((*cur_cb)->link);
	
	//start CU
	if (e100_read_cu_state(data) != CU_STATE_ACTIVE)
		e100_write16(data, CSR_SCB_CMD_WORD, CSR_SCB_CMD_CU_START);
	return 0;
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
	data->irq = pcif->irq_line;

	e100_reset();
	alloc_tx_ring(data);

	//load CU base
	e100_write32(data, CSR_SCB_GENERAL_PTR, 0x0);
	e100_write16(data, CSR_SCB_CMD_WORD, CSR_SCB_CMD_CU_LOAD_BASE);

	//load CU offset
	e100_write32(data, CSR_SCB_GENERAL_PTR, page2pa(data->tx_ring));

	//enable interrupt
	irq_setmask_8259A(irq_mask_8259A & ~(1 << data->irq));

	//enalbe CNA interrupt
	//e100_write32(data, CSR_SCB_CMD_WORD,
	//~CSR_SCB_CMD_INT_CNA_DISABLE & e100_read32(data, CSR_SCB_CMD_WORD));

	e100_tx(data, "test", sizeof("test"));
	e100_tx(data, "test", sizeof("test"));
	e100_tx(data, "test", sizeof("test"));

	tx_ring_walk(data);

	return 0;
}

// Wrapper routines
void inline
e100_write32(struct e100_private *data, uint16_t addr, uint32_t val)
{
	outl(data->io_base + addr, val);
}

void inline
e100_write16(struct e100_private *data, uint16_t addr, uint8_t val)
{
	outw(data->io_base + addr, val);
}

void inline
e100_write8(struct e100_private *data, uint16_t addr, uint8_t val)
{
	outb(data->io_base + addr, val);
}

uint32_t inline
e100_read32(struct e100_private *data, uint16_t addr)
{
	return inl(data->io_base + addr);
}

uint16_t inline
e100_read16(struct e100_private *data, uint16_t addr)
{
	return inw(data->io_base + addr);
}

uint8_t inline
e100_read8(struct e100_private *data, uint16_t addr)
{
	return inb(data->io_base + addr);
}
