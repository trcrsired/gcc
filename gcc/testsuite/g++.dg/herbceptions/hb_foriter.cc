// { dg-do run { target { *-*-linux* } } }
// { dg-options "-fherbceptions" }
// Spec'd calls inside a for-iterate expression must run once per
// iteration — the lowered channel machinery belongs inside the
// iterate slot, not hoisted before the loop.
namespace std { struct error { const void *domain; __UINTPTR_TYPE__ code; }; }
int n;
int wait() throws { ++n; return 0; }
int f() throws {
  int i = 0;
  for (; i < 3; wait())
    ++i;
  return n;
}
int main() {
  if (f() != 3) __builtin_abort();
}
