/* { dg-do compile } */
/* { dg-options "-fherbceptions -fno-exceptions -O0" } */

/* The Xtensa herbceptions error-channel discriminant is a nominal
   register leaf in a4 rather than the boolean registers: Xtensa has no
   practical way to move a boolean out of b0..b15 into a general
   register.  That is what clang's herbceptions return lowering does --
   see llvm/test/CodeGen/Xtensa/throws-attr.ll: the callee writes
   "movi a4, 0/1" before ret (call0) or retw (windowed), and the caller
   reads the leaf back, which is a4 under call0 and a12 under the
   windowed ABI, where the callee's a4 is the caller's a12 after the
   register window rotation.  A function's failure flag is an ordinary
   stack slot during the body and is only moved into a4 on the shared
   return edge.  */

extern int ext (int) return_failure{long};

int
callee (int x) return_failure{long}
{
  if (x < 0)
    return_failure 1;
  return x + 1;
}

int
caller (int x) return_failure{long}
{
  return try (ext (x));
}

/* The callee leaves the discriminant in a4 ...  */
/* { dg-final { scan-assembler "mov\\ta4, a" } } */
/* ... and the caller reads the leaf straight after the call: its own a12
   under the windowed ABI (the default), where the callee's a4 sits.  */
/* { dg-final { scan-assembler "call8\\text" } } */
/* { dg-final { scan-assembler "a12" } } */
