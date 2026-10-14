/* { dg-do compile } */
/* { dg-options "-fherbceptions -fno-exceptions -O0 -marm" } */

/* The ARM herbceptions error-channel discriminant is carried in CPSR.C
   rather than in a carrier register: ARM has no register left over for
   it, and the flag survives an epilogue made only of `pop'/`ldr'/`bx lr'
   and the SP teardown, none of which write CPSR.  The callee writes the
   flag with `cmp <zero-extended slot>, #1' as the last thing before the
   epilogue; the caller turns it back into a value with `sbc rX, rX, rX'
   (which is -(not C)) followed by `add rX, rX, #1'.  Mirrors LLVM's
   llvm/test/CodeGen/ARM/throws-attr.ll, where the callee does the
   equivalent `subs rx, rx, #1' and the caller selects straight off
   CPSR.C.  See herbceptions-disc-thumb.c for the Thumb-2 side, where
   the frame teardown has to be arranged not to write the flags.  */

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

/* The callee zero-extends the slot (a boolean only defines its low
   byte, while `cmp' is a word operation) and sets C from it ...  */
/* { dg-final { scan-assembler "cmp\\tr\[0-9\]+, #1" } } */
/* ... and the caller materializes C straight after the call.  */
/* { dg-final { scan-assembler "bl\\text" } } */
/* { dg-final { scan-assembler "sbc\\tr\[0-9\]+, r\[0-9\]+, r\[0-9\]+" } } */
/* { dg-final { scan-assembler "add\\tr\[0-9\]+, r\[0-9\]+, #1" } } */
