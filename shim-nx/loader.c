/*
 * Act as iBoot for xnu on the Switch.
 *
 * build.sh appends the mkpayload.py payload to bl33.bin at the first 4K
 * boundary after _end, and hekate loads the whole file at 0xAA000000, so
 * the payload is already in RAM (low bank). Copy the segments to
 * phys_base + (vmaddr - virt_base) in the upper bank, put the ADT and
 * boot_args behind the kernel and jump to the entry point at EL1 with the
 * MMU and caches off, x0 = boot_args (physical).
 *
 * hekate keeps its eMMC sector cache at 0xC7000000 (bdk/memory_map.h), so
 * bl33.bin plus payload has to end below that: ~460 MiB.
 */
#include "t210.h"
#include "console.h"
#include "payload.h"
#include "loader.h"

#define HEKATE_SAFE_END	0xC7000000UL

extern char _end[];

static int magic_ok(const volatile char *m)
{
	static const char want[] = "SWEETXNU";
	for (int i = 0; i < 8; i++)
		if (m[i] != want[i])
			return 0;
	return 1;
}

/* MMU off: everything is Device memory, so copy in aligned 8-byte words. */
static void copy64(uintptr_t dst, uintptr_t src, uint64_t n)
{
	volatile uint64_t *d = (volatile uint64_t *)dst;
	volatile const uint64_t *s = (volatile const uint64_t *)src;
	for (uint64_t i = 0; i < (n + 7) / 8; i++)
		d[i] = s[i];
}

static void zero64(uintptr_t dst, uint64_t n)
{
	volatile uint64_t *d = (volatile uint64_t *)dst;
	for (uint64_t i = 0; i < n / 8; i++)
		d[i] = 0;
}

const struct payload *loader_find(void)
{
	uintptr_t at = ((uintptr_t)_end + 0xfff) & ~0xfffUL;
	if (!magic_ok((const volatile char *)at))
		return 0;
	return (const struct payload *)at;
}

/* End of the payload in RAM, from its own header. */
static uintptr_t payload_end(const struct payload *p)
{
	uint64_t end = p->adt_off + p->adt_size;
	for (uint32_t i = 0; i < p->nsegs; i++) {
		uint64_t e = p->segs[i].file_off + p->segs[i].file_size;
		if (e > end)
			end = e;
	}
	return (uintptr_t)p + end;
}

const char *loader_check(const struct payload *p)
{
	if (p->version != 1 || (p->adt_off & 7))
		return "bad payload header";
	uintptr_t end = payload_end(p);
	con_hex2("PAYLOAD", "at=", (uintptr_t)p, "end=", end);
	con_hex2("PHYS", "base=", p->phys_base >> 12, "size=", p->mem_size >> 12);	/* 4K pages */
	con_hex("ENTRY", p->entry_va);
	if (end > HEKATE_SAFE_END)
		return "payload overlaps hekate buffers";
	if (p->phys_base < 0x100000000UL)
		return "phys_base not in the upper bank";
	if (p->kernel_end - p->virt_base + PAGE_16K * 2 + p->adt_size > p->mem_size)
		return "kernel does not fit in mem_size";
	return 0;
}

typedef void (*xnu_entry_t)(uint64_t boot_args_pa);

const char *loader_boot(const struct payload *p, uintptr_t fb)
{
	uint64_t pa_of_va = p->phys_base - p->virt_base;	/* pa = va + this */
	uintptr_t base = (uintptr_t)p;

	for (uint32_t i = 0; i < p->nsegs; i++) {
		const struct payload_seg *s = &p->segs[i];
		uintptr_t dst = s->vmaddr + pa_of_va;
		uint64_t fsz = (s->file_size + 7) & ~7UL;
		copy64(dst, base + s->file_off, s->file_size);
		if (s->vmsize > fsz)
			zero64(dst + fsz, s->vmsize - fsz);
	}
	con_hex2("SEGS", "n=", p->nsegs, "end=", (p->kernel_end + pa_of_va) >> 12);

	/* ADT, then boot_args, behind the kernel; everything below topOfKernelData is ours. */
	uintptr_t adt_pa = p->kernel_end + pa_of_va;
	copy64(adt_pa, base + p->adt_off, p->adt_size);
	uintptr_t ba_pa = (adt_pa + p->adt_size + PAGE_16K - 1) & ~(PAGE_16K - 1);
	uintptr_t top = ba_pa + PAGE_16K;

	zero64(ba_pa, PAGE_16K);
	volatile struct boot_args *ba = (volatile struct boot_args *)ba_pa;
	ba->Revision = 2;
	ba->Version = 2;
	ba->virtBase = p->virt_base;
	ba->physBase = p->phys_base;
	ba->memSize = p->mem_size;
	ba->topOfKernelData = top;
	ba->Video.v_baseAddr = fb;
	ba->Video.v_display = 1;
	ba->Video.v_rowBytes = FB_STRIDE;
	ba->Video.v_width = FB_W;
	ba->Video.v_height = FB_H;
	ba->Video.v_depth = 32;
	ba->deviceTreeP = adt_pa - pa_of_va;	/* xnu reads it with the MMU on: a VA */
	ba->deviceTreeLength = (uint32_t)p->adt_size;
	for (int i = 0; i < BOOT_LINE_LENGTH; i++)
		ba->CommandLine[i] = p->cmdline[i];
	ba->memSizeActual = p->mem_size;
	con_hex2("BOOTARG", "pa=", ba_pa >> 12, "top=", top >> 12);

	uintptr_t entry_pa = p->entry_va + pa_of_va;
	con_hex("JUMP", entry_pa);
	con_str("XNU", "jumping");

	/* Leave the GIC as we found it: our test enabled the CPU interface and INTID 27. */
	wr32(GICD_ICENABLER(0), BIT(INTID_VTIMER));
	wr32(GICC_CTLR, 0);
	wsr("cntv_ctl_el0", 0);

	/* Everything was written with the MMU and caches off; drop any stale I-cache lines too. */
	__asm__ volatile("msr daifset, #0xf; dsb sy; ic iallu; dsb sy; isb" ::: "memory");
	((xnu_entry_t)entry_pa)(ba_pa);
	return "xnu returned";
}
