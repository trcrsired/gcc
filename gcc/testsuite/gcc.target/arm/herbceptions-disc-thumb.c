/* { dg-do compile } */
/* { dg-options "-fherbceptions -fno-exceptions -O0 -mthumb -march=armv7-a -mfpu=vfpv3-d16" } */

/* The Thumb-2 half of the ARM herbceptions discriminant, which lives in
   CPSR.C (see herbceptions-disc.c).  The extra requirement here is that
   nothing may write the flags between the callee's `cmp' and the return,
   and the Thumb-2 frame-pointer teardown is exactly the place where a
   flag-setting instruction can slip in: bumping the frame pointer would
   take the 16-bit `adds r7, r7, #amount', which overwrites the carry the
   caller is waiting for.  The epilogue therefore steps the stack pointer
   over without touching the flags, which is what the negative scan
   below pins down.  Mirrors LLVM's throws-csr-disc.ll, where the
   discriminant write also has to precede the callee-saved `pop'.  */

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

/* The callee sets C from the zero-extended slot.  */
/* { dg-final { scan-assembler "cmp\\tr\[0-9\]+, #1" } } */
/* The caller materializes C straight after the call with `sbc' and then
   adds the borrow back; Thumb-2 spells the add `adds'.  */
/* { dg-final { scan-assembler "bl\\text" } } */
/* { dg-final { scan-assembler "sbc\\tr\[0-9\]+, r\[0-9\]+, r\[0-9\]+" } } */
/* { dg-final { scan-assembler "adds\\tr\[0-9\]+, r\[0-9\]+, #1" } } */
/* The teardown preserves the flags: the SP is moved off the frame
   pointer with `mov sp, r7' and a wide `add sp, sp, #...', never the
   flag-setting `adds r7, r7, #...'.  */
/* { dg-final { scan-assembler "mov\\tsp, r7" } } */
/* { dg-final { scan-assembler-not "adds\\tr7, r7, #" } } */
