// { dg-do compile { target { i?86-*-* x86_64-*-* } } }
// { dg-options "-fherbceptions -fno-exceptions -O2" }
// Two recursive calls whose results are summed: the failure flag merges in
// an empty block in front of the return, and whether the first call failed
// is only known on one of the two paths that reach that block.  The
// return-flag folding pass must split the merge block per incoming edge,
// then write the flag directly on every edge (1 from each failure path, 0
// from the `n <= 2' return and the recursive sum).  A shared slot reload
// would leave the `negb' write in place.

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

unsigned
caller (unsigned n) noexcept
{
  try
    {
      return doFib (n, 10);
    }
  catch throws (std::error)
    {
      return 0;
    }
}

// The two failure edges and the success edges.
// { dg-final { scan-assembler "stc" } }
// { dg-final { scan-assembler "clc" } }
// { dg-final { scan-assembler-not "negb" } }
