// { dg-do compile { target { c++11 && lp64 } } }
// { dg-options "-fherbceptions -fno-exceptions -mabi=ms -mcall-ms2sysv-xlogues -O2" }
// An ms_abi herbceptions function that calls a sysv function uses the
// ms2sysv register-saving stub.  The stub call clobbers CF, so the
// failure discriminant must be re-set from its slot afterwards; the tail
// form of the stub is reached by `jmp' and leaves CF alone.  Codegen used
// to trip "return not followed by barrier" here.

struct errc { int e; };

extern "C" __attribute__ ((sysv_abi)) long
sysv (long, long, long, long, long, long);

__attribute__ ((ms_abi)) int
msfn (int c) return_failure{errc}
{
  long x = sysv (1, 2, 3, 4, 5, 6);
  if (c < 0)
    return_failure errc{-c};
  return (int) x + c;
}

// { dg-final { scan-assembler "negb" } }
