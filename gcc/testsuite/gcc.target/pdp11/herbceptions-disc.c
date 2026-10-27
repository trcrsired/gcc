/* { dg-do compile } */
/* { dg-options "-fherbceptions -fno-exceptions -O0" } */

/* The PDP-11 herbceptions error-channel discriminant is the carry bit,
   the flag that the arithmetic instructions maintain.  The payload comes
   back in R0:R1 (a 32-bit channel) and the discriminant in C, which
   `rts pc' leaves alone.  The callee sets C from its failure slot with a
   byte negate -- NEG sets C iff the operand is nonzero -- and the caller
   materializes C with `movb $0,r' (MOV does not touch C, unlike CLR)
   followed by `adcb', before the argument-popping `add' can rewrite it.
   The epilogue deallocates with `mov r5,sp' rather than an `add', so the
   flag survives the frame teardown.  */

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

/* The callee sets C from the flag slot ...  */
/* { dg-final { scan-assembler "\\tnegb" } } */
/* ... the caller reads it back straight after the call ...  */
/* { dg-final { scan-assembler "\\tadcb" } } */
/* ... and the frame teardown preserves it.  */
/* { dg-final { scan-assembler "mov\tr5,sp" } } */
