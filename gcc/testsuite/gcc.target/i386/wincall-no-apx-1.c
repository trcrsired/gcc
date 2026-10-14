/* WinCall hands out RCX, RDX, R8, R9, R16, R17, R18 and R19 as its
   integer argument registers, so it is only usable where the last four
   exist.  Without APX EGPR the backend cannot encode R16-R19 at all, so
   the convention is gated on -mapxf rather than handing out registers
   that cannot appear in an instruction (which used to end in
   "unrecognizable insn" in cprop_hardreg when an argument spilled over
   into R16).  */

/* { dg-do compile { target { ! ia32 } } } */
/* { dg-options "-O2" } */

__attribute__ ((wincall, noinline)) long
f (long a)
{
  return a;
}

/* { dg-error "wincall.*requires APX EGPR support" "" { target *-*-* } .-4 } */
