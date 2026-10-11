// { dg-do compile { target c++17 } }
// { dg-additional-options "-fherbceptions" }

// There are no implicit conversions between herbceptions function
// pointers and other function pointers: the deterministic error
// channel changes the calling convention.

void f() throws;
void g() return_failure{int};
void n() noexcept;
void p();

void (*a)() = f;                       // { dg-error "invalid conversion" }
void (*b)() noexcept = f;              // { dg-error "invalid conversion" }
void (*c)() noexcept = g;              // { dg-error "invalid conversion" }
void (*d)() throws = n;                // { dg-error "invalid conversion" }
void (*e)() throws = g;                // { dg-error "invalid conversion" }
void (*h)() return_failure{int} = f;   // { dg-error "invalid conversion" }
void (*i)() return_failure{int} = g;   // OK
void (*j)() noexcept = n;              // OK: noexcept converts
void (*k)() = p;
