/* { dg-do compile } */
/* { dg-options "-fherbceptions -fno-exceptions -O0" } */

/* The SPARC herbceptions error-channel discriminant is carried in the
   carry bit of the integer condition codes, %icc.C, which subcc -- and
   therefore cmp -- sets as the borrow out of the subtraction.  That is
   what clang's herbceptions return lowering does -- see
   llvm/test/CodeGen/SPARC/throws-attr.ll: the callee emits "a cmp glued
   before retl" with carry set meaning failure, and the caller reads the
   bit back, or branches on it directly.  A function's failure flag is an
   ordinary stack slot during the body (calls and scope-exit cleanups
   freely clobber the condition codes) and is only compared into %icc on
   the shared return edge, so the callee ends with `cmp %g0, <flag>' and
   the caller materializes the bit with `addx %g0, 0, <reg>' immediately
   after the call.  */

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

/* The callee sets the carry from the failure flag ...  */
/* { dg-final { scan-assembler "cmp\\t%g0," } } */
/* ... and the caller materializes it straight after the call.  */
/* { dg-final { scan-assembler "call\\text" } } */
/* { dg-final { scan-assembler "addx\\t%g0, 0," } } */
