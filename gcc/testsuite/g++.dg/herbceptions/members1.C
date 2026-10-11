// { dg-do compile { target c++17 } }
// { dg-additional-options "-fherbceptions" }

// restrictions on where specs may appear.

struct S {
  void m() throws;                     // throws on member is fine
  void m2() throws(sizeof(int) == 4);  // deferred member spec
  ~S() throws;                         // { dg-error "destructor cannot" }
  void rf() return_failure{int};       // { dg-error "only be attached to free" }
};

void operator delete(void*) throws;    // { dg-error "deallocation function" }
void operator delete[](void*) throws;  // { dg-error "deallocation function" }

template<class T> void k() return_failure{T}; // { dg-error "only be attached to free" }

void l() { auto x = []() return_failure{int} {}; }; // { dg-error "only be attached to free" }
