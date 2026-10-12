// { dg-do compile { target c++17 } }
// { dg-options "-fherbceptions" }
// Port of LLVM herbceptions-special-funcs / herbceptions-dtor-no-throws:
// ctors (incl. copy) may be spec'd; destructors may not.
namespace std { struct error { const void *domain; __UINTPTR_TYPE__ code; }; }

struct Foo {
  int x;
  Foo (int x) throws : x (x) {}
  Foo (const Foo &o) throws : x (o.x) {}
  ~Foo () noexcept = default;
};

struct Bad {
  ~Bad () throws {}			// { dg-error "destructor" }
};
