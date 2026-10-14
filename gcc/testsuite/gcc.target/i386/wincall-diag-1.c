/* WinCall attribute diagnostics.  `wincall' is a distinct calling
   convention, so it may not be combined with the two x86 ABI attributes,
   and it is only meaningful where the four extended argument registers
   exist (x86-64 with APX EGPR).  The no-APX error and the 32-bit error
   are covered separately in wincall-no-apx-1.c; this pins the two
   attribute-compatibility errors and the X32 rejection.  */

/* { dg-do compile { target { ! ia32 } } } */
/* { dg-options "-O2 -mapxf" } */

extern void f1 (void) __attribute__ ((wincall, ms_abi));
/* { dg-error "not compatible" "" { target *-*-* } .-1 } */

extern void f2 (void) __attribute__ ((wincall, sysv_abi));
/* { dg-error "not compatible" "" { target *-*-* } .-1 } */
