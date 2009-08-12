#ifndef JOS_DEV_E100_H
#define JOS_DEV_E100_H 1

#include <inc/types.h>
#include <inc/x86.h>
#include <inc/mmu.h>
#include <inc/stdio.h>
#include <kern/pmap.h>
#include <dev/pci.h>
#include <dev/pcireg.h>

enum {
	CSR_SCB_STATUS			= 0x0,
	CSR_SCB_CMD_WORD		= 0x2,
	CSR_SCB_GENERAL_PTR		= 0x4,
	CSR_PORT			= 0x8,
	CSR_EEPROM_CTRL			= 0xe,
	CSR_MDI_CTRL			= 0x10,
	CSR_RX_DMA_COUNT		= 0x14,
	CSR_FLOW_CTRL			= 0x19,
	CSR_GENERAL_CTRL		= 0x1c,
	CSR_GENERAL_STATUS		= 0x1d,
	CSR_FUNC_EVENT			= 0x30,
	CSR_EVENT_MASK			= 0x34,
	CSR_FUNC_PRESENT_STATE		= 0x38,
	CSR_FORCE_EVENT			= 0x3c,
} csr_reg_offset;

enum {
	CSR_SCB_STATUS_CX_TNO		= (1<<15),
	CSR_SCB_STATUS_FR		= (1<<14),
	CSR_SCB_STATUS_CNA		= (1<<13),
	CSR_SCB_STATUS_RNR		= (1<<12),
	CSR_SCB_STATUS_MDI		= (1<<11),
	CSR_SCB_STATUS_SWI		= (1<<10),
	CSR_SCB_STATUS_FCP		= (1<<8),
	CSR_SCB_STATUS_CUS		= (1<<6),
	CSR_SCB_STATUS_RUS		= (1<<2),
} csr_scb_status_bits;

enum {
	CSR_SCB_CMD_RU			= (1<<16),
	CSR_SCB_CMD_RU_MASK		= (0x7),
	CSR_SCB_CMD_CU			= (1<<20),
	CSR_SCB_CMD_CU_MASK		= (0xf),
	CSR_SCB_CMD_M			= (1<<24),
	CSR_SCB_CMD_SI			= (1<<25),
	CSR_SCB_CMD_INT_FCP_DISABLE	= (1<<26),
	CSR_SCB_CMD_INT_ER_DISABLE	= (1<<27),
	CSR_SCB_CMD_INT_RNR_DISABLE	= (1<<28),
	CSR_SCB_CMD_INT_CNA_DISABLE	= (1<<29),
	CSR_SCB_CMD_INT_FR_DISABLE	= (1<<30),
	CSR_SCB_CMD_INT_CX_DISABLE	= (1<<31),
} csr_scb_command_bits;

enum {
	CSR_PORT_RESET			= 0x0,
	CSR_PORT_SELFTEST		= 0x1,
	CSR_PORT_SELECTIVE_RESET	= 0x2,
	CSR_PORT_DUMP			= 0x3,
	CSR_PORT_DUMP_WAKE_UP		= 0x7,
} csr_port_opcode;

// Control Block
struct cb {
	volatile uint16_t status;
	uint16_t cmd;
	uint32_t link;
	union {
		struct {
			uint32_t tbd_array_addr;
			uint8_t byte_count;
			uint8_t tx_threshold;
			uint8_t tbd_number;
			uint8_t data[1518]; // For largest data CB
		} tx_packet;
	};
};

enum {
	CB_CMD_NOP			= 0x0,
	CB_CMD_ADDR_SETUP		= 0x1,
	CB_CMD_CONFIGURE		= 0x2,
	CB_CMD_MULTICAST_ADDR_SETUP	= 0x3,
	CB_CMD_TX			= 0x4,
	CB_CMD_LOAD_UCODE		= 0x5,
	CB_CMD_DUMP			= 0x6,
	CB_CMD_DIAGNOSE			= 0x7,
} cb_cmd_opcodes;

enum {
	CB_CMD_EL			= (1<<15), // This CB is the last one on CBL
	CB_CMD_S			= (1<<14), // Suspend
	CB_CMD_I			= (1<<13), // Interrupt
	CB_CMD_CID			= (1<<8),  // CNA interrupt delay
	CB_CMD_NC			= (1<<4),  // CRC and Source addr is filled by HW
	CB_CMD_SF			= (1<<3),  // Simplified and Flexible mode
} cb_cmd_bits;

enum {
	CB_STATUS_C			= (1<<15), // Tx dma complete
	CB_STATUS_OK			= (1<<13), // Cmd execute complete
	CB_STATUS_U			= (1<<12), // Underrun
} cb_status_bits;

#define NR_CB_PER_PAGE		(PGSIZE / (sizeof(struct cb)))
#define NR_TX_RING		16
#define NR_RX_RING		16
#define NR_TX_RING_PAGES	(16/NR_CB_PER_PAGE)
#define NR_RX_RING_PAGES	(16/NR_CB_PER_PAGE)

// Prototypes
int e100_attach(struct pci_func *pcif);

struct e100_private {
	uint16_t	io_base;
	uint16_t	io_size;
	uint8_t		irq;
	struct Page	*tx_ring[NR_TX_RING_PAGES];
	struct Page	*rx_ring[NR_RX_RING_PAGES];
} __attribute__((packed));

#endif	// !JOS_DEV_E100_H
