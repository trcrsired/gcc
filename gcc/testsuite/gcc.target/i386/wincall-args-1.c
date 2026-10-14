/* WinCall argument classification.  A trivial aggregate of up to 32
   bytes is expanded into eight-byte chunks and handed out in the eight
   integer argument registers RCX, RDX, R8, R9, R16, R17, R18, R19; a
   larger one goes by invisible reference.  The chunks are cut by size
   and not by field, so the sub-word tail of a mixed layout shares the
   last eight-byte chunk.  That is the rule docs/WinCall.md and ABI.md
   spell out, and the rule the LLVM classifier's own comment states
   ("expanded into its 8-byte parts"); clang's implementation currently
   coerces per field instead, which is recorded in
   bugs/gcc_herbceptions/wincall-argument-classification/.

   The aggregate is passed as the *first* argument so the chunks land in
   RCX, RDX, R8, R9 and a per-field expansion would visibly push the
   trailing `char e' into a fifth register.  The test is asm-level
   because this box is a Zen 2 and cannot execute APX.  */

/* { dg-do compile { target { ! ia32 } } } */
/* { dg-require-effective-target apxf } */
/* { dg-options "-O2 -mapxf" } */

struct Mix { long long a, b, c; int d; char e; };	/* 29 bytes */
struct S32 { long long a, b, c, d; };		/* 32 bytes */
struct S40 { long long a, b, c, d, e; };		/* 40 bytes */

__attribute__ ((wincall, noinline)) long
mix (struct Mix s)
{
  return s.a + s.b + s.c + s.d + s.e;
}

__attribute__ ((wincall, noinline)) long
four (struct S32 s)
{
  return s.a + s.b + s.c + s.d;
}

__attribute__ ((wincall, noinline)) long
byref (struct S40 s)
{
  return s.a + s.b + s.c + s.d + s.e;
}

/* Mix: a, b and c fill the first three chunks; `d' is the low half of the
   fourth chunk R9 and `e' its second byte, read back with a 32-bit shift.
   A per-field expansion would instead consume a fifth register for `e'.  */
/* { dg-final { scan-assembler "%r9d" } } */
/* { dg-final { scan-assembler "shrq\t\\\$32, %r9" } } */
/* { dg-final { scan-assembler-not "%r16" } } */

/* S32 fills the four chunks exactly.  */
/* { dg-final { scan-assembler "%r8" } } */

/* S40 does not fit and is passed by pointer, which arrives in RCX.  */
/* { dg-final { scan-assembler "8\\(%rcx\\)" } } */
