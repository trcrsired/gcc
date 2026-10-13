// { dg-do run { target { *-*-linux* } } }
// { dg-options "-O2 -g -fherbceptions -fno-exceptions" }
// A `throws' function whose success value is delivered elsewhere (here an
// aggregate returned in memory, so the payload never goes through the
// channel union) may never write its own __hb_result in the optimized
// body.  remove_unused_locals then dropped the slot from the function's
// local_decls.
//
// The slot is nevertheless part of the calling convention: the caller
// reads the failure payload with .HERBCEPTION_RESULT (), and once the
// callee is inlined that read is redirected to the inlined copy of the
// callee's slot.  Because the source slot had already been dropped from
// local_decls, add_local_variables never added the copy to the caller
// either, so the redirected read referred to a declaration outside
// local_decls.  expand_used_vars only allocates declarations found in
// local_decls - clear_tree_used zeroes TREE_USED on every other block
// variable and only local_decls entries get it back - so the declaration
// reached RTL expansion without any RTL and make_decl_rtl aborted.
//
// This is what made fast_io's async print path ICE with `-g' (the
// immediately-invoked lambda at io_buffer/print_async.h:193 returns its
// string payload in memory) while silently producing zeros without `-g'.
//
// Regression test for bugs/gcc_herbceptions/inlined-channel-slot-not-local.
//
// Note: the sret value itself is deliberately not checked.  `mid' returns
// an aggregate in memory and that value is currently dropped by a separate
// pre-existing bug (the callee writes nothing and the caller reads an
// unwritten buffer); the point of this test is that the compiler no longer
// crashes and that the channel machinery is intact.

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
  char c[64];
};

/* Never fails, and returns by memory, so __hb_result is only ever written
   on a failure path that does not exist.  */
inline big
mid (int x) throws
{
  big b{};
  b.c[0] = (char) x;
  return b;
}

int
main (int argc, char **)
{
  big b = mid (argc);
  (void) b;
  return 0;
}
