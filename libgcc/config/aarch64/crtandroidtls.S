/* Ensure the executable's TLS segment is aligned to 64 bytes.

   Bionic's loader on AArch64 rejects executables whose PT_TLS segment is
   less aligned than 64 bytes (clang derives that alignment from the empty
   .tdata section in crtbegin_dynamic.o, which GNU ld discards when it is
   empty).  A retained, empty TLS section with 64-byte alignment gives the
   linker the alignment without reserving any TLS storage; when the program
   has no TLS at all the section is dropped and no TLS segment exists.

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

	.section .tbss,"awTR",%nobits
	.balign 64
	.hidden	__gcc_android_tls_alignment
	.type	__gcc_android_tls_alignment, %object
	.size	__gcc_android_tls_alignment, 0
__gcc_android_tls_alignment:
