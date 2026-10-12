/* { dg-do compile { target { x86_64-*-* } } } */
/* { dg-options "-fherbceptions -mabi=ms -O0" } */

/* Win64 (MS ABI) herbceptions calling convention: a trivially-copyable
   16-byte aggregate is coerced to the integer pair {i64, i64} and is
   returned in RAX:RDX and passed in two integer registers rather than in
   memory.  The discriminant rides in the carry flag.  */

typedef struct { long long a, b; } errc;
typedef struct { long long x, y; } big;

__attribute__ ((noinline)) big
f (big b, int c) return_failure{errc}
{
  if (c)
    return_failure ((errc){1, 2});
  return b;
}

long long
g (big b)
{
  __typeof__ (catch return_failure (f (b, 0))) r
    = catch return_failure (f (b, 0));
  return r.failed ? 1 : r.value.x;
}

/* The callee sets CF from the failure flag ...  */
/* { dg-final { scan-assembler-times "negb" 1 } } */
/* ... and the caller reads it back.  */
/* { dg-final { scan-assembler "setc" } } */
