// { dg-do run { target { *-*-linux* } } }
// { dg-options "-fherbceptions -fexceptions" }
// { dg-final { scan-assembler "call.*__cxa_error_code_itanium_exception_ptr" } }
// { dg-final { scan-assembler-not "call.*__cxa_begin_catch" } }
// { dg-final { scan-assembler-not "call.*__cxa_end_catch" } }
//
// The legacy-EH -> herbceptions conversion must hand
// __cxa_error_code_itanium_exception_ptr the _Unwind_Exception * the
// personality gave the landing pad -- not the object __cxa_begin_catch
// would have adjusted it to -- and must not enter a legacy catch at all:
// libherbceptions classifies that pointer itself, retires the
// uncaught-exception count __cxa_throw added, and lets the minted code
// adopt the in-flight reference.  Feeding it the adjusted object made it
// see a foreign exception_class and abort; calling __cxa_end_catch would
// release the reference the code now owns.
// Regression test for bugs/gcc_herbceptions/legacy-eh-conversion-wrong-pointer.

namespace std { struct error { const void *domain; __UINTPTR_TYPE__ code; }; }

static int g_begin, g_end;

extern "C" void *__cxa_error_domain_itanium_exception_ptr () noexcept
{
  return (void *) 0xDEAD;
}

extern "C" void *__cxa_begin_catch (void *) noexcept
{
  ++g_begin;
  return (void *) 0xBADC0FFE;
}

extern "C" void __cxa_end_catch ()
{
  ++g_end;
}

extern "C" __UINTPTR_TYPE__
__cxa_error_code_itanium_exception_ptr (__UINTPTR_TYPE__ flags, void *eh,
					void *, void *) noexcept
{
  if (flags != 1)
    return 0;
  /* A null or adjusted-object pointer means the conversion was handed
     the wrong thing; the regression returns 0 here and fails main.  */
  if (eh == nullptr || eh == (void *) 0xBADC0FFE)
    return 0;
  return 77;
}

struct myerr { int x; };

void
f () throws
{
  throw myerr{};
}

int
main ()
{
  /* A `throws' function boundary fabricates the pair on its channel.  */
  bool caught = false;
  try
    {
      f ();
    }
  catch throws (std::error e)
    {
      caught = true;
      if (e.code != 77)
	return 1;
    }
  if (!caught)
    return 2;
  if (g_begin != 0)
    return 3;			/* the boundary entered a legacy catch */
  if (g_end != 0)
    return 4;

  /* A `catch throws' handler converting a legacy throw in the same
     try.  */
  caught = false;
  try
    {
      throw myerr{};
    }
  catch throws (std::error e)
    {
      caught = true;
      if (e.code != 77)
	return 5;
    }
  if (!caught)
    return 6;
  if (g_begin != 0)
    return 7;
  if (g_end != 0)
    return 8;

  return 0;
}
