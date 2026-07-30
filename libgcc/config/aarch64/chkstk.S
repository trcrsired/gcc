/* AArch64 Windows stack probe helper.
   Copyright (C) 2026 Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 3, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

Under Section 7 of GPL version 3, you are granted additional
permissions described in the GCC Runtime Library Exception, version
3.1, as published by the Free Software Foundation.

You should have received a copy of the GNU General Public License and
a copy of the GCC Runtime Library Exception along with this program;
see the files COPYING3 and COPYING.RUNTIME respectively.  If not, see
<http://www.gnu.org/licenses/>.  */

/* On Windows (PE/COFF) the OS grows the stack via guard pages, so a
   stack allocation that moves SP by more than one page must touch each
   intervening page to hit the guard page; otherwise a large frame that
   jumps over it faults with an access violation.

   The compiler (clang and MSVC) emits a call to __chkstk for such
   frames:

     mov x15, #SIZE / 16   (in units of 16 bytes)
     bl  __chkstk
     sub sp, sp, x15, lsl #4

   __chkstk must touch each page from SP down to SP - SIZE without
   modifying SP, clobbering only the intra-procedure-call scratch
   registers x16 and x17.  */

#ifdef L_chkstk
	.text
	.p2align 2
	.global	__chkstk
__chkstk:
	/* Number of bytes to allocate.  */
	lsl	x16, x15, #4
	mov	x17, sp
1:
	sub	x17, x17, #0x1000	/* 4096 bytes = one page.  */
	subs	x16, x16, #0x1000
	ldr	xzr, [x17]		/* Probe (touch) the page.  */
	b.gt	1b
	ret
#endif
