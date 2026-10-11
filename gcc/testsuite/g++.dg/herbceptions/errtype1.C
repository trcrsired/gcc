// { dg-do compile { target c++17 } }
// { dg-additional-options "-fherbceptions" }

struct NT { NT(const NT&) {} };         // not trivially copyable
struct Big { char c[64]; };             // too large
struct Inc;                             // incomplete

void a() return_failure{NT};            // { dg-error "must be trivially copyable" }
void b() return_failure{Big};           // { dg-error "may not be larger" }
void c() return_failure{Inc};           // { dg-error "invalid use of incomplete type" }
void d() return_failure{__cxx_std_error}; // { dg-error "only carried by the implicit" }
void e() return_failure{int*};          // OK
void f() return_failure{double};        // OK
