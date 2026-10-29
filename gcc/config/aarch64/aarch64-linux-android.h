/* Machine description for AArch64 Android targets.
   Copyright (C) 2026 Free Software Foundation, Inc.

   This file is part of GCC.

   GCC is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3, or (at your option)
   any later version.

   GCC is distributed in the hope that it will be useful, but WITHOUT
   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
   or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
   License for more details.

   You should have received a copy of the GNU General Public License
   along with GCC; see the file COPYING3.  If not see
   <http://www.gnu.org/licenses/>.  */

#ifndef GCC_AARCH64_LINUX_ANDROID_H
#define GCC_AARCH64_LINUX_ANDROID_H

#ifndef TARGET_ANDROID_API_LEVEL
#define TARGET_ANDROID_API_LEVEL 0
#endif

/* Bionic installs the 64-bit dynamic linker as linker64; the
   architecture-independent name is the 32-bit one.  */
#undef GNU_USER_DYNAMIC_LINKER
#define GNU_USER_DYNAMIC_LINKER					\
  CHOOSE_DYNAMIC_LINKER (GLIBC_DYNAMIC_LINKER, UCLIBC_DYNAMIC_LINKER,	\
			 BIONIC_DYNAMIC_LINKER64, MUSL_DYNAMIC_LINKER)

#undef  TARGET_OS_CPP_BUILTINS
#define TARGET_OS_CPP_BUILTINS()					\
  do									\
    {									\
      GNU_USER_TARGET_OS_CPP_BUILTINS ();				\
      if (TARGET_ANDROID)						\
	{								\
	  builtin_define_with_int_value ("__ANDROID__", 1);		\
	  /* Clang predefines this; libgcc's DWARF FDE lookup keys	\
	     the PT_GNU_EH_FRAME path off it, which is the only one	\
	     bionic supports.  sys/cdefs.h repeats the identical	\
	     definition.  */						\
	  builtin_define_with_int_value ("__BIONIC__", 1);		\
	  /* A triple without an explicit API level (e.g. in	    \
	     configure) gets the lowest level supported by AArch64.  */ \
	  builtin_define_with_int_value ("__ANDROID_MIN_SDK_VERSION__", \
					 TARGET_ANDROID_API_LEVEL > 0    \
					 ? TARGET_ANDROID_API_LEVEL : 21); \
	  builtin_define_with_value ("__ANDROID_API__",		\
				     "__ANDROID_MIN_SDK_VERSION__", 0); \
	  /* Bionic adds a signedness overload of ioctl for Clang,	    \
	     which C cannot express; the header documents this macro as    \
	     the way to suppress it.  C++ uses our overloadable support    \
	     instead, so the overload stays available there.  */	    \
	  if (!c_dialect_cxx ())					\
	    {								\
	      builtin_define ("BIONIC_IOCTL_NO_SIGNEDNESS_OVERLOAD");	\
	      /* fixincludes' machine_name pass rewrites the non-reserved	\
		 guard to this reserved spelling in include-fixed.  */	\
	      builtin_define ("__BIONIC_IOCTL_NO_SIGNEDNESS_OVERLOAD__"); \
	    }								\
	}								\
    }									\
  while (0)

/* Android executables are position independent.  Bionic provides its
   own startup files in the sysroot (crtbegin_dynamic.o and friends),
   has no crti.o/crtn.o pair, and keeps thread support in libc.  Bionic
   ignores DT_RPATH, so use DT_RUNPATH as clang does.

   Bionic refuses load segments that are both writable and executable.
   The default linker script already starts the writable segment on a
   fresh page (DATA_SEGMENT_ALIGN) and RELRO keeps the writable data on
   a separate page, so one RX and one RW segment are enough.
   --separate-code only adds more page-aligned PT_LOADs, which GNU ld
   pads to max-page-size in the file.  Verified by relinking
   libstdc++.so without it: the segments are R E and RW, never RWE.

   Segments are aligned to 64K: some Android devices (for example
   NVIDIA Jetson boards, and emulator or Waydroid images built for
   them) run 64K page kernels.  A 64K-aligned binary also works on 4K
   and 16K kernels, since congruence holds for the smaller page size as
   well.

   GNU ld used to align the end of RELRO to the maximum page size and
   then start the writable segment at the next page boundary, which
   forced a max-page-size hole into the file.  It now packs RELRO and
   the writable segment the same way lld does: the writable segment
   starts on the next page in virtual address space but keeps the file
   offset of the previous one plus the RELRO size, so no padding is
   written.  A hello world is ~6K with either linker.  */
#undef  LINK_SPEC
#define LINK_SPEC							\
  LINUX_OR_ANDROID_LD (LINUX_TARGET_LINK_SPEC " "			\
		       AARCH64_ERRATA_LINK_SPEC,			\
		       LINUX_TARGET_LINK_SPEC " "			\
		       AARCH64_ERRATA_LINK_SPEC " " ANDROID_LINK_SPEC	\
		       " -z max-page-size=0x10000"			\
		       " -z noseparate-code"				\
		       " --enable-new-dtags"				\
		       " %{!shared:%{!static:%{!static-pie:"		\
		       "%{!r:%{!no-pie:-pie}}}}}")

#undef  CC1_SPEC
#define CC1_SPEC							\
  LINUX_OR_ANDROID_CC (GNU_USER_TARGET_CC1_SPEC " " ASAN_CC1_SPEC " "	\
		       AARCH64_ERRATA_COMPILE_SPEC,			\
		       GNU_USER_TARGET_CC1_SPEC " " ASAN_CC1_SPEC " "	\
		       AARCH64_ERRATA_COMPILE_SPEC " " ANDROID_CC1_SPEC)

#undef  LIB_SPEC
#define LIB_SPEC							\
  LINUX_OR_ANDROID_LD (GNU_USER_TARGET_LIB_SPEC,			\
		       GNU_USER_TARGET_NO_PTHREADS_LIB_SPEC " "		\
		       ANDROID_LIB_SPEC)

/* Android devices do not ship libgcc_s.so, so the unwinder and helper
   routines are linked statically.  */
#undef  REAL_LIBGCC_SPEC
#define REAL_LIBGCC_SPEC						\
  "%{!shared-libgcc:-lgcc -lgcc_eh}%{shared-libgcc:-lgcc_s -lgcc}"

#undef  STARTFILE_SPEC
#define STARTFILE_SPEC							\
  LINUX_OR_ANDROID_LD (GNU_USER_TARGET_STARTFILE_SPEC,			\
		       ANDROID_STARTFILE_SPEC)

/* Link the TLS-alignment object into executables; bionic rejects a TLS
   segment that is not 64-byte aligned, and GNU ld only takes the
   alignment from non-empty TLS sections.  */
#undef  ANDROID_STARTFILE_SPEC
#define ANDROID_STARTFILE_SPEC						\
  "%{shared: crtbegin_so%O%s;:"						\
  "  %{static: crtbegin_static%O%s crtandroidtls%O%s;"			\
  ": crtbegin_dynamic%O%s crtandroidtls%O%s}}"

#undef  ENDFILE_SPEC
#define ENDFILE_SPEC							\
  GNU_USER_TARGET_MATHFILE_SPEC " "					\
  LINUX_OR_ANDROID_LD (GNU_USER_TARGET_ENDFILE_SPEC,			\
		       ANDROID_ENDFILE_SPEC)

#endif  /* GCC_AARCH64_LINUX_ANDROID_H */
