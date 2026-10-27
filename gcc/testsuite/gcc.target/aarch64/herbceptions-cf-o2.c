/* { dg-do compile } */
/* { dg-options "-fherbceptions -fno-exceptions -O2 -fno-inline" } */
/* At -O2 the callee `f' has a constant discriminant on both return
   edges, so the return-flag folding pass writes NZCV.C directly with
   `cmp wzr, #1' (success, C = 0) and `cmp wzr, #0' (failure, C = 1)
   instead of materialising the value and re-loading it.  The
   `(use (reg:CC_C ...))' glued into the return keeps NZCV.C live across
   the flag-preserving epilogue.  `g' propagates the discriminant returned
   by `f', which is not a constant, so its reload stays and compares the
   captured register against 1.  This used to ICE with `unrecognizable
   insn' because herbceptions_load_failed required a memory operand.
   `f' is called rather than inlined so that the `cset' capture in `g'
   survives: -fno-inline is part of the test.  */

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
/* The folded constant writes ...  */
/* { dg-final { scan-assembler "cmp\\twzr, #0" } } */
/* { dg-final { scan-assembler "cmp\\twzr, #1" } } */
/* ... and the non-constant reload in `g'.  */
/* { dg-final { scan-assembler "cmp\\tw\[0-9\]+, #1" } } */
