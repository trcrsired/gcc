/* WinCall returns aggregates of up to 32 bytes in RAX, RDX, RCX, R8.
   The last pair, RCX:R8, is not a consecutive register pair, and the
   backend used to describe the second half of the value as a TImode at
   CX -- which names RCX:RBX.  RBX is callee-saved, so the callee stored
   the fourth eight-byte chunk into it, restored RBX on the way out, and
   the chunk was gone; the caller read the chunk out of its own saved
   RBX.  Every 25..32 byte WinCall return came back with its last eight
   bytes clobbered (17..24 byte returns need only three chunks and were
   fine).  Before WinCall became gated on -mapxf this was confirmed by
   running a 17..32 byte round-trip sweep; now that the convention
   requires APX it is checked at the assembly level instead.  */

/* { dg-do compile { target { ! ia32 } } } */
/* { dg-require-effective-target apxf } */
/* { dg-options "-O2 -mapxf" } */

struct S32 { long a, b, c, d; };
struct S24 { long a, b, c; };
struct S16 { long a, b; };

__attribute__ ((wincall, noinline, noipa)) struct S32
m32 (void)
{
  return (struct S32) { 1, 2, 3, 4 };
}

__attribute__ ((wincall, noinline, noipa)) struct S24
m24 (void)
{
  return (struct S24) { 1, 2, 3 };
}

__attribute__ ((wincall, noinline, noipa)) struct S16
m16 (void)
{
  return (struct S16) { 1, 2 };
}

/* The fourth chunk goes to R8 ...  */
/* { dg-final { scan-assembler "movl\t\\\$4, %r8d" } } */
/* ... and not to the callee-saved RBX, which is what TImode at CX
   would have named.  */
/* { dg-final { scan-assembler-not "%rbx" } } */
/* { dg-final { scan-assembler-not "%ebx" } } */
/* The third chunk is RCX, so R8 must not be reached by a shifted
   three-chunk value either.  */
/* { dg-final { scan-assembler "movl\t\\\$3, %ecx" } } */
