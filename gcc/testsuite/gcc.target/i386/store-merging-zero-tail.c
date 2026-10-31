/* { dg-do compile { target { ! ia32 } } } */
/* { dg-options "-O3" } */

/* An object returned in memory whose value is narrower than a word, with the
   remaining bytes cleared, must have each of its words written by a single
   store: a word load covered by two stores cannot be forwarded, and here each
   call copies the object out of the return slot.  */

struct R
{
  unsigned v, tag, pad[2];
};

static unsigned long long calls;

__attribute__ ((noinline, ms_abi)) struct R
f (unsigned n, unsigned d)
{
  struct R r = { 0, 0, { 0, 0 } };
  ++calls;

  if (!d)
    {
      r.tag = 1;
      return r;
    }
  if (n <= 2)
    {
      r.v = 1;
      return r;
    }

  struct R a = f (n - 2, d - 1);
  if (a.tag)
    return a;
  struct R b = f (n - 1, d - 1);
  if (b.tag)
    return b;

  r.v = a.v + b.v;
  return r;
}

/* { dg-final { scan-assembler-not "movq\t\\\$0, 4\\(%r" } } */
