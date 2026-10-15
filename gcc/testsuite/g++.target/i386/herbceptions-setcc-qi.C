// { dg-do compile { target { x86_64-*-* } } }
// { dg-options "-fherbceptions -fno-exceptions -O2 -m32 -ffixed-eax -ffixed-ecx -ffixed-edx" }

// The failure discriminant is materialised with `setc' (caller side) and
// set with `negb' (callee epilogue).  Both patterns constrained their
// QImode operand `"rm"', which lets the register allocator choose
// SI/DI/BP/SP -- registers whose low byte (sil/dil/bpl/spl) only exists
// under a REX prefix.  On i686 there is no REX, so the final output died
// in print_reg with "unsupported size for integer register".  Pinning the
// byte-addressable registers with -ffixed-* forces the allocator off the Q
// class and reproduces the old failure; the `"qm"' constraint (the same
// one the canonical *setcc_qi uses) now confines the operand to a byte
// register or memory.

struct E { unsigned long long a; };

__attribute__ ((noinline)) bool
inner (long long *t, int c) return_failure{E}
{
  if (c < 0)
    return_failure E{(unsigned long long) -c};
  *t = c;
  return true;
}

__attribute__ ((noinline)) bool
outer (long long *t, int c) return_failure{E}
{
  bool ok{inner (t, c)};
  return ok;
}

int
main (int argc, char **)
{
  long long t{};
  bool ok{outer (&t, argc)};
  return static_cast<int> (t) + static_cast<int> (ok) + argc;
}

// The discriminant is a Q register (or memory), so both halves emit.
// { dg-final { scan-assembler "setc" } }
// { dg-final { scan-assembler "negb" } }
