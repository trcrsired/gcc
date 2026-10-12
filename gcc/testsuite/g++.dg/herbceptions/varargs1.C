// { dg-do run { target { *-*-linux* } } }
// { dg-options "-fherbceptions -fno-exceptions" }
// Port of LLVM herbceptions-throws-varargs: variadic throws functions —
// channel result through the ellipsis, va_arg inside, indirect calls.
namespace std { struct error { const void *domain; __UINTPTR_TYPE__ code; }; }

int
count_max (int n, ...) throws
{
  __builtin_va_list ap;
  __builtin_va_start (ap, n);
  int m = 0;
  for (int i = 0; i < n; ++i)
    {
      int v = __builtin_va_arg (ap, int);
      if (v > m)
	m = v;
    }
  __builtin_va_end (ap);
  return m;
}

int
via_ptr (int (*fp) (int, ...) throws, int n, int a) throws
{
  return fp (n, a);
}

int
main ()
{
  int (*fp) (int, ...) throws = count_max;
  try
    {
      int a = count_max (3, 10, 42, 7);
      int b = via_ptr (fp, 2, 9);
      return (a == 42 && b == 9) ? 0 : 1;
    }
  catch throws (std::error)
    {
      return 2;
    }
}
