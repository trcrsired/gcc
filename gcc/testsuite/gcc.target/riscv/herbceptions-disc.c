/* { dg-do compile } */
/* { dg-options "-fherbceptions -fno-exceptions -O0" } */

/* The RISC-V herbceptions error-channel discriminant is a nominal
   register leaf rather than a condition flag: RISC-V has none.  The
   payload comes back where the base ABI puts it -- a0, or a0:a1 for
   values wider than XLEN -- and the discriminant in a2 (X12), which is
   an argument register but is call-clobbered, so no live value can be
   parked there across a call.  A function's failure flag is an ordinary
   stack slot during the body (calls and scope-exit cleanups freely
   clobber a2) and is only moved into a2 on the shared return edge, so
   the callee stores it with `lbu a2,' just before the epilogue.  The
   caller snapshots it with `andi ..., a2, 1' immediately after the call,
   before the call's result is materialized.  Mirrors LLVM's
   llvm/test/CodeGen/RISCV/throws-attr.ll, which reads the leaf with
   `andi a2, a2, 1'.  */

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

/* The callee stores the failure flag into a2 ...  */
/* { dg-final { scan-assembler "lbu\\ta2," } } */
/* ... and the caller reads a2 straight after the call.  */
/* { dg-final { scan-assembler "call\\text" } } */
/* { dg-final { scan-assembler "andi\\t\[a-z\]\[0-9\]+,a2,1" } } */
