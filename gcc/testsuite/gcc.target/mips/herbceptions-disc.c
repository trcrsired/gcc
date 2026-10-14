/* { dg-do compile } */
/* { dg-options "-fherbceptions -fno-exceptions -O0" } */

/* The MIPS herbceptions error-channel discriminant is a nominal register
   leaf rather than a condition flag: MIPS has none.  The payload comes
   back where the ABI puts it -- $2:$3 -- and the discriminant in $4
   ($a0), which is an argument register but call-clobbered, so no live
   value can be parked there across a call.  A function's failure flag is
   an ordinary stack slot during the body (calls and scope-exit cleanups
   freely clobber $4) and is only moved into $4 on the shared return
   edge, so the callee loads it with `lbu $4,' just before the epilogue.
   The caller snapshots it with `andi ..., $4, 1' immediately after the
   call.  Mirrors LLVM's llvm/test/CodeGen/Mips/throws-attr.ll, which
   reads the leaf with `andi $1, $4, 1'.  */

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

/* The callee loads the failure flag into $4 ...  */
/* { dg-final { scan-assembler "lbu\\t\[\$]4," } } */
/* ... and the caller reads $4 straight after the call.  */
/* { dg-final { scan-assembler "jal\\text" } } */
/* { dg-final { scan-assembler "andi\\t\[\$]\[0-9\]+,\[\$]4,1" } } */
