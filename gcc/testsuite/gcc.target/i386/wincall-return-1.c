/* WinCall returns aggregates of up to 32 bytes in RAX, RDX, RCX, R8.
   The last pair, RCX:R8, is not a consecutive register pair, and the
   backend used to describe the second half of the value as a TImode at
   CX -- which names RCX:RBX.  RBX is callee-saved, so the callee stuffed
   the fourth eight-byte chunk into it, restored RBX on the way out and
   the chunk was gone; every 25..32 byte return came back with its last
   eight bytes clobbered.  Sizes 17..24 need only three chunks and were
   unaffected, so cover the whole range.  */

/* { dg-do run { target { ! ia32 } } } */
/* { dg-options "-O2" } */

extern void abort (void);

#define DEF(N)							\
  struct S##N { char b[N]; };					\
  __attribute__ ((wincall, noinline, noipa)) struct S##N	\
  make##N (void)						\
  {								\
    struct S##N s;						\
    for (int i = 0; i < N; i++)					\
      s.b[i] = (char) (i + 1);					\
    return s;							\
  }								\
  __attribute__ ((wincall, noinline)) void			\
  check##N (void)						\
  {								\
    struct S##N s = make##N ();					\
    for (int i = 0; i < N; i++)					\
      if (s.b[i] != (char) (i + 1))				\
	abort ();						\
  }

DEF (17) DEF (18) DEF (19) DEF (20)
DEF (21) DEF (22) DEF (23) DEF (24)
DEF (25) DEF (26) DEF (27) DEF (28)
DEF (29) DEF (30) DEF (31) DEF (32)

/* An empty by-value record takes no register slot, so the argument
   after it is still passed as if it were first.  Pair it with an
   aggregate so a wrongly consumed slot would show up.  */
struct empty { };
struct S16 { long a, b; };

__attribute__ ((wincall, noinline, noipa)) struct S16
after_empty (struct empty e, struct S16 s, long x, long y, long z)
{
  (void) e;
  s.a += x + y + z;
  return s;
}

__attribute__ ((wincall, noinline)) void
check_empty (void)
{
  struct empty e;
  struct S16 s = after_empty (e, (struct S16) { 1, 2 }, 10, 20, 30);
  if (s.a != 61 || s.b != 2)
    abort ();
}

int
main (void)
{
  check17 (); check18 (); check19 (); check20 ();
  check21 (); check22 (); check23 (); check24 ();
  check25 (); check26 (); check27 (); check28 ();
  check29 (); check30 (); check31 (); check32 ();
  check_empty ();
  return 0;
}
