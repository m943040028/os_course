#include <inc/x86.h>
#include "e100.h"

// Wrapper routines
static void inline
e100_write32(e100_t *dev, uint32_t addr, uint32_t val)
{
	outl(dev->io_base + addr, val);
}

static void inline
e100_write16(e100_t *dev, uint32_t addr, uint16_t val)
{
	outw(dev->io_base + addr, val);
}

static void inline
e100_write8(e100_t *dev, uint32_t addr, uint8_t val)
{
	outb(dev->io_base + addr, val);
}

static uint32_t inline
e100_read32(e100_t *dev, uint32_t addr)
{
	return inl(dev->io_base + addr);
}

static uint16_t inline
e100_read16(e100_t *dev, uint32_t addr)
{
	return inw(dev->io_base + addr);
}

static uint8_t inline
e100_read8(e100_t *dev, uint32_t addr)
{
	return inb(dev->io_base + addr);
}

void e100_set_io_callbacks(e100_t *dev)
{
	dev->write32 = e100_write32;
	dev->write16 = e100_write16;
	dev->write8 = e100_write8;
	dev->read32 = e100_read32;
	dev->read16 = e100_read16;
	dev->read8 = e100_read8;
}
