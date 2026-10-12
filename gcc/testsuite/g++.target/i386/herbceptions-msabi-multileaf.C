// { dg-do run { target { x86_64-*-* } } }
// { dg-options "-fherbceptions -fno-exceptions -std=c++26 -ffreestanding -mabi=ms -O0" }

// Win64 (MS ABI) herbceptions: a trivially-copyable <=16-byte payload is
// returned as two i64 register leaves plus the discriminant, regardless of
// the source type's field/array shape.  A type whose raw type would decompose
// into more than the return-register budget (e.g. { empty, unsigned long long }
// or { int, int, int, int }) still travels in RAX:RDX with the discriminant in
// CF.  Port of clang's herbceptions-windows-multileaf-return.cpp; regression
// test for bugs/llvm_herbceptions/throws-multileaf-payload-crash.
//
// The register-level ABI itself is checked by herbceptions-msabi-1.C.

struct empty {};
struct S {
  empty e;
  unsigned long long x;
};
struct FourInts {
  int a, b, c, d;
};

__attribute__ ((noinline)) S
f (unsigned long long v) return_failure{int}
{
  return S{empty{}, v};
}

__attribute__ ((noinline)) FourInts
g (int v) return_failure{int}
{
  return FourInts{v, v, v, v};
}

int
main ()
{
  auto s = catch return_failure (f (0x1122334455667788ull));
  if (s.failed || s.value.x != 0x1122334455667788ull)
    return 1;

  auto fi = catch return_failure (g (0x0a0b0c0d));
  if (fi.failed || fi.value.a != 0x0a0b0c0d || fi.value.d != 0x0a0b0c0d)
    return 2;

  return 0;
}
