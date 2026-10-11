// { dg-do compile { target c++17 } }
// { dg-additional-options "-fherbceptions" }

struct B { virtual void m() throws;
	   virtual void n() noexcept; };
struct D : B {
  void m() noexcept override;          // { dg-error "looser exception specification" }
  void n() throws override;            // { dg-error "looser exception specification" }
};
struct E : B {
  void m() throws override;            // OK
};
