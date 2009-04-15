/* See COPYRIGHT for copyright information. */

#ifndef JOS_KERN_BUDDY_H
#define JOS_KERN_BUDDY_H
#ifndef JOS_KERNEL
# error "This is a JOS kernel header; user programs should not #include it"
#endif

#define MAX_ORDER       11
#define BUDDY_OF(p, order) \
	({\
	 ppn_t pn = page2ppn(p);\
	 pn ^= (1 << (order));\
	 ppn2page(pn);\
	 })

#define ALLOC_MAGIC             0xffffffff
#define PAGE_ALLOCATED(p)\
	({\
	 bool r = ((p)->pp_link.le_next == (struct Page *)ALLOC_MAGIC);\
	 r;\
	 })
#define PAGE_MARK_ALLOC(p)      { (p)->pp_link.le_next = (struct Page *)ALLOC_MAGIC; }
#define PAGE_MARK_FREE(p)       { (p)->pp_link.le_next = 0; }

// Record a region of physical memory
typedef struct mem_chunk
{
	struct Page *pp;
	uint8_t order;
	uint8_t flags;
} mem_chunk_t;

// Memory Region flags
#define R_NEED_ALLOC	0x0001

#endif
