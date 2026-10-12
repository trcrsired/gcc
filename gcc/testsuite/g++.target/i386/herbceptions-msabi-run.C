// { dg-do run { target { x86_64-*-* } } }
// { dg-options "-fherbceptions -fno-exceptions -std=c++26 -ffreestanding -mabi=ms" }

// Runtime check of the Win64 (MS ABI) herbceptions calling convention:
// 16-byte trivially-copyable aggregates travel in RAX:RDX (return) and in
// two integer registers (arguments), with the discriminant in CF.

struct errc { long long a, b; };
struct big { long long x, y; };

__attribute__ ((noinline)) big
id (big b) return_failure{errc}
{
  return b;
}

__attribute__ ((noinline)) big
f (big b, int c) return_failure{errc}
{
  if (c)
    return_failure errc{1, 2};
  return b;
}

int
main ()
{
  big b{5, 6};
  auto ok = catch return_failure (f (b, 0));
  if (ok.failed || ok.value.x != 5 || ok.value.y != 6)
    return 1;

  auto bad = catch return_failure (f (b, 1));
  if (!bad.failed || bad.error.a != 1 || bad.error.b != 2)
    return 2;

  auto idr = catch return_failure (id (b));
  if (idr.failed || idr.value.x != 5 || idr.value.y != 6)
    return 3;

  return 0;
}
