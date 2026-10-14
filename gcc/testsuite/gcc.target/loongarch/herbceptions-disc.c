/* { dg-do compile } */
/* { dg-options "-fherbceptions -fno-exceptions -O0" } */

/* The LoongArch herbceptions error-channel discriminant is a nominal
   register leaf rather than a condition flag: LoongArch has none.  The
   payload comes back where the base ABI puts it -- a0, or a0:a1 for
   values wider than GRLen -- and the discriminant in a2 ($r6), which is
   an argument register but call-clobbered, so no live value can be
   parked there across a call.  A function's failure flag is an ordinary
   stack slot during the body (calls and scope-exit cleanups freely
   clobber a2) and is only moved into a2 on the shared return edge, so
   the callee stores it with `ld.bu $r6,' just before the epilogue.  The
   caller snapshots it with `andi ..., $r6, 1' immediately after the
   call, before the call's result is materialized.  Mirrors LLVM's
   llvm/test/CodeGen/LoongArch/throws-attr.ll, which reads the leaf with
   `andi $a1, $a2, 1'.  */

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

/* The callee stores the failure flag into a2 (or ANDs it in when the
   slot was promoted to a register) ...  */
/* { dg-final { scan-assembler "ld\.bu\\t\[\$]r6," } } */
/* ... and the caller reads a2 straight after the call.  */
/* { dg-final { scan-assembler "andi\\t\[\$]r\[0-9\]+,\[\$]r6,1" } } */
