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

void
aarch64_pe_seh_unwind_emit (FILE *out_file, rtx_insn *insn)
{
  rtx note;
  struct seh_frame_state *seh;

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
      switch (REG_NOTE_KIND (note))
	{
	case REG_CFA_ADJUST_CFA:
	  {
	    rtx pat = XEXP (note, 0);
	    if (pat == NULL_RTX)
	      pat = PATTERN (insn);

	    /* Extract the SET operation.  */
	    if (GET_CODE (pat) == PARALLEL)
	      pat = XVECEXP (pat, 0, 0);

	    if (GET_CODE (pat) != SET)
	      break;

	    rtx dest = SET_DEST (pat);
	    rtx src = SET_SRC (pat);

	    /* We handle sp = sp - N (sub) or sp = sp + N (add).  */
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
	      }
	  }
	  break;

	case REG_CFA_OFFSET:
	  {
	    rtx set = XEXP (note, 0);
	    if (set == NULL_RTX)
	      set = single_set (insn);
	    if (set == NULL_RTX || GET_CODE (set) != SET)
	      break;

	    rtx reg = SET_DEST (set);
	    rtx mem = SET_SRC (set);

	    if (!MEM_P (reg))
	      {
		reg = SET_SRC (set);
		mem = SET_DEST (set);
	      }

	    if (!REG_P (reg) || !MEM_P (mem))
	      break;

	    unsigned int regno = REGNO (reg);
	    HOST_WIDE_INT offset = 0;

	    if (GET_CODE (XEXP (mem, 0)) == PLUS
		&& CONST_INT_P (XEXP (XEXP (mem, 0), 1)))
	      offset = INTVAL (XEXP (XEXP (mem, 0), 1));
	    else if (REG_P (XEXP (mem, 0)))
	      offset = 0;
	    else
	      break;

	    seh->reg_offset[regno] = offset;
	    HOST_WIDE_INT sp_rel_off = seh->sp_offset - offset;

	    /* Emit the appropriate SEH save directive based on register type.  */
	    if (regno == 29 || regno == 30)
	      {
		/* x29 (FP) or x30 (LR) - check if paired via the full pattern.  */
		rtx pat = PATTERN (insn);
		if (GET_CODE (pat) == PARALLEL && XVECLEN (pat, 0) >= 2)
		  {
		    rtx set0 = XVECEXP (pat, 0, 0);
		    rtx set1 = XVECEXP (pat, 0, 1);
		    if (GET_CODE (set0) == SET && GET_CODE (set1) == SET)
		      {
			rtx reg0 = REG_P (SET_SRC (set0)) ? SET_SRC (set0) : SET_DEST (set0);
			rtx reg1 = REG_P (SET_SRC (set1)) ? SET_SRC (set1) : SET_DEST (set1);
			if (REG_P (reg0) && REG_P (reg1)
			    && ((REGNO (reg0) == 29 && REGNO (reg1) == 30)
				|| (REGNO (reg0) == 30 && REGNO (reg1) == 29)))
			  {
			    fprintf (out_file, "\t.seh_save_fplr\t"
				     HOST_WIDE_INT_PRINT_DEC "\n", sp_rel_off);
			    break;
			  }
		      }
		  }
		fprintf (out_file, "\t.seh_save_reg\tx%d, "
			 HOST_WIDE_INT_PRINT_DEC "\n", regno, sp_rel_off);
	      }
	    else if (regno >= 8 && regno <= 15)
	      fprintf (out_file, "\t.seh_save_freg\td%d, "
		       HOST_WIDE_INT_PRINT_DEC "\n", regno, sp_rel_off);
	    else
	      fprintf (out_file, "\t.seh_save_reg\tx%d, "
		       HOST_WIDE_INT_PRINT_DEC "\n", regno, sp_rel_off);
	  }
	  break;

	case REG_CFA_REGISTER:
	case REG_CFA_DEF_CFA:
	case REG_CFA_EXPRESSION:
	  /* These are used for frame pointer setup.  For ARM64 SEH,
	     the frame pointer setup (.seh_set_fp / .seh_add_fp) is
	     not easily detectable from REG_CFA notes alone.  We look
	     at the actual insn pattern as a fallback.  */
	  break;

	default:
	  break;
	}
    }
}


