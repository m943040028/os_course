#ifndef JOS_DEV_E100_H
#define JOS_DEV_E100_H 1

#include <inc/types.h>
#include <inc/x86.h>
#include <inc/mmu.h>
#include <inc/stdio.h>
#include <kern/pmap.h>
#include <dev/pci.h>
#include <dev/pcireg.h>

struct Trapframe;

enum csr_reg_offset {
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
};

enum csr_scb_status_bits {
	CSR_SCB_STATUS_RU_STATE_SHIFT	= 2,
	CSR_SCB_STATUS_RU_STATE_MASK	= 0x3,
	CSR_SCB_STATUS_CU_STATE_SHIFT	= 6,
	CSR_SCB_STATUS_CU_STATE_MASK	= 0xf,
	CSR_SCB_STATUS_CX_TNO		= (1<<15),
	CSR_SCB_STATUS_FR		= (1<<14),
	CSR_SCB_STATUS_CNA		= (1<<13),
	CSR_SCB_STATUS_RNR		= (1<<12),
	CSR_SCB_STATUS_MDI		= (1<<11),
	CSR_SCB_STATUS_SWI		= (1<<10),
	CSR_SCB_STATUS_FCP		= (1<<8),
	CSR_SCB_STATUS_CUS		= (1<<6),
	CSR_SCB_STATUS_RUS		= (1<<2),
};

enum cu_state {
	CU_STATE_IDEL			= 0x0,
	CU_STATE_ACTIVE			= 0x1,
	CU_STATE_LPQ_ACTIVE		= 0x2,
	CU_STATE_HQP_ACTIVE		= 0x3,
};

static char cu_state_to_string[][10] =
{
	"CU_IDLE",
	"CU_ACTIVE",
	"LPQ",
	"HQP",
};


enum csr_scb_command_bits {
	CSR_SCB_CMD_RU_SHIFT		= 0,
	CSR_SCB_CMD_RU_MASK		= (0x7),
	CSR_SCB_CMD_CU_SHIFT		= 4,
	CSR_SCB_CMD_CU_MASK		= (0xf),
	CSR_SCB_CMD_M			= (1<<8),
	CSR_SCB_CMD_SI			= (1<<9),
	CSR_SCB_CMD_INT_FCP_DISABLE	= (1<<10),
	CSR_SCB_CMD_INT_ER_DISABLE	= (1<<11),
	CSR_SCB_CMD_INT_RNR_DISABLE	= (1<<12),
	CSR_SCB_CMD_INT_CNA_DISABLE	= (1<<13),
	CSR_SCB_CMD_INT_FR_DISABLE	= (1<<14),
	CSR_SCB_CMD_INT_CX_DISABLE	= (1<<15),
};

enum csr_scb_cu_opcode {
	CSR_SCB_CMD_CU_NOOP		= (0x0<<CSR_SCB_CMD_CU_SHIFT),
	CSR_SCB_CMD_CU_START		= (0x1<<CSR_SCB_CMD_CU_SHIFT),
	CSR_SCB_CMD_CU_RESUME		= (0x2<<CSR_SCB_CMD_CU_SHIFT),
	CSR_SCB_CMD_CU_LOAD_BASE	= (0x6<<CSR_SCB_CMD_CU_SHIFT),
};

enum csr_scb_ru_opcode {
	CSR_SCB_CMD_RU_NOOP		= (0x0<<CSR_SCB_CMD_RU_SHIFT),
	CSR_SCB_CMD_RU_START		= (0x1<<CSR_SCB_CMD_RU_SHIFT),
	CSR_SCB_CMD_RU_RESUME		= (0x2<<CSR_SCB_CMD_RU_SHIFT),
	CSR_SCB_CMD_RU_ABORT		= (0x4<<CSR_SCB_CMD_RU_SHIFT),
	CSR_SCB_CMD_RU_LOAD_BASE	= (0x6<<CSR_SCB_CMD_RU_SHIFT),
};

enum csr_port_opcode {
	CSR_PORT_RESET			= 0x0,
	CSR_PORT_SELFTEST		= 0x1,
	CSR_PORT_SELECTIVE_RESET	= 0x2,
	CSR_PORT_DUMP			= 0x3,
	CSR_PORT_DUMP_WAKE_UP		= 0x7,
};

// Control Block
//
// For simplicy, we generize the Control Block Descriptor
// and Received Frame Descriptor to this structure.

// Also, we use simplified mode for tx and rx buffer
struct cb {
	volatile uint16_t status;
	uint16_t cmd;
	uint32_t link;
	union {
		// Used for transmit packet CB command (simplified mode)
		struct {
			uint32_t tbd_array_addr;
			uint16_t byte_count;
			uint8_t tx_threshold;
			uint8_t tbd_number;
			uint8_t data[1518]; // For largest frame
		} tx_packet;

		// Used for Receive Frame Descriptor (simplified mode)
		struct {
			uint32_t reserved;
			// Note: in simplified mode, actual_count should
			// equal to size
			uint16_t actual_count; // total number of bytes write into RFA
			uint16_t size; // received frame size exclude header RFD size
			uint8_t data[1518];
		} rx_packet;
	};
};

enum cb_cmd_opcodes {
	CB_CMD_NOP			= 0x0,
	CB_CMD_ADDR_SETUP		= 0x1,
	CB_CMD_CONFIGURE		= 0x2,
	CB_CMD_MULTICAST_ADDR_SETUP	= 0x3,
	CB_CMD_TX			= 0x4,
	CB_CMD_LOAD_UCODE		= 0x5,
	CB_CMD_DUMP			= 0x6,
	CB_CMD_DIAGNOSE			= 0x7,
};

enum cb_tx_cmd_bits {
	CB_CMD_EL			= (1<<15), // This CB is the last one on CBL
	CB_CMD_S			= (1<<14), // Suspend
	CB_CMD_I			= (1<<13), // Interrupt
	CB_CMD_CID			= (1<<8),  // CNA interrupt delay
	CB_CMD_NC			= (1<<4),  // CRC and Source addr is filled by HW
	CB_CMD_SF			= (1<<3),  // Simplified and Flexible mode
};

enum cb_rx_cmd_bits {
	CB_CMD_H			= (1<<4),  // Current RFD is header RFD(used for simplified mode)
};

enum cb_rx_count_bits {
	CB_COUNT_F			= (1<<14), // Actual byte count updated
	CB_COUNT_EOF			= (1<<15), // Data has been completely placed into data area
	CB_COUNT_MASK			= 0x3fff,
};

enum cb_status_bits {
	CB_STATUS_C			= (1<<15), // Tx dma complete
	CB_STATUS_OK			= (1<<13), // Cmd execute complete
	CB_STATUS_U			= (1<<12), // Underrun
};

#define NR_TX_RING_PAGES	8
#define NR_TX_CB		((NR_TX_RING_PAGES * PGSIZE) / (sizeof(struct cb)))
#define NR_RX_RING_PAGES	NR_TX_RING_PAGES
#define NR_RX_CB		NR_TX_CB

// Prototypes
int e100_attach(struct pci_func *pcif);
void e100_int_handler(struct Trapframe *tf);
void e100_tx(void *data_ptr, size_t len);

LIST_HEAD(wait_queue_head, Env);

struct e100_private {
	uint16_t	io_base;
	uint16_t	io_size;
	uint8_t		irq;
	uint8_t		cu_state;
	struct Page	*tx_ring;
	struct Page	*rx_ring;
	struct cb	*cur_tx_cb;
	struct cb	*tail_tx_cb;
	int8_t		tx_cb_count;
	struct cb	*cur_rx_cb;
	struct cb	*tail_rx_cb;
	int8_t		rx_cb_count;
	struct wait_queue_head wait_queue;
} __attribute__((packed));

#endif	// !JOS_DEV_E100_H
