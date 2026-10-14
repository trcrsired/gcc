/* Herbceptions over WinCall, C++ side.  A `throws' channel on a WinCall
   function returns its payload in the WinCall return registers (a
   16-byte std::error comes back in RAX:RDX rather than through a hidden
   pointer) and keeps the failure discriminant in CF, which the caller
   reads immediately after the call.  asm-level only: the convention
   requires -mapxf and this box cannot execute APX.  */

/* { dg-do compile { target { x86_64-*-* } } } */
/* { dg-require-effective-target apxf } */
/* { dg-options "-O2 -mapxf -fherbceptions -fno-exceptions" } */

namespace std { struct error { const void *domain; __UINTPTR_TYPE__ code; }; }

__attribute__ ((wincall, noinline))
long
c (long a) throws
{
  if (a < 0)
    throw throws (std::error{ (const void *) 1, (__UINTPTR_TYPE__) -a });
  return a + 1;
}

long
caller (long a) throws
{
  return c (a);
}

/* The callee materialises the failure flag with a CF-setting negate.  */
/* { dg-final { scan-assembler "negb" } } */

/* The caller reads CF immediately after the call.  (`throws' is part of
   the function type, so the callee mangles as `_Z1cDrl'; on PE it also
   carries the `@win' suffix.)  */
/* { dg-final { scan-assembler "call\t_Z1cDrl(@win)?\n" } } */
/* { dg-final { scan-assembler "setc" } } */
