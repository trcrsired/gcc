// { dg-do compile { target { i?86-*-* x86_64-*-* } } }
// { dg-options "-fherbceptions -fno-exceptions -O2" }
// The failure discriminant must be written to CF before the epilogue's
// callee-saved restores.  Under register pressure the discriminant's
// register may itself be callee-saved, and reading it after the restores
// would observe the caller's restored value (the restores themselves are
// mov/pop/lea and preserve CF, so the flag survives to the ret).  Mirrors
// llvm/test/CodeGen/X86/throws-csr-disc.ll.

struct E { int e; };

extern long callee (long) return_failure{E};

__attribute__ ((noinline)) long
f (long a, long b, long c, long d, long e, long g, long h, long i, long j)
  return_failure{E}
{
  auto r = catch return_failure (callee (a));
  long s = a + b + c + d + e + g + h + i + j + r.value;
  asm volatile ("" : "+r" (s));
  return_failure E{(int) s};
}

// The flag write ...
// { dg-final { scan-assembler "negb" } }
