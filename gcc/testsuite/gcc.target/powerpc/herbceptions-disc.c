/* { dg-do compile } */
/* { dg-options "-fherbceptions -fno-exceptions -O0" } */

/* The PowerPC herbceptions error-channel discriminant lives in cr6.GT
   (bit 25), a field the ABI leaves volatile, so no save and restore is
   needed around a call.  The callee has to set the field from its failure
   flag on every return edge, and the caller has to extract it before
   anything else can rewrite the field.  See
   llvm/test/CodeGen/PowerPC/throws-attr.ll: a compare into cr6 sits
   immediately before blr and the caller runs mfcr.  GCC prints the field
   numerically, so cr6 is spelled "6" in the assembly.  */

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

/* The callee compares the failure flag into cr6 ...  */
/* { dg-final { scan-assembler "cmp\[wd\]i 6," } } */
/* ... and the caller extracts cr6.GT straight after the call.  */
/* { dg-final { scan-assembler "bl ext" } } */
/* { dg-final { scan-assembler "mfcr \[0-9\]" } } */
