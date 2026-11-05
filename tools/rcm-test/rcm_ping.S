/*
 * RCM "ping" payload for the BPMP (ARM7TDMI): no display, no SD.
 * Waits 3 s on the microsecond timer, then reboots into RCM
 * (PMC_SCRATCH0 bit 1 + PMC_CNTRL MAIN_RST). If the Switch drops off USB
 * and re-enumerates as 0955:7321 ~3 s after the send, payloads execute.
 * Build: ./build.sh -> rcm_ping.bin; send with fusee-nano.
 */
	.arm
	.section .text.start, "ax"
	.globl _start
_start:
	ldr	r0, =0x60005010		@ TIMERUS_CNTR_1US
	ldr	r1, [r0]		@ start
	ldr	r3, =3000000		@ 3 s
1:	ldr	r2, [r0]
	sub	r2, r2, r1
	cmp	r2, r3
	blo	1b
	ldr	r0, =0x7000E450		@ PMC_SCRATCH0
	ldr	r1, [r0]
	orr	r1, r1, #2		@ boot to RCM
	str	r1, [r0]
	ldr	r0, =0x7000E400		@ PMC_CNTRL
	ldr	r1, [r0]
	orr	r1, r1, #0x10		@ MAIN_RST
	str	r1, [r0]
2:	b	2b
	.ltorg
