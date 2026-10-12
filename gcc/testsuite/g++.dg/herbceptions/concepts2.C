// { dg-do compile { target c++20 } }
// { dg-options "-fherbceptions -fno-exceptions" }
// `throws' in a simple-requirement is ill-formed — it only exists in
// the compound form `{ expr } throws'.

void f () throws {}

template<typename T>
concept bad = requires
{
    f () throws;   // { dg-error "simple-requirement" }
};

template<typename T>
concept good = requires
{
    { f () } throws;
};

static_assert (good<int>);
