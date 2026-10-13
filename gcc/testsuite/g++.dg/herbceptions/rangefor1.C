// { dg-do run { target { *-*-linux* } } }
// { dg-options "-O2 -fherbceptions -fno-exceptions" }
// A call the parser never auto-wrapped reaches hb_lower_try as a plain
// CALL_EXPR whose declared return type is in memory.  build_range_exp
// emits the range-for's internal `__for_begin = begin (__range)' as a
// bare MODIFY_EXPR, so there is no TARGET_EXPR and no AGGR_INIT_EXPR
// slot.  Evaluated as a bare expression the callee was handed no return
// buffer at all -- gimplify allocated a throwaway one -- and __for_begin
// kept whatever it already held, so the first iteration of the loop read
// an unwritten entry.  The call has to be materialised into the
// channel-local, exactly like an AGGR_INIT_EXPR.
//
// fast_io's `for (auto const &e : current (at))' directory listing hit
// this: the first posix_directory_entry came out garbage (a stale
// d_name pointer) and is_dot() dereferenced it, segfaulting
// examples/0017.network/async/http-server.cc on any directory request.
//
// Regression test for bugs/gcc_herbceptions/refreturn-range-for.

namespace std
{
  struct error
  {
    const void *domain;
    __UINTPTR_TYPE__ code;
  };
}

struct itr
{
  unsigned long a;
  unsigned long b;
  unsigned long c;
};

struct gen
{
  itr dummy;
};

struct sent
{
};

inline itr
operator* (itr i) noexcept
{
  return i;
}

inline itr &
operator++ (itr &i) throws
{
  i.a = 0x11;
  i.b = 0x22;
  i.c = 3;
  return i;
}

inline itr
begin (gen const &) throws
{
  itr i{};
  ++i;
  return i;
}

inline sent
end (gen const &) noexcept
{
  return {};
}

inline bool
operator!= (itr const &i, sent) noexcept
{
  return i.a != 0;
}

int
main ()
{
  gen g{};

  int n = 0;
  for (auto const &e : g)
    {
      if (e.a != 0x11 || e.b != 0x22 || e.c != 3)
	return 1;
      if (++n > 2)
	break;
    }
  if (n != 3)
    return 2;

  return 0;
}
