// { dg-do run { target { *-*-linux* } } }
// { dg-options "-fherbceptions -fno-exceptions -O3" }
// IPA-SRA / IPA-CP must not change the signature of a herbceptions
// function: the payload travels through the error channel and the
// architecture's return registers, and a signature rewrite makes the
// caller's .HERBCEPTION_RESULT read a register the call insn no longer
// declares as an output.  The observed failure was the payload being
// clobbered by the success-path return value (e.g. `cmpl $3, %eax'
// comparing the constant 10 with 3 after the clone dropped the return).

struct E { int e; };

__attribute__ ((noinline)) long
f (long a0, long a1, long a2, long a3, long a4, long a5, long a6, long a7)
  return_failure{E}
{
  if (a0 < 0)
    return_failure E{(int) -a0};
  return a0;
}

int
main ()
{
  auto bad = catch return_failure (f (-3, 2, 3, 4, 5, 6, 7, 8));
  if (!bad.failed)
    return 1;
  if (bad.error.e != 3)
    return 2;

  auto ok = catch return_failure (f (5, 2, 3, 4, 5, 6, 7, 8));
  if (ok.failed)
    return 3;
  if (ok.value != 5)
    return 4;

  return 0;
}
