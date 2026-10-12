// { dg-do run { target { *-*-linux* } } }
// { dg-options "-fherbceptions -fno-exceptions" }
// Port of LLVM herbceptions-catch-fails-indirect: an sret-valued
// return_failure{E} call through catch return_failure binds both
// the materialized value and the wide error record.
struct Big { long long a, b, c, d; };
struct E2 { long long x, y; };

Big
mkbig (int n) return_failure{E2}
{
  if (n < 0)
    return_failure E2{-n, -n - 1};
  return {n, n + 1, n + 2, n + 3};
}

long long
foo (int x)
{
  auto e = catch return_failure (mkbig (x));
  return !e.failed ? e.value.d : e.error.x;
}

int
main ()
{
  return (foo (0) == 3 && foo (-7) == 7) ? 0 : 1;
}
