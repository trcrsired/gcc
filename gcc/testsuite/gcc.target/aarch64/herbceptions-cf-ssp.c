/* { dg-do compile } */
/* { dg-options "-fherbceptions -fno-exceptions -O2 -fstack-protector-all" } */
/* The stack-protector canary check runs before the failure flag is written:
   expand_function_end emits stack_protect_epilogue() first and
   herbceptions_load_failed afterwards, so the `cmp w*, #1' that sets NZCV.C
   follows the cookie compare.  Smoke test that the combination builds and
   emits both.  */

int
f (int x) return_failure{long}
{
  volatile char buf[32];
  buf[0] = (char) x;
  if (x < 0)
    return_failure 1;
  return buf[0];
}

/* The cookie check ... */
/* { dg-final { scan-assembler "__stack_chk_fail" } } */
/* ... and the flag write both appear.  */
/* { dg-final { scan-assembler "cmp\\tw\[0-9\]+, #1" } } */
