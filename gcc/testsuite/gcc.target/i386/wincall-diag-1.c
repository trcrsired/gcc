/* WinCall attribute diagnostics.  `wincall' is a distinct calling
   convention, so it may not be combined with the two x86 ABI attributes,
   and it is only meaningful on x86-64.  A function that uses WinCall now
   implies APX EGPR instead of rejecting the attribute without -mapxf;
   that is checked in wincall-implies-apx-1.c and -2.c.  This pins the two
   attribute-compatibility errors and the X32 rejection.  */

/* { dg-do compile { target { ! ia32 } } } */
/* { dg-options "-O2 -mapxf" } */

extern void f1 (void) __attribute__ ((wincall, ms_abi));
/* { dg-error "not compatible" "" { target *-*-* } .-1 } */

extern void f2 (void) __attribute__ ((wincall, sysv_abi));
/* { dg-error "not compatible" "" { target *-*-* } .-1 } */
