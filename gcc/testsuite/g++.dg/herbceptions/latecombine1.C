// { dg-do run { target { *-*-linux* } } }
// { dg-options "-O2 -fherbceptions -fno-exceptions" }
// On x86 the failure capture of a call that fails is a partial write to
// the single-byte register %al (a setcc of the carry flag).  When the
// return value of a preceding call has been copied to a callee-saved
// register, RTL late-combine must not replace later uses of that copy
// with the original return register: the source register may already
// have been clobbered by the failure capture (or by an intervening
// call), and a write in a narrower mode invalidates the wider value in
// the same register.
// Regression test for bugs/gcc_herbceptions/lto-setcc-clobbers-return.

namespace std
{
  struct error
  {
    const void *domain;
    __UINTPTR_TYPE__ code;
  };
}

extern "C" int printf (const char *, ...);

long
inner (int x) throws
{
  if (x < 0)
    throw throws (std::error{(const void *) 0x1234, 7});
  return x + 100;
}

long
outer (int x) throws
{
  return inner (x);
}

int
main ()
{
  long r = outer (5);
  printf ("%ld\n", r);
  return r != 105;
}
