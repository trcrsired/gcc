// { dg-do compile { target c++17 } }
// { dg-additional-options "-fherbceptions" }

// Mutual exclusion and malformed specifiers.

void a() noexcept throws;              // { dg-error "cannot be combined with 'noexcept'" }
void b() throws noexcept;              // { dg-error "mutually exclusive" }
void c() return_failure{int} throws;   // { dg-error "mutually exclusive" }
void d() throws return_failure{int};   // { dg-error "mutually exclusive" }
void e() return_failure(int);          // { dg-error "requires a brace-enclosed error type" }
void f() return_failure{};             // { dg-error "expected error type" }
