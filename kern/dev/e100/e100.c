// LAB 6: Your driver code here
#include <inc/string.h>
#include <kern/trap.h>
#include <kern/picirq.h>
#include <kern/env.h>
#include <kern/sched.h>
#include <inc/queue.h>
#include <inc/error.h>

#include "e100.h"
static e100_t e100;

#define IRQ_ENABLE \
	(CSR_SCB_CMD_INT_FCP_DISABLE|CSR_SCB_CMD_INT_ER_DISABLE |\
	CSR_SCB_CMD_INT_RNR_DISABLE|CSR_SCB_CMD_INT_CNA_DISABLE)

void e100_int_tx_finish(void)
{
	e100_t *dev = &e100;
	struct cb **tail_cb = &dev->tail_tx_cb;
	cprintf("%s\n", __func__);

	while ((*tail_cb)->cmd == (CB_CMD_TX|CB_CMD_I) && 
		(*tail_cb)->status & CB_STATUS_OK) {
		cprintf("TX completed\n");
		// This command is finished, mark it free
		(*tail_cb)->cmd = CB_CMD_NOP|CB_CMD_S|CB_CMD_EL;

		dev->tx_cb_count--;
		*tail_cb = KADDR((*tail_cb)->link);
	}

	assert(dev->tx_cb_count >= 0);
}

void e100_int_rx_finish(void)
{
	e100_t *dev = &e100;
	struct cb **tail_cb = &dev->tail_rx_cb;
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
		dev->rx_cb_count++;

		*tail_cb = KADDR((*tail_cb)->link);
	}
}

void e100_int_handler(struct Trapframe *tf)
{
	cprintf("%s\n", __func__);
	uint16_t status = e100.read16(&e100, CSR_SCB_STATUS);

	cprintf("status = %08x\n", status);

	if (status & CSR_SCB_STATUS_CX_TNO)
		e100_int_tx_finish();

	if (status & CSR_SCB_STATUS_FR);
		e100_int_rx_finish();

	// Ack interrupt
	e100.write16(&e100, CSR_SCB_STATUS, status);
}

static void e100_reset(void) {
	int i;

	e100.write32(&e100, CSR_PORT, CSR_PORT_RESET);
	// wait for 10us, read port 0x84 take 1.25us
	for (i = 0; i < 8; i++)
		e100.read8(&e100, 0x84);
}

static void 
alloc_dma_ring(e100_t *dev) {
	int i;

	struct Page *tx_pp, *rx_pp;
	struct cb *tx_cb, *rx_cb;
	if (pages_alloc(&tx_pp, get_order(NR_TX_RING_PAGES * PGSIZE)) < 0)
		panic("Cannot allocate tx ring\n");
	if (pages_alloc(&rx_pp, get_order(NR_RX_RING_PAGES * PGSIZE)) < 0)
		panic("Cannot allocate rx ring\n");
	dev->tx_ring = tx_pp;
	dev->rx_ring = rx_pp;
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

	// cyclic the ring buffer
	(tx_cb-1)->link = (uint32_t)page2pa(dev->tx_ring);
	(rx_cb-1)->link = (uint32_t)page2pa(dev->rx_ring);

	// tail points to the first cb, to construct initial state
	dev->tail_tx_cb = dev->cur_tx_cb = page2kva(dev->tx_ring);
	dev->tail_rx_cb = dev->cur_rx_cb = page2kva(dev->rx_ring);
	dev->tx_cb_count = 0;
	dev->rx_cb_count = 0;
}

static void
tx_ring_walk(e100_t *dev) {
	struct Page *pp = dev->tx_ring;
	struct cb *first = page2kva(pp), *curr = first;

	cprintf("cur_cb is %08p, tail_cb is %08p\n", dev->cur_tx_cb, dev->tail_tx_cb);

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
	uint32_t status = e100.read32(&e100, CSR_SCB_STATUS);

	return (status & CSR_SCB_STATUS_CU_STATE_MASK)
		>> CSR_SCB_STATUS_CU_STATE_SHIFT;
}

int
e100_tx(void *data_ptr, size_t len) {
	e100_t *dev = &e100;
	struct cb *cur_cb = dev->cur_tx_cb;

	// All CB is filled, block the environment
	if (dev->tx_cb_count == NR_TX_CB)
		return -E_AGAIN;

	cur_cb->cmd = CB_CMD_TX|CB_CMD_I;
	cur_cb->status = 0;
	// use simplified mode
	cur_cb->tx_packet.tbd_array_addr = 0xffffffff;
	cur_cb->tx_packet.byte_count = len;

	// data should be accumulaed in the internal buffer
	// before being transmitted
	// this value is granularity of 8bytes
	cur_cb->tx_packet.tx_threshold = 0x40;
	cur_cb->tx_packet.tbd_number = 0;
	memmove(cur_cb->tx_packet.data, data_ptr, len);
	dev->tx_cb_count++;

	//start CU
	if (e100_read_cu_state() != CU_STATE_ACTIVE) {
		// before trigger cu, we should load h/w offset into 
		// pointer register
		e100.write32(&e100, CSR_SCB_GENERAL_PTR, PADDR(cur_cb));
		e100.write16(&e100, CSR_SCB_CMD_WORD, CSR_SCB_CMD_CU_START);
	}
	e100.write16(&e100, CSR_SCB_CMD_WORD, CSR_SCB_CMD_SI);

	dev->cur_tx_cb = KADDR(cur_cb->link);
	return 0;
}

int
e100_rx(void *data_ptr, size_t *len)
{
	e100_t *dev = &e100;
	struct cb *cur_cb;
	uint32_t actual_len;

	// no packet received, block the caller
	if (dev->rx_cb_count == 0)
		return -E_AGAIN;

	cur_cb = dev->cur_rx_cb;
	actual_len = cur_cb->rx_packet.actual_count & CB_COUNT_MASK;

	// OK, pick a packet
	memmove(data_ptr, cur_cb->rx_packet.data, actual_len);
	*len = actual_len;

	// after dev has trnasfered, EOF and F flags should be
	// cleared to indicated this RFD is free to use by device
	cur_cb->cmd = 0;

	dev->rx_cb_count--;
	dev->cur_rx_cb = KADDR(cur_cb->link);

	return 0;
}

// PCI attach function
int e100_attach(struct pci_func *pcif)
{
	e100_t *dev = &e100;
	cprintf("%s called\n", __func__);
	pci_func_enable(pcif);

	// BAR[1] is i/o address
	// TODO: the pci_func structure should add a bit
	// to distinguish between i/o address and mmio address
	dev->io_base = (uint16_t) pcif->reg_base[1];
	dev->io_size = (uint16_t) pcif->reg_size[1];
	dev->irq = pcif->irq_line;

	e100_set_io_callbacks(dev);

	e100_reset();
	alloc_dma_ring(dev);

	//load RU base
	e100.write32(&e100, CSR_SCB_GENERAL_PTR, 0x0);
	e100.write16(&e100, CSR_SCB_CMD_WORD, CSR_SCB_CMD_RU_LOAD_BASE);

	//load RU offset 
	e100.write32(&e100, CSR_SCB_GENERAL_PTR, page2pa(dev->rx_ring));

	//trigger RU
	e100.write16(&e100, CSR_SCB_CMD_WORD, CSR_SCB_CMD_RU_START);

	//load CU base
	e100.write32(&e100, CSR_SCB_GENERAL_PTR, 0x0);
	e100.write16(&e100, CSR_SCB_CMD_WORD, CSR_SCB_CMD_CU_LOAD_BASE);

	//load CU offset
	e100.write32(&e100, CSR_SCB_GENERAL_PTR, page2pa(dev->tx_ring));

	//trigger CU
	e100.write16(&e100, CSR_SCB_CMD_WORD, CSR_SCB_CMD_CU_START);

	//Enable CX interrupt and FR interrupt
	e100.write16(&e100, CSR_SCB_CMD_WORD, IRQ_ENABLE);

	//enable interrupt
	irq_setmask_8259A(irq_mask_8259A & ~(1 << dev->irq));

	return 0;
}
