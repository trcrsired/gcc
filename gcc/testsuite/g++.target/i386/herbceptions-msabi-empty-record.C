// { dg-do run { target { x86_64-*-* } } }
// { dg-options "-fherbceptions -fno-exceptions -std=c++26 -ffreestanding -mabi=ms -O0" }

// On x86-64 Windows a `throws' function treats an empty by-value record as
// zero register slots (empty structs decompose to zero leaves), so the
// integer argument that follows it arrives in the first integer register.
// The ordinary MS x64 ABI still passes the empty record as a one-byte value
// in the first integer register, pushing the integer to the second.  This
// mirrors clang's WinX86_64ABIInfo IsThrows handling; the plain MS ABI path
// must not change.  Regression test for
// bugs/llvm_herbceptions/empty-record-consumes-register.

struct empty {};
struct errc { int e; };

__attribute__ ((noinline)) int
f (empty e, int c) return_failure{errc}
{
  if (c == 7)
    return_failure errc{99};
  return c;
}

__attribute__ ((noinline)) int
g (empty e, int c)
{
  return c;
}

// `f' is a `throws' function: `c' is the first integer argument and is
// stored from %ecx.
// { dg-final { scan-assembler-times "movl\t%ecx, \[0-9\]+\\(%rbp\\)" 1 } }
// `g' is an ordinary MS ABI function: the empty record occupies %ecx and `c'
// is stored from %edx.
// { dg-final { scan-assembler-times "movl\t%edx, \[0-9\]+\\(%rbp\\)" 1 } }

int
main ()
{
  auto ok = catch return_failure (f (empty{}, 42));
  if (ok.failed || ok.value != 42)
    return 1;

  auto bad = catch return_failure (f (empty{}, 7));
  if (!bad.failed || bad.error.e != 99)
    return 2;

  if (g (empty{}, 5) != 5)
    return 3;

  return 0;
}
