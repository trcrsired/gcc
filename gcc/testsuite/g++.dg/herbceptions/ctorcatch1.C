// { dg-do run { target { *-*-linux* } } }
// { dg-options "-fherbceptions -fno-exceptions" }
// Port of LLVM herbceptions-ctor-catch: a spec'd constructor failing
// inside a try block routes the error to the catch throws handler.
namespace std { struct error { const void *domain; __UINTPTR_TYPE__ code; }; }

struct bad_file {
  int fd;
  bad_file () throws
  {
    throw throws (std::error{(const void *) 0x3, 5});
  }
  ~bad_file () noexcept {}
};

struct good_file {
  int fd;
  good_file () throws : fd (1) {}
  ~good_file () noexcept {}
};

int
test_bug () try
{
  bad_file f1;		// Throws
  good_file f2;		// Must not reach
  return 0;
}
catch throws (std::error e)
{
  return e.code == 5 ? 1 : 2;
}

int
main ()
{
  return test_bug () == 1 ? 0 : 3;
}
