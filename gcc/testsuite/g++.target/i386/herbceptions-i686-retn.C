// { dg-do compile { target { x86_64-*-* } } }
// { dg-options "-fherbceptions -fno-exceptions -O0 -m32" }
// On i686 a callee-pops convention (`stdcall' / `thiscall') returns with
// `ret N'; that, like `leave', preserves EFLAGS, so the carry-flag
// discriminant set by `negb' survives.  The old register-carrier design
// fused `negb' into the return and skipped the `ret N' pop.

struct E { int e; };

__attribute__ ((stdcall, noinline)) int
f (int c) return_failure{E}
{
  if (c < 0)
    return_failure E{-c};
  return c;
}

// { dg-final { scan-assembler "negb" } }
// { dg-final { scan-assembler "ret\\t\\$4" } }
