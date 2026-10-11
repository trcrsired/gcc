// { dg-do compile { target c++11 } }
// { dg-options "-fherbceptions" }

namespace std { struct error { const void *domain; __UINTPTR_TYPE__ code; }; }

int f() throws;

void e1() { throw throws(std::error{}); }              // { dg-error "only valid inside a function declared" }
int e2() { return f(); }                              // { dg-error "must be handled" }
void e3() { f(); }                                    // { dg-error "must be handled" }
void e4() throws { throw throws; }                    // { dg-error "rethrows the caught error" }
int e5() return_failure{int} { throw throws(std::error{}); } // { dg-error "return_failure" }
void e6() { try {} catch return_failure(int) {} }      // { dg-error "expression form" }
void e7() { throw throws; }                           // { dg-error "rethrows the caught error" }
void e8() { try { f(); } catch throws(int e) { (void)e; } } // { dg-error "std::error" }
void e9() { return_failure(1); }                      // { dg-error "only valid inside" }
