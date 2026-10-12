// { dg-do run { target { *-*-linux* } } }
// { dg-options "-fherbceptions -fno-exceptions" }
// Port of LLVM herbceptions-fails-noexcept: a return_failure{E}
// function may call legacy (unspec'd) functions freely — only
// spec'd calls need handling.
struct E { int code; };
struct S { int v; };

static int g_called;

void
legacy () noexcept
{
  ++g_called;
}

int
defaultf () return_failure{E}
{
  legacy ();
  return 1;
}

int
main ()
{
  auto e = catch return_failure (defaultf ());
  return (!e.failed && e.value == 1 && g_called == 1) ? 0 : 1;
}
