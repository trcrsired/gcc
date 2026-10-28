/* { dg-do compile } */
/* { dg-options "-fherbceptions -fno-exceptions -O2 -fno-inline" } */
/* At -O2 the callee `f' has a constant discriminant on both return
   edges, so the return-flag folding pass writes NZCV.C directly with
   `cmn wzr, wzr' (success, C = 0) and `cmp wzr, wzr' (failure, C = 1)
   instead of materialising the value and re-loading it.  The
   `(use (reg:CC_C ...))' glued into the return keeps NZCV.C live across
   the flag-preserving epilogue.  `g' branches directly on the carry that
   `f' returned, and its own return edges are constant too, so it has no
   `cset' capture and no reload of the captured byte either.  This used to
   ICE with `unrecognizable insn' because herbceptions_load_failed required
   a memory operand.  `f' is called rather than inlined so that the
   call/return sequence survives: -fno-inline is part of the test.  */

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

/* The folded constant writes ...  */
/* { dg-final { scan-assembler "cmn\\twzr, wzr" } } */
/* { dg-final { scan-assembler "cmp\\twzr, wzr" } } */
/* ... `g' branches on the carry straight from the call ...  */
/* { dg-final { scan-assembler "bcs\\t" } } */
/* ... and no captured byte is compared.  */
/* { dg-final { scan-assembler-not "cmp\\tw[0-9]+, #1" } } */
