/* Windows specific ABI for AArch64 architecture.
   Copyright (C) 2025-2026 Free Software Foundation, Inc.
   Contributed by ARM Ltd.

   This file is part of GCC.

   GCC is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3, or (at your option)
   any later version.

   GCC is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GCC; see the file COPYING3.  If not see
   <http://www.gnu.org/licenses/>.  */

#define IN_TARGET_CODE 1

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "target.h"
#include "backend.h"
#include "rtl.h"
#include "tree.h"
#include "stringpool.h"
#include "attribs.h"
#include "regs.h"
#include "function-abi.h"
#include "builtins.h"
#include "memmodel.h"
#include "output.h"
#include "emit-rtl.h"
#include "rtl-iter.h"
#include "aarch64-abi-ms-protos.h"
#include "config/mingw/winnt.h"

/* Iterate through the target-specific builtin types for va_list.
   IDX denotes the iterator, *PTREE is set to the result type of
   the va_list builtin, and *PNAME to its internal type.
   Returns zero if there is no element for this index, otherwise
   IDX should be increased upon the next call.
   Note, do not iterate a base builtin's name like __builtin_va_list.
   Used from c_common_nodes_and_builtins.  */

int
aarch64_ms_variadic_abi_enum_va_list (int idx, const char **pname, tree *ptree)
{
  switch (idx)
    {
    default:
      break;

    case 0:
      *ptree = ms_va_list_type_node;
      *pname = "__builtin_ms_va_list";
      return 1;
    }

  return 0;
}

/* This function returns the calling abi specific va_list type node.
   It returns  the FNDECL specific va_list type.  */

tree
aarch64_ms_variadic_abi_fn_abi_va_list (tree fndecl)
{
  gcc_assert (fndecl != NULL_TREE);

  arm_pcs pcs = (arm_pcs) fndecl_abi (fndecl).id ();
  if (pcs == ARM_PCS_MS_VARIADIC)
    return ms_va_list_type_node;

  return std_fn_abi_va_list (fndecl);
}

/* Returns the canonical va_list type specified by TYPE.
   If there is no valid TYPE provided, it return NULL_TREE.  */

tree
aarch64_ms_variadic_abi_canonical_va_list_type (tree type)
{
  if (lookup_attribute ("ms_abi va_list", TYPE_ATTRIBUTES (type)))
    return ms_va_list_type_node;

  return NULL_TREE;
}

/* Implement TARGET_ARG_PARTIAL_BYTES.  */

int
aarch64_arg_partial_bytes (cumulative_args_t pcum_v,
			   const function_arg_info &arg ATTRIBUTE_UNUSED)
{
  CUMULATIVE_ARGS *pcum = get_cumulative_args (pcum_v);

  if (pcum->pcs_variant != ARM_PCS_MS_VARIADIC)
    return 0;

  /* Handle the case when argument is split between the last registers and
     the stack.  */
  if ((pcum->aapcs_reg != NULL_RTX) && (pcum->aapcs_stack_words != 0))
    return pcum->aapcs_stack_words * UNITS_PER_WORD;

  return 0;
}


/* AArch64 SEH unwind emission.

   This function is called for each frame-related insn and emits the
   appropriate ARM64 SEH assembly directives based on REG_CFA_* notes
   and the insn pattern.

   ARM64 SEH directives differ from x64:
     .seh_save_fplr   <offset>   - stp x29, x30, [sp, #offset]
     .seh_save_fplr_x <offset>   - stp x29, x30, [sp, #-offset]!
     .seh_save_regp   <r1>,<r2>,<offset> - stp pair at SP+offset
     .seh_save_reg    <r>,<offset>        - str reg at SP+offset
     .seh_save_fregp  <d1>,<d2>,<offset>  - stp d pair at SP+offset
     .seh_save_freg   <d>,<offset>        - str d at SP+offset
     .seh_alloc_stack <size>   - sub sp, sp, #size
     .seh_set_fp               - mov x29, sp
     .seh_add_fp      <offset> - add x29, sp, #offset
     .seh_save_lrpair <r>,<offset> - stp x30, <r>, [sp, #offset]
     .seh_nop                  - no-op padding
*/

/* Emit aarch64 SEH directives for one frame-related expression PAT.
   Handles PARALLELs of SETs (including the storewb_pre_pair_8 combined
   alloc+save), plain alloc_stack, set_fp/add_fp, register saves, and the
   store_pair_8 pattern expressed as (set (mem:V2x8QI ...) (unspec [...])).
   All offsets are relative to the current SP.  */

static void
seh_aarch64_emit_expr (FILE *out_file, struct seh_frame_state *seh, rtx pat)
{
  rtx dest, src;

  if (pat == NULL_RTX)
    return;

  if (GET_CODE (pat) == PARALLEL || GET_CODE (pat) == SEQUENCE)
    {
      int i, n = XVECLEN (pat, 0);

      /* Combined alloc_stack + save_fplr:
	 (parallel [(set sp sp-N) (set mem[sp-N] x29) (set mem[sp-N+8] x30)])
	 This is the pre-indexed stp x29, x30, [sp, #-N]! which performs both
	 the stack allocation AND the register save.  Emit a single
	 .seh_save_fplr_x, not a separate .seh_alloc_stack.  */
      if (n == 3
	  && GET_CODE (XVECEXP (pat, 0, 0)) == SET
	  && GET_CODE (XVECEXP (pat, 0, 1)) == SET
	  && GET_CODE (XVECEXP (pat, 0, 2)) == SET
	  && SET_DEST (XVECEXP (pat, 0, 0)) == stack_pointer_rtx
	  && GET_CODE (SET_SRC (XVECEXP (pat, 0, 0))) == PLUS
	  && XEXP (SET_SRC (XVECEXP (pat, 0, 0)), 0) == stack_pointer_rtx
	  && CONST_INT_P (XEXP (SET_SRC (XVECEXP (pat, 0, 0)), 1))
	  && INTVAL (XEXP (SET_SRC (XVECEXP (pat, 0, 0)), 1)) < 0)
	{
	  rtx set1 = XVECEXP (pat, 0, 1);
	  rtx set2 = XVECEXP (pat, 0, 2);
	  rtx reg1 = MEM_P (SET_DEST (set1)) ? SET_SRC (set1)
	    : (REG_P (SET_DEST (set1)) ? SET_DEST (set1) : NULL_RTX);
	  rtx reg2 = MEM_P (SET_DEST (set2)) ? SET_SRC (set2)
	    : (REG_P (SET_DEST (set2)) ? SET_DEST (set2) : NULL_RTX);
	  if (reg1 && reg2)
	    {
	      unsigned int r1 = REGNO (reg1), r2 = REGNO (reg2);
	      HOST_WIDE_INT size
		= -INTVAL (XEXP (SET_SRC (XVECEXP (pat, 0, 0)), 1));
	      if (seh->cfa_reg == stack_pointer_rtx)
		seh->cfa_offset += size;
	      seh->sp_offset += size;

	      if ((r1 == 29 && r2 == 30) || (r1 == 30 && r2 == 29))
		{
		  fprintf (out_file, "\t.seh_save_fplr_x\t"
			   HOST_WIDE_INT_PRINT_DEC "\n", size);
		  return;
		}

	      /* Pre-indexed stp of a non-FPLR pair, e.g.
		 "stp d8, d9, [sp, #-64]!" or "stp x19, x20, [sp, #-64]!".
		 GAS has no save_regp_x/save_fregp_x combined directive, so
		 split into an allocation followed by the register-pair save
		 at offset 0 relative to the (new) SP.  */
	      if ((FP_REGNUM_P (r1) && FP_REGNUM_P (r2))
		  || (!FP_REGNUM_P (r1) && !FP_REGNUM_P (r2)))
		{
		  fprintf (out_file, "\t.seh_alloc_stack\t"
			   HOST_WIDE_INT_PRINT_DEC "\n", size);
		  if (FP_REGNUM_P (r1))
		    fprintf (out_file, "\t.seh_save_fregp\td%d, d%d, 0\n",
			     r1 - V0_REGNUM, r2 - V0_REGNUM);
		  else
		    fprintf (out_file, "\t.seh_save_regp\tx%d, x%d, 0\n",
			     r1, r2);
		  return;
		}
	    }
	}

      /* stp pair as a PARALLEL of two SETs:
	 (parallel [(set mem[sp+off] reg1) (set mem[sp+off+8] reg2)])
	 Emit .seh_save_regp / .seh_save_fplr.  */
      if (n == 2
	  && GET_CODE (XVECEXP (pat, 0, 0)) == SET
	  && GET_CODE (XVECEXP (pat, 0, 1)) == SET)
	{
	  rtx set0 = XVECEXP (pat, 0, 0);
	  rtx set1 = XVECEXP (pat, 0, 1);
	  rtx mem0 = MEM_P (SET_DEST (set0)) ? SET_DEST (set0)
	    : (MEM_P (SET_SRC (set0)) ? SET_SRC (set0) : NULL_RTX);
	  rtx mem1 = MEM_P (SET_DEST (set1)) ? SET_DEST (set1)
	    : (MEM_P (SET_SRC (set1)) ? SET_SRC (set1) : NULL_RTX);
	  rtx reg0 = REG_P (SET_SRC (set0)) ? SET_SRC (set0)
	    : (REG_P (SET_DEST (set0)) ? SET_DEST (set0) : NULL_RTX);
	  rtx reg1 = REG_P (SET_SRC (set1)) ? SET_SRC (set1)
	    : (REG_P (SET_DEST (set1)) ? SET_DEST (set1) : NULL_RTX);
	  if (mem0 && mem1 && reg0 && reg1)
	    {
	      /* Both mems should be (plus sp N) or (sp) with an 8-byte
		 stride.  */
	      rtx a0 = XEXP (mem0, 0), a1 = XEXP (mem1, 0);
	      HOST_WIDE_INT off0 = 0, off1 = 0;
	      bool same_base = false;

	      /* Both mems should be sp-based, e.g. (reg sp) and (plus sp 8).  */
	      rtx base0 = (GET_CODE (a0) == PLUS) ? XEXP (a0, 0) : a0;
	      rtx base1 = (GET_CODE (a1) == PLUS) ? XEXP (a1, 0) : a1;
	      if (base0 == base1 && base0 == stack_pointer_rtx)
		{
		  off0 = (GET_CODE (a0) == PLUS && CONST_INT_P (XEXP (a0, 1)))
		    ? INTVAL (XEXP (a0, 1)) : 0;
		  off1 = (GET_CODE (a1) == PLUS && CONST_INT_P (XEXP (a1, 1)))
		    ? INTVAL (XEXP (a1, 1)) : 0;

		  /* SEH offset is relative to the current SP.  */
		  unsigned int r0 = REGNO (reg0), r1 = REGNO (reg1);
		  if ((r0 == 29 && r1 == 30) || (r0 == 30 && r1 == 29))
		    fprintf (out_file, "\t.seh_save_fplr\t"
			     HOST_WIDE_INT_PRINT_DEC "\n", off0);
		  else if (FP_REGNUM_P (r0) && FP_REGNUM_P (r1))
		    fprintf (out_file, "\t.seh_save_fregp\td%d, d%d, "
			     HOST_WIDE_INT_PRINT_DEC "\n",
			     r0 - V0_REGNUM, r1 - V0_REGNUM, off0);
		  else if (!FP_REGNUM_P (r0) && !FP_REGNUM_P (r1))
		    fprintf (out_file, "\t.seh_save_regp\tx%d, x%d, "
			     HOST_WIDE_INT_PRINT_DEC "\n", r0, r1, off0);
		  return;
		}
	    }
	}

      for (i = 0; i < n; ++i)
	{
	  rtx ele = XVECEXP (pat, 0, i);
	  if (GET_CODE (ele) == SET || GET_CODE (ele) == PARALLEL
	      || GET_CODE (ele) == SEQUENCE)
	    seh_aarch64_emit_expr (out_file, seh, ele);
	}
      return;
    }

  if (GET_CODE (pat) != SET)
    return;

  dest = SET_DEST (pat);
  src = SET_SRC (pat);

  /* sp = sp + N (alloc_stack for N < 0).  */
  if (dest == stack_pointer_rtx
      && GET_CODE (src) == PLUS
      && XEXP (src, 0) == stack_pointer_rtx
      && CONST_INT_P (XEXP (src, 1)))
    {
      HOST_WIDE_INT delta = INTVAL (XEXP (src, 1));
      if (delta < 0)
	{
	  delta = -delta;
	  if (seh->cfa_reg == stack_pointer_rtx)
	    seh->cfa_offset += delta;
	  seh->sp_offset += delta;
	  fprintf (out_file, "\t.seh_alloc_stack\t"
		   HOST_WIDE_INT_PRINT_DEC "\n", delta);
	}
      return;
    }

  /* Frame pointer setup: x29 = sp or x29 = sp + N.  */
  if (dest == hard_frame_pointer_rtx)
    {
      if (src == stack_pointer_rtx)
	{
	  fprintf (out_file, "\t.seh_set_fp\n");
	  seh->cfa_reg = hard_frame_pointer_rtx;
	  seh->cfa_offset = 0;
	  return;
	}
      if (GET_CODE (src) == PLUS && XEXP (src, 0) == stack_pointer_rtx
	  && CONST_INT_P (XEXP (src, 1)))
	{
	  fprintf (out_file, "\t.seh_add_fp\t"
		   HOST_WIDE_INT_PRINT_DEC "\n", INTVAL (XEXP (src, 1)));
	  seh->cfa_reg = hard_frame_pointer_rtx;
	  seh->cfa_offset = 0;
	  return;
	}
      return;
    }

  /* Register save to memory: mem = reg.  The SEH offset is relative to
     the current SP (after preceding alloc_stack), so emit the raw memory
     offset directly.  */
  if (MEM_P (dest) && REG_P (src))
    {
      unsigned int regno = REGNO (src);
      HOST_WIDE_INT offset = 0;
      rtx addr = XEXP (dest, 0);

      if (GET_CODE (addr) == PLUS && CONST_INT_P (XEXP (addr, 1)))
	offset = INTVAL (XEXP (addr, 1));
      else if (GET_CODE (addr) == PRE_DEC && XEXP (addr, 0) == stack_pointer_rtx)
	{
	  /* str reg, [sp, #-N]! : both allocates and stores.  */
	  HOST_WIDE_INT step = GET_MODE_SIZE (GET_MODE (dest)).to_constant ();
	  offset = -step;
	}
      else if (GET_CODE (addr) == REG)
	offset = 0;

      seh->reg_offset[regno] = offset;

      if (FP_REGNUM_P (regno))
	fprintf (out_file, "\t.seh_save_freg\td%d, "
		 HOST_WIDE_INT_PRINT_DEC "\n", regno - V0_REGNUM, offset);
      else if (regno >= 0 && regno <= 30)
	fprintf (out_file, "\t.seh_save_reg\tx%d, "
		 HOST_WIDE_INT_PRINT_DEC "\n", regno, offset);
      return;
    }

  /* Store pair expressed as (set (mem:V2x8QI addr) (unspec [(reg) (reg)] UNSPEC_STP)).  */
  if (MEM_P (dest) && GET_CODE (src) == UNSPEC
      && XVECLEN (src, 0) == 2
      && REG_P (XVECEXP (src, 0, 0)) && REG_P (XVECEXP (src, 0, 1)))
    {
      unsigned int r1 = REGNO (XVECEXP (src, 0, 0));
      unsigned int r2 = REGNO (XVECEXP (src, 0, 1));
      HOST_WIDE_INT offset = 0;
      rtx addr = XEXP (dest, 0);

      if (GET_CODE (addr) == PLUS && CONST_INT_P (XEXP (addr, 1)))
	offset = INTVAL (XEXP (addr, 1));
      else if (GET_CODE (addr) == PRE_DEC && XEXP (addr, 0) == stack_pointer_rtx)
	offset = -16;

      if ((r1 == 29 && r2 == 30) || (r1 == 30 && r2 == 29))
	fprintf (out_file, "\t.seh_save_fplr\t"
		 HOST_WIDE_INT_PRINT_DEC "\n", offset);
      else if (FP_REGNUM_P (r1) && FP_REGNUM_P (r2))
	fprintf (out_file, "\t.seh_save_fregp\td%d, d%d, "
		 HOST_WIDE_INT_PRINT_DEC "\n", r1 - V0_REGNUM, r2 - V0_REGNUM, offset);
      else if (!FP_REGNUM_P (r1) && !FP_REGNUM_P (r2))
	fprintf (out_file, "\t.seh_save_regp\tx%d, x%d, "
		 HOST_WIDE_INT_PRINT_DEC "\n", r1, r2, offset);
      return;
    }
}

void
aarch64_pe_seh_unwind_emit (FILE *out_file, rtx_insn *insn)
{
  rtx note;
  struct seh_frame_state *seh;
  bool handled_one = false;

  if (!TARGET_SEH)
    return;

  seh = cfun->machine->seh;

  if (NOTE_P (insn) && NOTE_KIND (insn) == NOTE_INSN_SWITCH_TEXT_SECTIONS)
    {
      fputs ("\t.seh_endproc\n", out_file);
      seh->in_cold_section = true;
      return;
    }

  if (NOTE_P (insn) || !RTX_FRAME_RELATED_P (insn))
    return;

  if (seh->after_prologue)
    return;

  for (note = REG_NOTES (insn); note; note = XEXP (note, 1))
    {
      rtx pat;

      switch (REG_NOTE_KIND (note))
	{
	case REG_FRAME_RELATED_EXPR:
	  /* The insn pattern is too complex (e.g. store_pair_8 UNSPEC);
	     the note carries the canonical PARALLEL of SETs.  */
	  seh_aarch64_emit_expr (out_file, seh, XEXP (note, 0));
	  handled_one = true;
	  break;

	case REG_CFA_ADJUST_CFA:
	  pat = XEXP (note, 0);
	  if (pat == NULL_RTX)
	    pat = PATTERN (insn);
	  if (GET_CODE (pat) == PARALLEL)
	    pat = XVECEXP (pat, 0, 0);
	  seh_aarch64_emit_expr (out_file, seh, pat);
	  handled_one = true;
	  break;

	case REG_CFA_OFFSET:
	  pat = XEXP (note, 0);
	  if (pat == NULL_RTX)
	    pat = single_set (insn);
	  seh_aarch64_emit_expr (out_file, seh, pat);
	  handled_one = true;
	  break;

	case REG_CFA_REGISTER:
	case REG_CFA_DEF_CFA:
	case REG_CFA_EXPRESSION:
	  /* Frame pointer setup and other complex cases are handled
	     from the instruction pattern directly.  */
	  break;

	default:
	  break;
	}
    }

  /* If no REG_CFA note described the insn, examine the instruction
     pattern directly.  The aarch64 prologue emits frame-related insns
     (such as the plain "sub sp, sp, N" and "mov x29, sp") without
     REG_CFA notes.  */
  if (!handled_one)
    seh_aarch64_emit_expr (out_file, seh, PATTERN (insn));
}


