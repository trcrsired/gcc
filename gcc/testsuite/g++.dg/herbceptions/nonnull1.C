// { dg-do compile { target c++17 } }
// { dg-options "-fherbceptions -fno-exceptions" }
// Port of LLVM herbceptions-returns-nonnull: [[returns_nonnull]]
// applies to the success payload of a spec'd function — the channel
// wrapper keeps the attribute meaningful for callers.
extern "C" void *malloc (__SIZE_TYPE__);

__attribute__ ((returns_nonnull)) void *
alloc_like (__SIZE_TYPE__ n) throws
{
  return malloc (n);
}

__attribute__ ((returns_nonnull)) void *
alloc_plain (__SIZE_TYPE__ n)
{
  return malloc (n);
}

void
use () throws
{
  (void) alloc_like (8);
  (void) alloc_plain (8);
}
