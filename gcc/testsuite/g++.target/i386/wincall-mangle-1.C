/* A WinCall function-pointer type is part of the Itanium mangling: the
   `wincall' attribute table entry sets affects_type_identity, so the
   convention is written as the `U7wincall' vendor qualifier, exactly as
   `ms_abi' becomes `U5ms_abi'.  No cp/mangle.cc change is needed.  The
   `@win' assembler suffix is a PE-only decoration and is checked in
   gcc.target/i386/wincall-symbol-1.c.  */

/* { dg-do compile { target { x86_64-*-* } } } */
/* { dg-require-effective-target apxf } */
/* { dg-options "-O2 -mapxf" } */

using W = void (__attribute__ ((wincall)) *)(int);

template <typename T> T id (T x) { return x; }

W w;

template W id<W> (W);

/* { dg-final { scan-assembler "_Z2idIPU7wincallFviEET_S2_" } } */
