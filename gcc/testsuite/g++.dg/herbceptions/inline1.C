// { dg-do run { target { *-*-linux* } } }
// { dg-options "-fherbceptions -fno-exceptions -O2" }
// Inlining a channel-returning callee must forward its payload and
// failure flag to the caller.  The caller's channel reads
// (IFN_HERBCEPTION_RESULT / IFN_HERBCEPTION_FAILED) sample the ABI
// return registers and the carry flag, which no longer exist once the
// callee body is spliced in, so the inliner must redirect those reads
// to the inlined callee's remapped __hb_result / __hb_failed slots.
// `chain' is small enough to inline its two `add2' calls at -O2, which
// is exactly where the payload used to be dropped.
// Regression test for bugs/gcc_herbceptions/inlining-breaks-channel.

struct E { int e; };

int
add2 (int x) return_failure{E}
{
  if (x < 0)
    return_failure E{-x};
  return x + 2;
}

int
chain (int x) return_failure{E}
{
  int a = try (add2 (x));
  int b = try (add2 (a));
  return b - x;		/* == 4 on success */
}

int
main ()
{
  auto ok = catch return_failure (chain (1));
  if (ok.failed)
    return 1;
  if (ok.value != 4)
    return 2;

  auto bad = catch return_failure (chain (-3));
  if (!bad.failed)
    return 3;
  if (bad.error.e != 3)
    return 4;

  return 0;
}
