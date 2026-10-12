// { dg-do run { target { *-*-linux* } } }
// { dg-options "-fherbceptions" }
// A spec'd constructor called through a decl initializer must run the
// channel check — ctor-init calls bypass the parser's auto-wrap.
namespace std { struct error { const void *domain; __UINTPTR_TYPE__ code; }; }
struct L {
  int fd;
  L(int x) throws { fd = x; if (x < 0) throw throws(std::error{(const void*)1,9}); }
};
int main() {
  try { L l{1}; } catch throws(std::error e) { return (int)e.code; }
  try { L l{-1}; } catch throws(std::error e) { return (int)e.code; }
  return 1;
}
