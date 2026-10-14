/* WinCall lets an empty record consume no argument register slot, so the
   first real argument still lands in the first register (ECX/RCX).  This
   is the C++ empty-class case; in C a zero-size struct is turned into a
   pointer by the front end before the backend sees it, which is generic
   GCC behaviour and not specific to WinCall.  */

/* { dg-do compile { target { x86_64-*-* } } } */
/* { dg-require-effective-target apxf } */
/* { dg-options "-O2 -mapxf" } */

struct empty {};

__attribute__ ((wincall, noinline))
long long
f (empty e, long long x)
{
  (void) e;
  return x;
}

/* `x' is the first register argument because `e' took no slot.  */
/* { dg-final { scan-assembler "movq\t%rcx, %rax" } } */
