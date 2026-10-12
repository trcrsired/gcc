// { dg-do run { target { *-*-linux* } } }
// { dg-options "-fherbceptions -fno-exceptions" }
// Bare `return_failure;' carries a value-initialized E through the
// channel — the operand-less form must not crash the lowering.

namespace std { struct error { const void *domain; __UINTPTR_TYPE__ code; }; }

int
f (int x) return_failure{int}
{
  if (x)
    return_failure;
  return x;
}

int
g () throws
{
  return_failure;
}

int
main ()
{
  auto a = catch return_failure (f (0));
  if (a.failed || a.value != 0)
    return 1;
  auto b = catch return_failure (f (5));
  if (!b.failed || b.error != 0)
    return 2;
  try
    {
      g ();
      return 3;
    }
  catch throws (std::error e)
    {
      return (e.domain != 0 || e.code != 0) ? 4 : 0;
    }
}
