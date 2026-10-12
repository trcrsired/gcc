// { dg-do run { target { *-*-linux* } } }
// { dg-options "-fherbceptions -fno-exceptions" }
// Port of LLVM herbceptions-autoprop: a bare call to a spec'd function
// inside a spec'd function auto-propagates the channel; a plain
// function must catch explicitly.

int
bar (int x) return_failure{int}
{
  if (x < 0)
    return_failure x;
  return x + 1;
}

int
foo (int x) return_failure{int}
{
  return bar (x);	// bare call, auto-propagates
}

int
bazour (int x)
{
  auto e = catch return_failure (bar (x));
  return !e.failed ? e.value : e.error;
}

int
main ()
{
  auto ok = catch return_failure (foo (5));
  if (ok.failed || ok.value != 6)
    return 1;
  auto bad = catch return_failure (foo (-7));
  if (!bad.failed || bad.error != -7)
    return 2;
  if (bazour (5) != 6)
    return 3;
  if (bazour (-7) != -7)
    return 4;
  return 0;
}
