// Herbceptions: a register-returning `throws` call materialised through
// a TARGET_EXPR (a template with a requires-clause) must still deliver
// its value from the channel, not an unwritten slot.
// { dg-do run { target { *-*-linux* } } }
// { dg-options "-fherbceptions -fno-exceptions -O0" }

namespace std { struct error { const void *domain; __UINTPTR_TYPE__ code; }; }

struct S { long a; long b; };

template <typename T>
[[gnu::cold]] S cold (T x) throws
{
  if (x < 0)
    throw throws (std::error { (const void *) 1, 2 });
  return S { x, x + 1 };
}

template <typename T>
S outer (T x) throws
  requires (sizeof (T) > 0)
{
  return cold (x);
}

int main ()
{
  S s = outer (5L);
  if (s.a != 5 || s.b != 6)
    return 1;
  bool caught = false;
  try
    {
      S t = outer (-1L);
      (void) t;
    }
  catch throws (::std::error e)
    {
      caught = true;
    }
  return caught ? 0 : 2;
}
