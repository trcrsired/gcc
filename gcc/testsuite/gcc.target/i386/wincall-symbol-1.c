/* WinCall symbols carry a fixed "@win" assembler suffix so that the
   linker can catch a caller/callee calling-convention mismatch, much as
   the "@N" parameter-size suffix does for stdcall on i386.  Because `@'
   introduces symbol versioning on ELF, the suffix is emitted only on the
   PE/COFF targets (it lives in config/mingw/winnt.cc), which is what
   this test checks.  */

/* { dg-do compile { target { *-*-mingw* } } } */
/* { dg-require-effective-target apxf } */
/* { dg-options "-O2 -mapxf" } */

__attribute__ ((wincall, noinline)) long
foo (long a)
{
  return a + 1;
}

long
bar (long a)
{
  return a + 2;
}

/* A WinCall function gets the suffix ...  */
/* { dg-final { scan-assembler "foo@win" } } */

/* ... and an ordinary one does not.  */
/* { dg-final { scan-assembler-not "bar@win" } } */
