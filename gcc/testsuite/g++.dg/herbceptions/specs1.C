// { dg-do compile { target c++17 } }
// { dg-additional-options "-fherbceptions" }

// Basic throws/return_failure spec parsing and typing.

void f() throws;                       // basic implicit-channel spec
void ft() throws(true);                // same spec, explicit operand
void ff() throws(false);               // equivalent to noexcept
void g() return_failure{int};          // explicit error channel
void gs() return_failure{short};
void gf() return_failure{float};
void gp() return_failure{int*};
void gv() return_failure{void*};

static_assert(!__is_same(decltype(f), void()), "throws changes the type");
static_assert(__is_same(decltype(f), decltype(ft)),
	      "throws and throws(true) are the same spec");
static_assert(__is_same(decltype(ff), void() noexcept),
	      "throws(false) is noexcept");
static_assert(!__is_same(decltype(f), decltype(ff)),
	      "throws is not noexcept");
static_assert(!__is_same(decltype(f), decltype(g)),
	      "throws is not return_failure{E}");
static_assert(!__is_same(decltype(g), decltype(gs)),
	      "return_failure{E} carries E");
static_assert(!__is_same(decltype(f), void() noexcept),
	      "throws is a distinct type from noexcept");

void (*pf)() throws = f;
void (*pg)() return_failure{int} = g;
static_assert(__is_same(decltype(pf), decltype(&f)),
	      "throws fnptr type");
static_assert(__is_same(decltype(pg), decltype(&g)),
	      "return_failure fnptr type");
