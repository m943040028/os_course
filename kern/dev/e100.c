// LAB 6: Your driver code here
#include <dev/e100.h>
#include <inc/string.h>
#include <kern/trap.h>
#include <kern/picirq.h>
#include <kern/env.h>
#include <kern/sched.h>
#include <inc/queue.h>

static struct e100_private e100_private;

// Wrapper routines
void inline
e100_write32(uint16_t addr, uint32_t val)
{
	outl(e100_private.io_base + addr, val);
}

void inline
e100_write16(uint16_t addr, uint16_t val)
{
	outw(e100_private.io_base + addr, val);
}

void inline
e100_write8(uint16_t addr, uint8_t val)
{
	outb(e100_private.io_base + addr, val);
}

uint32_t inline
e100_read32(uint16_t addr)
{
	return inl(e100_private.io_base + addr);
}

uint16_t inline
e100_read16(uint16_t addr)
{
	return inw(e100_private.io_base + addr);
}

uint8_t inline
e100_read8(uint16_t addr)
{
	return inb(e100_private.io_base + addr);
}

void e100_int_tx_finish(void)
{
	struct e100_private *data = &e100_private;
	struct cb **tail_cb = &data->tail_tx_cb;
	struct Env *ep;
	cprintf("%s\n", __func__);

	while ((*tail_cb)->cmd == (CB_CMD_TX|CB_CMD_I) && 
		(*tail_cb)->status & CB_STATUS_OK) {
		cprintf("TX completed\n");
		// This command is finished, mark it free
		(*tail_cb)->cmd = CB_CMD_NOP|CB_CMD_S|CB_CMD_EL;

		data->tx_cb_count--;
		*tail_cb = KADDR((*tail_cb)->link);
	}

	assert(data->tx_cb_count >= 0);

	// wake up the environment(s) that waiting for
	// TX ring space(s).
	if (!LIST_EMPTY(&data->wait_queue)) {
		ep = LIST_FIRST(&data->wait_queue);
		LIST_REMOVE(ep, env_link);
		ep->env_status = ENV_RUNNABLE;
	}
}

void e100_int_rx_finish(void)
{
	struct e100_private *data = &e100_private;
	struct cb **tail_cb = &data->tail_rx_cb;
	cprintf("%s\n", __func__);

	while ( ((*tail_cb)->status & (CB_STATUS_C|CB_STATUS_OK))
		== (CB_STATUS_C|CB_STATUS_OK) &&
		((*tail_cb)->rx_packet.actual_count & (CB_COUNT_F|CB_COUNT_EOF))
		== (CB_COUNT_F|CB_COUNT_EOF))
	{
		cprintf("RX completed\n");
		cprintf("actual_size = %d, size = %d\n",
			(*tail_cb)->rx_packet.actual_count & CB_COUNT_MASK,
			(*tail_cb)->rx_packet.size & CB_COUNT_MASK);
		data->rx_cb_count++;

		*tail_cb = KADDR((*tail_cb)->link);
	}

	// notify the environment who is waiting for packet
	// to receive

	// after data has trnasfered, EOF and F flags should be
	// cleared to indicated this RFD is free to use by device
}

void e100_int_handler(struct Trapframe *tf)
{
	cprintf("%s\n", __func__);
	uint16_t status = e100_read16(CSR_SCB_STATUS);

	cprintf("status = %08x\n", status);
	// Ack interrupt
	e100_write16(CSR_SCB_STATUS, status);

	if (status & CSR_SCB_STATUS_CX_TNO)
		e100_int_tx_finish();

	if (status & CSR_SCB_STATUS_FR);
		e100_int_rx_finish();
}

static void e100_reset(void) {
	int i;

	e100_write32(CSR_PORT, CSR_PORT_RESET);
	// wait for 10us, read port 0x84 take 1.25us
	for (i = 0; i < 8; i++)
		inb(0x84);
}

static void 
alloc_dma_ring(struct e100_private *data) {
	int i;

	struct Page *tx_pp, *rx_pp;
	struct cb *tx_cb, *rx_cb;
	if (pages_alloc(&tx_pp, get_order(NR_TX_RING_PAGES * PGSIZE)) < 0)
		panic("Cannot allocate tx ring\n");
	if (pages_alloc(&rx_pp, get_order(NR_RX_RING_PAGES * PGSIZE)) < 0)
		panic("Cannot allocate rx ring\n");
	data->tx_ring = tx_pp;
	data->rx_ring = rx_pp;
	for (i = 0, tx_cb = (struct cb *)page2kva(tx_pp),
		rx_cb = (struct cb *)page2kva(rx_pp);
		i < NR_TX_CB; i++, tx_cb++, rx_cb++)
	{
		// initial with no operation
		tx_cb->cmd = CB_CMD_NOP|CB_CMD_S|CB_CMD_EL;
		tx_cb->status = CB_STATUS_C|CB_STATUS_OK;
		tx_cb->link = PADDR(tx_cb + 1);

		// let this RFD becomes a header RFD
		cprintf("rx_cb phys %08x\n", PADDR(rx_cb));
		//rx_cb->cmd = CB_CMD_H;
		rx_cb->cmd = 0;
		rx_cb->status = 0;
		rx_cb->rx_packet.size = 1518;
		rx_cb->link = PADDR(rx_cb + 1);
		
	};

	// cyclic link the ring buffer
	tx_cb--;
	tx_cb->link = (uint32_t)page2pa(data->tx_ring);
	rx_cb--;
	rx_cb->link = (uint32_t)page2pa(data->rx_ring);

	// tail points to the first cb, to construct initial state
	data->tail_tx_cb = data->cur_tx_cb = (struct cb *)KADDR(tx_cb->link);
	data->tail_rx_cb = data->cur_rx_cb = (struct cb *)KADDR(rx_cb->link);
	data->tx_cb_count = 0;
	data->rx_cb_count = 0;
}

static void
tx_ring_walk(struct e100_private *data) {
	struct Page *pp = data->tx_ring;
	struct cb *first = page2kva(pp), *curr = first;

	cprintf("cur_cb is %08p, tail_cb is %08p\n", data->cur_tx_cb, data->tail_tx_cb);

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
e100_read_cu_state(void) {
	uint32_t status = e100_read32(CSR_SCB_STATUS);

	return (status & CSR_SCB_STATUS_RU_STATE_MASK)
		>> CSR_SCB_STATUS_RU_STATE_SHIFT;
}

void
e100_tx(void *data_ptr, size_t len) {
	struct e100_private *data = &e100_private;
	struct cb **cur_cb = &data->cur_tx_cb;

	// All CB is filled, block the environment
	if (data->tx_cb_count == NR_TX_CB) {
		curenv->env_status = ENV_NOT_RUNNABLE;
		LIST_INSERT_HEAD(&data->wait_queue, curenv,
				env_link);
		sched_yield();
	}

	(*cur_cb)->cmd = CB_CMD_TX|CB_CMD_I;
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
	data->tx_cb_count++;
	
	//start CU
	if (e100_read_cu_state() != CU_STATE_ACTIVE) {
		// before trigger cu, we should load h/w offset into 
		// pointer register
		e100_write32(CSR_SCB_GENERAL_PTR, PADDR(*cur_cb));
		e100_write16(CSR_SCB_CMD_WORD, CSR_SCB_CMD_CU_START);
	}

	*cur_cb = KADDR((*cur_cb)->link);
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

	LIST_INIT(&data->wait_queue);

	e100_reset();
	alloc_dma_ring(data);

	//load RU base
	e100_write32(CSR_SCB_GENERAL_PTR, 0x0);
	e100_write16(CSR_SCB_CMD_WORD, CSR_SCB_CMD_RU_LOAD_BASE);

	//load RU offset 
	e100_write32(CSR_SCB_GENERAL_PTR, page2pa(data->rx_ring));

	//trigger RU
	e100_write16(CSR_SCB_CMD_WORD, CSR_SCB_CMD_RU_START);

	//load CU base
	e100_write32(CSR_SCB_GENERAL_PTR, 0x0);
	e100_write16(CSR_SCB_CMD_WORD, CSR_SCB_CMD_CU_LOAD_BASE);

	//load CU offset
	e100_write32(CSR_SCB_GENERAL_PTR, page2pa(data->tx_ring));

	//trigger CU
	e100_write16(CSR_SCB_CMD_WORD, CSR_SCB_CMD_CU_START);

	//Enable CX interrupt and FR interrupt
	e100_write16(CSR_SCB_CMD_WORD,
			CSR_SCB_CMD_INT_FCP_DISABLE |
			CSR_SCB_CMD_INT_ER_DISABLE |
			CSR_SCB_CMD_INT_RNR_DISABLE |
			CSR_SCB_CMD_INT_CNA_DISABLE);

	//enable interrupt
	irq_setmask_8259A(irq_mask_8259A & ~(1 << data->irq));

	return 0;
}
