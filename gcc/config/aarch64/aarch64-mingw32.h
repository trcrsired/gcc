/* Operating system specific defines for AArch64 Windows-on-ARM targets.
   Copyright (C) 2026 Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3, or (at your option)
any later version.

GCC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING3.  If not see
<http://www.gnu.org/licenses/>.  */

#ifndef GCC_AARCH64_MINGW32_H
#define GCC_AARCH64_MINGW32_H

/* Windows on ARM64 requires the DYNAMIC_BASE (ASLR) characteristic on
   every PE image; the loader rejects images without it.  The generic
   mingw LINK_SPEC maps -no-pie to --disable-dynamicbase, which would
   produce such images.  Override that here so ASLR is always kept for
   aarch64-w64-mingw32 regardless of the -no-pie/-pie setting.  */
#undef LINK_SPEC_DISABLE_DYNAMICBASE
#define LINK_SPEC_DISABLE_DYNAMICBASE ""

/* Windows on ARM64 is increasingly run on systems with 16K pages
   (Apple Silicon, Android).  Align every section to 64K by default so
   the loader can map images directly regardless of the page size; this
   matches what Wine uses when building WoA binaries.  */
#undef LINK_SPEC_SECTION_ALIGNMENT
#define LINK_SPEC_SECTION_ALIGNMENT " --section-alignment=0x10000"

#endif /* GCC_AARCH64_MINGW32_H */
