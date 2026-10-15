/* WinCall hands out RCX, RDX, R8, R9, R16, R17, R18 and R19 as its integer
   argument registers, so it needs APX EGPR.  EGPR cannot be confined to the
   WinCall function itself: the callee reads the last four registers and every
   caller has to be able to name them in order to forward the fifth and later
   arguments.  Rather than reject the attribute when -mapxf was not given
   (which used to end in "unrecognizable insn" in cprop_hardreg when an
   argument spilled over into R16), GCC compiles just the functions that use
   WinCall -- as callee or as caller -- with EGPR enabled.  That is the
   per-function rule clang implements with "+egpr" in
   X86TargetMachine::getSubtargetImpl; wincall-implies-apx-2.c checks that
   functions that do not use WinCall keep the translation unit defaults.
   This box is a Zen 2 and cannot execute APX, so the check is at the
   assembly level.  */

/* { dg-do compile { target { ! ia32 } } } */
/* { dg-require-effective-target apxf } */
/* { dg-options "-O2" } */

__attribute__ ((wincall, noinline)) long
f (long a, long b, long c, long d, long e)
{
  return a + b + c + d + e;
}

long
caller (long a, long b, long c, long d, long e)
{
  return f (a, b, c, d, e);
}

/* The fifth argument is forwarded in R16 and read back from it, although
   neither function was compiled with -mapxf.  `caller' returns straight
   from `f', so GCC turns the call into a sibling call (clang keeps the
   `call'); accept either transfer.  */
/* { dg-final { scan-assembler "%r16" } } */
/* { dg-final { scan-assembler "\t(call|jmp)\tf" } } */
