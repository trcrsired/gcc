// { dg-do compile { target { x86_64-*-* } } }
// { dg-options "-fherbceptions -fno-exceptions -O2" }

// A caller reads the carry-flag failure discriminant by materializing it
// with `setc' and then testing the byte.  `setc' produces 0 or 1 and reads
// but does not write the flags, so `testb'/`jne' is a verbose `jb': the
// two insns fold away and the branch reads CF directly.  The fold has to
// happen after prologue/epilogue threading, where a caller's
// argument-popping `add' -- which redefines CF -- is either a real
// instruction or has been merged away for good.  Mirrors LLVM's
// X86HerbceptionsFold machine pass.
//
// Keeping `g' an incomplete type leaves a real call behind, so the
// discriminant is not constant-folded away.

int g (int) throws;

int f (int x) throws
{
  return g (x) + 1;
}

// The discriminant is read with `setc' ...
// { dg-final { scan-assembler "setc" } }
// ... and the branch tests CF rather than the byte, so no `testb' remains.
// { dg-final { scan-assembler "\\tjc\\t" } }
// { dg-final { scan-assembler-times "testb" 0 } }
