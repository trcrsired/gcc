// { dg-do run { target { *-*-linux* } } }
// { dg-options "-fherbceptions -fno-exceptions" }
// A `throws' function must not sibling-call a callee that produces no
// herbceptions discriminant: the tail call skips the caller's own
// failure-flag materialisation, so the caller's success edge inherits
// whatever the callee left in the flag register.  Here `inner' (throws)
// would tail-call `cb' (noexcept), whose stale carry flag then made
// main take its catch edge.
#include <cstdlib>

namespace std { struct error { const void *domain; __UINTPTR_TYPE__ code; }; }

[[gnu::noinline]] void
fallible (int fail) throws
{
  if (fail)
    throw throws (std::error{(const void *) 1, 5});
}

[[gnu::noinline]] void
cb () noexcept
{
  try
    {
      fallible (1);
    }
  catch throws (std::error)
    {
    }
}

[[gnu::noinline]] void
inner () throws
{
  cb ();
}

int
main ()
{
  try
    {
      inner ();
    }
  catch throws (std::error)
    {
      return 1;
    }
  return 0;
}
