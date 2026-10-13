// { dg-do run { target { *-*-linux* } } }
// { dg-options "-O2 -fherbceptions -fno-exceptions" }
// A `throws' function that returns a value in memory (sret) must hand the
// result back through the caller's slot.  The front end applies the return
// slot optimization for aggregates, so the RETURN_EXPR it hands to the
// lowering already carries DECL_RESULT as its operand, and the value is
// written through it by the preceding statements.
//
// hb_lower_return turned that into a value-less `return;'.  A bare return
// leaves the sret buffer looking like an ordinary local that nothing ever
// reads, so GIMPLE DSE (`048t.dse1') and CD-DCE (`049t.cddce1') dropped the
// write-through and the caller read whatever the slot happened to hold --
// for fast_io's native_fstatat() that meant a zeroed or garbage
// posix_file_status, which made every file request in its http-server
// example fail as 403 Forbidden.
//
// The second half covers the other shape the front end uses for a material-
// ized aggregate call: a deduced-`auto' return forwarding another call (a
// generic wrapper, a lambda's operator(), fast_io's posix_api_common chain)
// arrives as a plain CALL_EXPR inside a TARGET_EXPR.  That only works while
// the call is the TARGET_EXPR's initializer -- replacing the initializer
// with a void COMPOUND_EXPR made gimplify_target_expr take its "the mere
// evaluation initialises the slot" path and the callee was handed no return
// buffer at all.
//
// Regression test for bugs/gcc_herbceptions/sret-throws-value-dropped.

namespace std
{
  struct error
  {
    const void *domain;
    __UINTPTR_TYPE__ code;
  };
}

struct big
{
  unsigned long v[20];
  unsigned t;
};

/* Returned in memory, so the payload never goes through the channel
   union and __hb_result is only written on failure paths.  */
inline big
impl (int x) throws
{
  big b{};
  b.v[0] = (unsigned long) x;
  b.t = 7;
  return b;
}

/* Deduced return type: the front end materialises impl() into a temporary
   and copies it to the result slot.  */
template <typename F>
inline auto
via (F f) throws
{
  return f ();
}

inline auto
outer (int x) throws
{
  return via ([&]() throws { return impl (x); });
}

int
main ()
{
  big a = impl (3);
  if (a.t != 7 || a.v[0] != 3)
    return 1;

  big b = outer (4);
  if (b.t != 7 || b.v[0] != 4)
    return 2;

  return 0;
}
