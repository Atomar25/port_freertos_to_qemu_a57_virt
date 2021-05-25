/*
 *  Copyright 2021, Amit Singh Tomar
 *  MMU enablement for QEMU ARMv8
 *
 */
#include <stdint.h>
#include <stdbool.h>

#define PAGE_SIZE	0x1000
#define UNUSED_DESC	0x6EbAAD0BBADbA6E0

#define SZ_1M 0x100000
#define SZ_4K 0x1000

/* granularity */
#define PT_PAGE     0b11        // 4k granule
#define PT_BLOCK    0b01        // 2M granule
/* accessibility */
#define PT_KERNEL   (0<<6)      // privileged, supervisor EL1 access only
#define PT_USER     (1<<6)      // unprivileged, EL0 access allowed
#define PT_RW       (0<<7)      // read-write
#define PT_RO       (1<<7)      // read-only
#define PT_AF       (1<<10)     // accessed flag
#define PT_NX       (1UL<<54)   // no execute
/* shareability */
#define PT_OSH      (2<<8)      // outter shareable
#define PT_ISH      (3<<8)      // inner shareable

/* defined in MAIR register */
#define PT_MEM      (0<<2)      // normal memory
#define PT_DEV      (1<<2)      // device MMIO
#define PT_NC (2<<2) // non-cachable

#define MT_NORMAL 0
#define MT_DEVICE 1

int mmu_init(unsigned long load_addr)
{
	unsigned long r, b;
	uint32_t i;
	uint64_t reg;
	volatile uint32_t *ptr = (void *)0x09000000;

	uint64_t *l1_table = ((load_addr) + SZ_1M);
	uint64_t *l2_table_0 = ((load_addr) + SZ_1M + SZ_4K);
	uint64_t *l2_table_1 = ((load_addr) + SZ_1M + (2*SZ_4K));

	for (i = 2; i <= 511; i++)
		l1_table[i] = UNUSED_DESC;

	for (i = 0; i <= 511; i++)
		l2_table_0[i] = UNUSED_DESC;

	for (i = 0; i <= 511; i++)
		l2_table_1[i] = UNUSED_DESC;

#if 0
	/* Only one table (which is at level 1) is used, that just shows
	 * with one table we can map 1GB of address space, where VA == PA
	 */
	l1_table[0] =   (uint64_t)0x0000000;
	l1_table[0] |= PT_BLOCK | PT_KERNEL | PT_AF;
	l1_table[0] |= (MT_DEVICE << 2);

	l1_table[1] = (uint64_t)0x40000000;
	l1_table[1] |= PT_BLOCK | PT_AF | PT_KERNEL;
	l1_table[1] |= (MT_NORMAL << 2);
#endif

#if 1
	/* This demonstrates address translation using two tables
	 * where table walk starts from level 1 page table (since
	 * VA is 31 bit) and first two enteries of level 1 table
	 * (0 and 1) points to two level 2 tables (l2_table_0 and
	 * l2_table_1).
	 *
	 * Each entry (PTE) in level 2 points to BLOCK of 2MB in
	 * address space (hence PT_BLOCK flag), here in this
	 * in this example we have used entry 64 and 72 for first,
	 * level 2 table, .i.e. l2_table_0, and 20 MB for FreeRTOS
	 * code section by second, * level 2 table, .i.e. l2_table_1.
	 */
	l1_table[0] =  (uint64_t)l2_table_0;
	l1_table[0] |= PT_PAGE | PT_KERNEL | PT_AF;
	l1_table[0] |= (MT_NORMAL << 2);

	l1_table[1] =  (uint64_t)l2_table_1;
	l1_table[1] |= PT_PAGE | PT_KERNEL | PT_AF;
	l1_table[1] |= (MT_NORMAL << 2);

	/*
	 * Covers GIC region
	 */
	l2_table_0[64] = (uint64_t)0x08000000;
	l2_table_0[64] |= PT_BLOCK | PT_KERNEL | PT_AF;
	l2_table_0[64] |= (MT_DEVICE << 2);

	/*
	 * Covers UART region
	 */
	l2_table_0[72] = (uint64_t)0x09000000;
	l2_table_0[72] |= PT_BLOCK | PT_KERNEL | PT_AF;
	l2_table_0[72] |= (MT_DEVICE << 2);

	/*
	 * Covers FreeRTOS code section, and along with translation tables
	 */
	for ( i = 0; i < 5; i++) {
		l2_table_1[i] = ((uint64_t)0x40000000 + (i * 0x200000));
		l2_table_1[i] |= PT_BLOCK | PT_KERNEL | PT_AF;
		l2_table_1[i] |= (MT_NORMAL << 2);
	}
#endif

	__asm__ volatile ("dsb sy": : : "memory");
	__asm__ volatile ("msr ttbr0_el1, %0" :: "r" (l1_table));

	r=  (0b00LL << 37) | // TBI=0, no tagging
	(0b1LL << 31)| // IPS= 32 bit ... 000 = 32bit, 001 = 36bit, 010 = 40bit
	(0b000LL << 32)| // IPS= 32 bit ... 000 = 32bit, 001 = 36bit, 010 = 40bit
	(0b1LL  << 23) | // set EPD1 to disable Translation table walk via TBBR1
	(0b00LL << 14) | // TG0=4k
	(0b11LL << 12) | // SH0=3 inner
	(0b01LL << 10) | // ORGN0=1 write back
	(0b01LL << 8) |  // IRGN0=1 write back
	(0b0LL  << 7) |  // EPD0 enable lower half
	(32LL   << 0);   // T0SZ=33, 3 levels (512G)
	asm volatile ("msr tcr_el1, %0" :: "r" (r));

	reg=(0xFF << (MT_NORMAL * 8)) |    // normal, in/out write back, non-alloc
             (0x00 <<  (MT_DEVICE * 8));    // device, nGnRE

	__asm__ volatile ("msr mair_el1, %0\n" :: "r" (reg));

	 __asm__ volatile ("isb; mrs %0, sctlr_el1" : "=r" (r));
	r |= (1<<0) | (1<<2);     // set M, enable MMU
	__asm__ volatile ("msr sctlr_el1, %0" : : "r" (r));

	return 0;
}
