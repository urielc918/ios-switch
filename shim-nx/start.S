/*
 * Switch (T210) BL33 shim entry. hekate's L4T launcher has BL31 (ATF)
 * eret here at EL2 (SPSR_EL2T), MMU and caches off, at 0xAA000000.
 *
 * hekate treats base + u32@(base+0x10) as a DTB to patch if it holds FDT
 * magic, so the word at +0x10 stays 0 (-> the branch below, not FDT magic).
 */
	.section .text.head, "ax"
	.globl _head
_head:
	b	_start			/* +0x00 */
	.long	0			/* +0x04 */
	.ascii	"NXSHIM\0\0"		/* +0x08 */
	.long	0			/* +0x10: hekate BL33 DTB offset, keep 0 */
	.long	0
	.quad	_end - _head		/* +0x18: image size incl. bss/stack */

	.text
	.globl _start
_start:
	msr	daifset, #0xf
	mov	x21, x0			/* whatever BL31 left in x0 */
	mrs	x20, CurrentEL
	lsr	x20, x20, #2
	cmp	x20, #2
	b.ne	el1_entry

	/* EL2 -> EL1h, no virtualization. */
	mov	x0, #(1 << 31)		/* HCR_EL2.RW: EL1 is AArch64 */
	msr	hcr_el2, x0
	mrs	x0, cnthctl_el2
	orr	x0, x0, #3		/* EL1PCTEN | EL1PCEN */
	msr	cnthctl_el2, x0
	msr	cntvoff_el2, xzr
	mrs	x0, midr_el1
	msr	vpidr_el2, x0
	mrs	x0, mpidr_el1
	msr	vmpidr_el2, x0
	mov	x0, #0x33ff		/* CPTR_EL2: no FP/SIMD traps */
	msr	cptr_el2, x0
	msr	hstr_el2, xzr
	ldr	x0, =0x30d00800		/* SCTLR_EL1: RES1 bits, MMU/caches off */
	msr	sctlr_el1, x0
	mov	x0, #0x3c5		/* EL1h, DAIF masked */
	msr	spsr_el2, x0
	adr	x0, el1_entry
	msr	elr_el2, x0
	isb
	eret

el1_entry:
	mov	x0, #(3 << 20)		/* CPACR_EL1.FPEN */
	msr	cpacr_el1, x0
	msr	spsel, #1
	ldr	x1, =_stack_top
	mov	sp, x1
	ldr	x1, =vectors
	msr	vbar_el1, x1
	isb
	ldr	x1, =_bss_start
	ldr	x2, =_bss_end
1:	cmp	x1, x2
	b.hs	2f
	str	xzr, [x1], #8
	b	1b
2:	mov	x0, x20			/* entry EL */
	mov	x1, x21			/* entry x0 */
	bl	shim_main
3:	wfe
	b	3b

	.ltorg

/*
 * Exception vectors (same as shim/start.S). Each slot stashes x29/x30,
 * loads its slot number and jumps to the common path, which saves x0-x28
 * and calls exc_handler(slot, regs).
 */
.macro vent slot
	.balign	0x80
	stp	x29, x30, [sp, #-16]!
	mov	x29, #\slot
	b	exc_common
.endm

	.balign	0x800
	.globl vectors
vectors:
	vent 0
	vent 1
	vent 2
	vent 3
	vent 4
	vent 5
	vent 6
	vent 7
	vent 8
	vent 9
	vent 10
	vent 11
	vent 12
	vent 13
	vent 14
	vent 15

exc_common:
	sub	sp, sp, #(30 * 8)
	stp	x0, x1, [sp, #0]
	stp	x2, x3, [sp, #16]
	stp	x4, x5, [sp, #32]
	stp	x6, x7, [sp, #48]
	stp	x8, x9, [sp, #64]
	stp	x10, x11, [sp, #80]
	stp	x12, x13, [sp, #96]
	stp	x14, x15, [sp, #112]
	stp	x16, x17, [sp, #128]
	stp	x18, x19, [sp, #144]
	stp	x20, x21, [sp, #160]
	stp	x22, x23, [sp, #176]
	stp	x24, x25, [sp, #192]
	stp	x26, x27, [sp, #208]
	str	x28, [sp, #224]
	mov	x0, x29
	mov	x1, sp
	bl	exc_handler
	ldp	x0, x1, [sp, #0]
	ldp	x2, x3, [sp, #16]
	ldp	x4, x5, [sp, #32]
	ldp	x6, x7, [sp, #48]
	ldp	x8, x9, [sp, #64]
	ldp	x10, x11, [sp, #80]
	ldp	x12, x13, [sp, #96]
	ldp	x14, x15, [sp, #112]
	ldp	x16, x17, [sp, #128]
	ldp	x18, x19, [sp, #144]
	ldp	x20, x21, [sp, #160]
	ldp	x22, x23, [sp, #176]
	ldp	x24, x25, [sp, #192]
	ldp	x26, x27, [sp, #208]
	ldr	x28, [sp, #224]
	add	sp, sp, #(30 * 8)
	ldp	x29, x30, [sp], #16
	eret
