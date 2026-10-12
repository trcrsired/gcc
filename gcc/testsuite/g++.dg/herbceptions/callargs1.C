// { dg-do run { target { *-*-linux* } } }
// { dg-options "-fherbceptions -fno-exceptions -O0" }
// The failure discriminant is carried in CF, so the caller must read it
// immediately after the call.  With more than six integer arguments the
// callee is followed by a stack-argument pop (`add rsp, N'), which
// clobbers CF; reading the flag after the pop silently drops failures.

struct E { int e; };

__attribute__ ((noinline)) long
callee (long a, long b, long c, long d, long e, long g, long h, long i)
  return_failure{E}
{
  if (a < 0)
    return_failure E{(int) -a};
  return a + b + c + d + e + g + h + i;
}

int
main ()
{
  auto ok = catch return_failure (callee (1, 2, 3, 4, 5, 6, 7, 8));
  if (ok.failed)
    return 1;
  if (ok.value != 36)
    return 2;

  auto bad = catch return_failure (callee (-9, 0, 0, 0, 0, 0, 0, 0));
  if (!bad.failed)
    return 3;
  if (bad.error.e != 9)
    return 4;

  return 0;
}
