/* { dg-do compile } */
/* { dg-options "-fherbceptions -fno-exceptions -O2" } */
/* A caller reads the carry-flag failure discriminant by materializing it
   with `cset Wd, cs' and then testing bit 0 of the byte.  `cset ..., cs'
   produces exactly 0 or 1 and reads but does not write NZCV, so
   `tbnz Wd, #0' is a verbose `bcs': fold the pair into a branch on the
   carry flag.  The fold is a peephole, so it runs after prologue/epilogue
   threading, and it requires the two insns to be adjacent so that nothing
   has redefined NZCV in between.  It mirrors the x86 setc/testb/jcc fold
   and LLVM's X86HerbceptionsFold machine pass.

   Note that `-fpeephole2' is only on from `-O2', so `-O0'/`-O1' never see
   the fold; that is why this test is pinned at `-O2'.

   The callee is left undefined so that a real call survives: with the
   callee visible the discriminant is promoted to a register and no `cset'
   is emitted at all.  The success path does something the failure path
   does not -- `+ 1' -- so that the test of the discriminant cannot be
   optimized away as redundant.  At -O2 the return-flag folding pass also
   turns this function's own return into constant NZCV.C writes, so once
   the branch is folded the `cset' has no remaining user and disappears;
   the branch on the carry flag must still be there.  */

int callee (int) return_failure{long};

int
caller (int x) return_failure{long}
{
  return try (callee (x)) + 1;
}

/* The `cset' may be dead once the return flag is folded, but the branch
   tests the carry flag directly, rather than the byte.  */
/* { dg-final { scan-assembler "bcs\\t" } } */
/* { dg-final { scan-assembler-not "tbnz\\tw\[0-9\]+, 0" } } */
