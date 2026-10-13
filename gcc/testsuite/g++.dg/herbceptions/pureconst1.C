// { dg-do run { target { *-*-linux* } } }
// { dg-options "-O2 -fherbceptions -fno-exceptions" }
// A herbceptions-spec'd function hands part of its result back out of
// band, in the carry flag, rather than in its return value.  The compiler
// cannot see that output through the function's type, so such a function
// must not be treated as `const' or `pure': otherwise DCE deletes calls
// whose only consumer is the following .HERBCEPTION_RESULT () /
// .HERBCEPTION_FAILED (), and the caller reads an undefined channel.
//
// This is what made fast_io's posix_clock_id_to_native_value (marked
// [[gnu::pure]]) hand a garbage clock id to clock_gettime, so
// fast_io::timer threw EINVAL and every benchmark using it died.
//
// Regression test for bugs/gcc_herbceptions/pure-herbceptions-dce.
//
// (clang can honour const/pure here because it puts the discriminant in
// the return value, {union{T,E}, i1}; GCC's discriminant is in CF.)

namespace std
{
  struct error
  {
    const void *domain;
    __UINTPTR_TYPE__ code;
  };
}

[[noreturn]] inline void
throw_err (int e) throws
{
  throw throws (std::error{(const void *) 0x1, (unsigned long) e});
}

[[gnu::const]] inline int
to_native_const (int pcid) throws
{
  switch (pcid)
    {
    case 0: return 0;
    case 4: return 1;
    case 6: return 4;
    default: throw_err (22);
    };
}

[[gnu::pure]] inline int
to_native_pure (int pcid) throws
{
  switch (pcid)
    {
    case 0: return 0;
    case 4: return 1;
    case 6: return 4;
    default: throw_err (22);
    };
}

int
main (int argc, char **)
{
  /* Opaque to the constant folder, so the calls really happen.  */
  int id = argc > 0 ? 6 : 0;
  if (to_native_const (id) != 4)
    return 1;
  if (to_native_pure (id) != 4)
    return 2;
  return 0;
}
