// { dg-do compile { target c++17 } }
// { dg-additional-options "-fherbceptions" }

// Redeclarations must carry the identical spec.

void f() throws;
void f() noexcept;                     // { dg-error "different exception specifier" }
void g() return_failure{int};
void g() return_failure{char};         // { dg-error "different exception specifier" }
void h() throws;
void h() throws;                       // OK
void i() return_failure{int};
void i() return_failure{int};          // OK
