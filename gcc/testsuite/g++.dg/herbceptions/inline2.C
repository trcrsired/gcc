// { dg-do run { target { *-*-linux* } } }
// { dg-options "-fherbceptions -fno-exceptions -O2" }
// The inliner forwards a channel-returning callee's payload and failure
// flag to the caller's trailing IFN_HERBCEPTION_RESULT /
// IFN_HERBCEPTION_FAILED reads.  DCE drops the lhs of one of those reads
// when only the other value is used, leaving a bare `.HERBCEPTION_RESULT ()';
// the forwarding loop must skip such a dead read rather than stop, or the
// following live `.HERBCEPTION_FAILED ()' keeps sampling the deleted
// callee epilogue's carry flag and a real failure is silently lost.
// `f' inlines `make' at -O2; the payload read after the inlined `f' is
// dead (only the flag is used), which used to end the scan early.
// Regression test for bugs/gcc_herbceptions/inline-throws-channel-lost.

#include <cstdio>

namespace std { struct error { const void *domain; __UINTPTR_TYPE__ code; }; }

int alive = 0;

struct S
{
  int v;
  S (int x) : v (x) { ++alive; }
  S (const S &o) : v (o.v) { ++alive; }
  ~S () { --alive; }
};

S
make (int x) throws
{
  if (x < 0)
    throw throws (std::error{(const void *) 0x1234, 7});
  return S (x);
}

S
f (int x) throws
{
  S s = make (x);
  return s;
}

int
main ()
{
  { S a = f (42); if (a.v != 42) return 1; }
  /* printf (an opaque call) keeps the destructor bookkeeping live so
     the inlined `f (-1)' channel probe is not constant-folded away.  */
  if (alive != 0) { std::printf ("leak after success: %d\n", alive); return 2; }

  bool caught = false;
  try { S b = f (-1); (void) b; }
  catch throws (::std::error e) { caught = true; }
  if (!caught)
    return 3;			/* the failure flag was lost */

  if (alive != 0) { std::printf ("leak after failure: %d\n", alive); return 4; }
  return 0;
}
