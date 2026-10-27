// { dg-do run { target { *-*-linux* } } }
// { dg-options "-fherbceptions -fno-exceptions -O2" }
// Behavioural counterpart of flagreturnfib1.C: the split return block must
// still report success on the summed path and the second call's failure
// must still propagate, exactly as the shared slot reload did.

namespace std { struct error { const void *domain; __UINTPTR_TYPE__ code; }; }

static unsigned
doFib (unsigned n, unsigned maxDepth) throws __attribute__ ((noinline));

static unsigned
doFib (unsigned n, unsigned maxDepth) throws
{
  if (!maxDepth)
    throw throws std::error{nullptr, 33};
  if (n <= 2)
    return 1;
  return doFib (n - 2, maxDepth - 1) + doFib (n - 1, maxDepth - 1);
}

int
main ()
{
  try
    {
      unsigned v = doFib (10, 20);
      if (v != 55)
	return 1;
    }
  catch throws (std::error)
    {
      return 2;
    }

  // maxDepth runs out before the recursion bottoms out: the fifth level
  // throws, so the failure has to travel back through the sums.
  try
    {
      doFib (10, 3);
      return 3;
    }
  catch throws (std::error e)
    {
      return e.code == 33 ? 0 : 4;
    }
}
