// { dg-do run { target { *-*-linux* } } }
// { dg-options "-fherbceptions -fno-exceptions -O2" }
// A `return_failure{E}' function whose success value returns in memory
// (sret) materializes that value through DECL_RESULT.  The success
// return must carry the result as its operand so the slot stays live:
// with a value-less `return;' GIMPLE DSE drops the write-through as a
// dead store to the result local and the caller reads an
// uninitialized slot.
// Regression test for bugs/gcc_herbceptions/sret-dse.

struct E { int e; };
struct Big { long a, b, c, d; };

Big
make (int x) return_failure{E}
{
  if (x < 0)
    return_failure E{-x};
  return Big{x, 1, 2, 3};
}

int
main ()
{
  auto ok = catch return_failure (make (3));
  if (ok.failed)
    return 1;
  if (ok.value.a != 3 || ok.value.b != 1 || ok.value.c != 2
      || ok.value.d != 3)
    return 2;

  auto bad = catch return_failure (make (-3));
  if (!bad.failed)
    return 3;
  if (bad.error.e != 3)
    return 4;

  return 0;
}
