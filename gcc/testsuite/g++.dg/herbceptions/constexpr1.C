// { dg-do compile }
// { dg-options "-fherbceptions -fno-exceptions -std=c++23" }
// Herbceptions in constant evaluation: `return_failure{E}' failures
// and `catch return_failure' must fold to the {value,error,failed}
// either at compile time.

struct E { int code; };

constexpr int f (int x) return_failure{E}
{
  if (x < 0)
    return_failure E{-x};
  return x * 2;
}

constexpr int gv (int x)
{
  auto e = catch return_failure (f (x));
  return e.value;
}
constexpr int gf (int x)
{
  auto e = catch return_failure (f (x));
  return e.failed ? 1 : 0;
}
constexpr int ge (int x)
{
  auto e = catch return_failure (f (x));
  return (int) e.error.code;
}
static_assert (gv (5) == 10);
static_assert (gf (5) == 0);
static_assert (ge (5) == 0);
static_assert (gv (-3) == 0);
static_assert (gf (-3) == 1);
static_assert (ge (-3) == 3);
