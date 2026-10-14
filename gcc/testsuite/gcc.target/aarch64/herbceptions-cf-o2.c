/* { dg-do compile } */
/* { dg-options "-fherbceptions -fno-exceptions -O2 -fno-inline" } */
/* At -O2 SRA promotes the failure slot to a register, so the callee must
   compare that register (the `(use (reg:CC_C ...))' glued into the return
   keeps NZCV.C live across the flag-preserving epilogue).  This used to
   ICE with `unrecognizable insn' because herbceptions_load_failed required
   a memory operand.  `f' is called rather than inlined so that the `cset'
   capture in `g' survives: -fno-inline is part of the test.  */

int
f (int x) return_failure{long}
{
  if (x < 0)
    return_failure 1;
  return x;
}

int
g (int x) return_failure{long}
{
  return try (f (x));
}

/* { dg-final { scan-assembler "cset\\tw\[0-9\]+, cs" } } */
/* { dg-final { scan-assembler "cmp\\tw\[0-9\]+, #1" } } */
