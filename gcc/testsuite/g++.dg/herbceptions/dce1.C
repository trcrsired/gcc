// { dg-do run { target { *-*-linux* } } }
// { dg-options "-fherbceptions -fno-exceptions -O2 -fstack-protector-all" }
// When only the failure discriminant of a `catch return_failure` is used,
// DCE drops the payload lhs but keeps the HERBCEPTION_RESULT call.  The
// expander used to assert on the null lhs (-O2 -fstack-protector-all).

struct E { int e; };

__attribute__ ((noinline)) int
f (int c) return_failure{E}
{
  if (c < 0)
    return_failure E{-c};
  return c;
}

int
main ()
{
  auto ok = catch return_failure (f (5));
  if (ok.failed)
    return 1;

  auto bad = catch return_failure (f (-5));
  if (!bad.failed)
    return 2;

  return 0;
}
