// { dg-do compile { target c++17 } }
// { dg-options "-fherbceptions" }

namespace std { struct error { const void *domain; __UINTPTR_TYPE__ code; }; }

int f() throws;

template<class T> T g() throws { throw throws(std::error{}); }
template<class T> void h() { try { f(); } catch throws(std::error e) { (void)e; } }
template<class T> void i() { if constexpr (sizeof(T) > 0) { f(); } else { f(); } }

void use() throws { g<int>(); }
void use2() { try { g<char>(); } catch throws(std::error) {} }
void use3() { h<double>(); }
void use4() { i<long>(); }
