// { dg-do run { target { *-*-linux* } } }
// { dg-options "-fherbceptions -fno-exceptions -O2" }

// Runtime check of the folded discriminant test: the caller branches on
// the carry flag it captured from the callee, so both the success and the
// failure path have to reach the right place.
//
// `good' is `noinline' and `noipa' so that the call, and with it the
// `setc' the branch is folded onto, survives optimization.

namespace std { struct error { const void *domain; __UINTPTR_TYPE__ code; }; }

extern "C" void abort (void);

__attribute__ ((noinline, noipa)) int
good (int c) throws
{
  if (c < 0)
    throw throws (::std::error{nullptr, (__UINTPTR_TYPE__) -c});
  return c + 1;
}

__attribute__ ((noinline)) int
f (int c) throws
{
  return good (c) + 1;
}

int main ()
{
  try {
      if (f (5) != 7)
	abort ();
  } catch throws (::std::error e) {
    abort ();
  }

  try {
      f (-3);
      abort ();
  } catch throws (::std::error e) {
    if ((int) e.code != 3)
      abort ();
  }

  return 0;
}
