// { dg-do compile { target c++11 } }
// { dg-options "-fherbceptions" }

namespace std { struct error { const void *domain; __UINTPTR_TYPE__ code; }; }

int f() throws;
void v() throws;
int rf() return_failure{int};

void s1() throws { throw throws(std::error{}); }
int s2() throws { return f(); }
void s3() throws { f(); }
void s4() { try { f(); } catch throws(std::error e) { (void)e; } }
void s5() { try { v(); } catch throws(std::error) {} }
void s6() throws { try { f(); } catch throws(std::error) { throw throws; } }
void s7() throws { try { f(); } catch throws(std::error) { v(); } }
int s8() return_failure{int} { return try(rf()); }
int s9() throws { return try(f()); }
void s10() throws { return_failure(std::error{}); }
int s11() return_failure{int} { return_failure 7; }
int s12() return_failure{int} { return_failure(8); }
void s13() { auto e = catch return_failure(rf()); (void)e; }

struct S { void m() throws; };
void s14() throws { S s; s.m(); }
