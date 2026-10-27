/* Make the herbceptions return payload visible to the middle end.
   Copyright (C) 2026 Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 3, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING3.  If not see
<http://www.gnu.org/licenses/>.  */

/* The front ends lower the herbceptions {value,error} union return
   channel to a volatile local, `__hb_result', that is written just
   before each return and read only by expand_function_end at RTL
   expansion time.  The read is invisible to GIMPLE, so the stores must
   be volatile; that in turn makes the whole union addressable and
   prevents SRA/DSE from promoting the payload, leaving every spec'd
   function with a stack round-trip of the return value (and, on x86, a
   partial store-forwarding stall on the reload).

   This pass runs after all inlining and before the main optimization
   pipeline.  It turns each store into the channel local into an
   explicit `IFN_HERBCEPTION_RETURN_PAYLOAD (value, byte-offset)' call,
   whose RTL expansion places the payload in a pseudo holding the
   function's return-register group.  The epilogue then moves that
   pseudo group to the real return registers.  Once every store has been
   converted the local is dead, so its volatility is dropped and DCE
   removes it.  A target whose ABI returns the channel in registers then
   never sends the payload through memory.  If any access cannot be
   represented this way the whole function is left untouched and the
   old epilogue path (which reloads the local) stays in charge.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "backend.h"
#include "tree.h"
#include "gimple.h"
#include "gimple-iterator.h"
#include "gimple-walk.h"
#include "tree-pass.h"
#include "ssa.h"
#include "tree-ssa.h"
#include "fold-const.h"
#include "tree-cfg.h"
#include "internal-fn.h"
#include "tree-into-ssa.h"
#include "gimplify-me.h"

namespace {

/* If T designates a byte range of SLOT (possibly through a chain of
   structure members), return true and store the byte offset in *OFF.
   Bit-fields and any other non-byte-aligned access are rejected.  */

static bool
hb_slot_offset (tree t, tree slot, HOST_WIDE_INT *off)
{
  HOST_WIDE_INT o = 0;

  while (TREE_CODE (t) == COMPONENT_REF)
    {
      tree f = TREE_OPERAND (t, 1);
      if (TREE_CODE (f) != FIELD_DECL
	  || DECL_BIT_FIELD (f)
	  || int_bit_position (f) % BITS_PER_UNIT != 0)
	return false;
      o += int_bit_position (f) / BITS_PER_UNIT;
      t = TREE_OPERAND (t, 0);
    }

  if (t != slot)
    return false;

  *off = o;
  return true;
}

/* walk_tree callback: stop when *TP is SLOT.  */

static tree
hb_find_slot (tree *tp, int *walk_subtrees, void *data)
{
  if (*tp == (tree) data)
    return *tp;
  if (TYPE_P (*tp))
    *walk_subtrees = 0;
  return NULL_TREE;
}

/* True if the RHS of the assignment STMT mentions SLOT.  */

static bool
hb_rhs_uses_slot (gimple *stmt, tree slot)
{
  if (gimple_code (stmt) != GIMPLE_ASSIGN)
    return false;
  return walk_tree (gimple_assign_rhs1_ptr (stmt), hb_find_slot, slot, NULL)
	 != NULL_TREE;
}

/* True if any operand of STMT, including the LHS, mentions SLOT.  */

static bool
hb_stmt_uses_slot (gimple *stmt, tree slot)
{
  for (unsigned i = 0; i < gimple_num_ops (stmt); i++)
    if (walk_tree (gimple_op_ptr (stmt, i), hb_find_slot, slot, NULL)
	!= NULL_TREE)
      return true;
  return false;
}

/* Decide whether STMT is a scalar store into the channel local at a
   word-aligned byte offset.  Aggregates are handled by
   hb_convertible_aggregate_store_p below; the automatic-variable
   initializer of the whole local needs no conversion and is left for
   DCE to remove once the local is dead.  */

static bool
hb_convertible_store_p (gimple *stmt, tree slot, HOST_WIDE_INT *off)
{
  if (gimple_code (stmt) != GIMPLE_ASSIGN)
    return false;

  tree lhs = gimple_assign_lhs (stmt);
  if (!hb_slot_offset (lhs, slot, off))
    return false;

  tree rhs = gimple_assign_rhs1 (stmt);
  tree rhs_type = TREE_TYPE (rhs);

  if (!is_gimple_reg_type (rhs_type) || !is_gimple_val (rhs))
    return false;

  HOST_WIDE_INT size = int_size_in_bytes (rhs_type);
  if (size <= 0 || size > UNITS_PER_WORD)
    return false;

  if (*off % UNITS_PER_WORD != 0)
    return false;

  if (hb_rhs_uses_slot (stmt, slot))
    return false;

  return true;
}

/* Decide whether STMT copies a whole aggregate into the channel local at
   a word-aligned offset.  The front end uses such copies on failure
   propagation paths (`__hb_result.error = result.error').  They are
   split into word-sized load/payload pairs.  */

static bool
hb_convertible_aggregate_store_p (gimple *stmt, tree slot, HOST_WIDE_INT *off)
{
  if (gimple_code (stmt) != GIMPLE_ASSIGN)
    return false;

  if (!hb_slot_offset (gimple_assign_lhs (stmt), slot, off))
    return false;

  tree rhs = gimple_assign_rhs1 (stmt);
  tree rhs_type = TREE_TYPE (rhs);

  if (is_gimple_reg_type (rhs_type))
    return false;

  if (!DECL_P (rhs) && !REFERENCE_CLASS_P (rhs))
    return false;

  /* The word loads below assume the source object is at least
     word-aligned; leave packed or otherwise under-aligned copies to the
     memory path.  */
  if (TYPE_ALIGN (rhs_type) < BITS_PER_WORD)
    return false;

  HOST_WIDE_INT size = int_size_in_bytes (rhs_type);
  if (size <= 0 || size % UNITS_PER_WORD != 0)
    return false;

  if (*off % UNITS_PER_WORD != 0)
    return false;

  if (hb_rhs_uses_slot (stmt, slot))
    return false;

  return true;
}

/* Return true if STMT is the deferred initializer of the whole channel
   local, which the conversion makes redundant.  */

static bool
hb_slot_deferred_init_p (gimple *stmt, tree slot)
{
  return (is_gimple_call (stmt)
	  && gimple_call_internal_p (stmt, IFN_DEFERRED_INIT)
	  && gimple_call_lhs (stmt) == slot);
}

unsigned int
herbceptions_return_channel (function *fun)
{
  tree slot = fun->herbceptions_error_slot;
  if (slot == NULL_TREE)
    return 0;

  /* First make sure the whole function can be converted.  */
  basic_block bb;
  FOR_EACH_BB_FN (bb, fun)
    for (gimple_stmt_iterator gsi = gsi_start_bb (bb);
	 !gsi_end_p (gsi); gsi_next (&gsi))
      {
	gimple *stmt = gsi_stmt (gsi);
	if (is_gimple_debug (stmt))
	  continue;

	if (hb_slot_deferred_init_p (stmt, slot))
	  continue;

	HOST_WIDE_INT off;
	if (hb_convertible_store_p (stmt, slot, &off)
	    || hb_convertible_aggregate_store_p (stmt, slot, &off))
	  continue;

	/* Any other statement that mentions the slot makes the local
	   live in a way the payload carrier cannot express.  */
	if (hb_stmt_uses_slot (stmt, slot))
	  return 0;
      }

  /* Now rewrite.  */
  bool changed = false;
  FOR_EACH_BB_FN (bb, fun)
    for (gimple_stmt_iterator gsi = gsi_start_bb (bb);
	 !gsi_end_p (gsi);)
      {
	gimple *stmt = gsi_stmt (gsi);

	if (hb_slot_deferred_init_p (stmt, slot))
	  {
	    gsi_next (&gsi);
	    continue;
	  }

	HOST_WIDE_INT off;
	if (hb_convertible_store_p (stmt, slot, &off))
	  {
	    tree rhs = gimple_assign_rhs1 (stmt);
	    tree off_cst = build_int_cst (sizetype, off);
	    gcall *call
	      = gimple_build_call_internal
		  (IFN_HERBCEPTION_RETURN_PAYLOAD, 2, rhs, off_cst);
	    gimple_set_location (call, gimple_location (stmt));
	    gsi_replace (&gsi, call, true);
	    changed = true;
	  }
	else if (hb_convertible_aggregate_store_p (stmt, slot, &off))
	  {
	    tree rhs = gimple_assign_rhs1 (stmt);
	    HOST_WIDE_INT size = int_size_in_bytes (TREE_TYPE (rhs));
	    tree wtype = build_nonstandard_integer_type (BITS_PER_WORD, 1);
	    tree ptype = build_pointer_type (wtype);
	    tree addr
	      = force_gimple_operand_gsi (&gsi,
					  fold_convert (ptype,
							build_fold_addr_expr (rhs)),
					  true, NULL_TREE, true, GSI_SAME_STMT);

	    for (HOST_WIDE_INT o = 0; o < size; o += UNITS_PER_WORD)
	      {
		tree mem = build2 (MEM_REF, wtype, addr,
				   build_int_cst (ptype, o));
		tree val
		  = force_gimple_operand_gsi (&gsi, mem, true, NULL_TREE,
					      true, GSI_SAME_STMT);
		gcall *call
		  = gimple_build_call_internal
		      (IFN_HERBCEPTION_RETURN_PAYLOAD, 2, val,
		       build_int_cst (sizetype, off + o));
		gimple_set_location (call, gimple_location (stmt));
		gsi_insert_before (&gsi, call, GSI_SAME_STMT);
	      }

	    gsi_remove (&gsi, true);
	    changed = true;
	  }

	gsi_next (&gsi);
      }

  if (!changed)
    return 0;

  /* The local is now dead.  Dropping the volatility lets DCE remove it
     (and with it the channel's stack slot).  */
  TREE_THIS_VOLATILE (slot) = 0;

  /* The deferred initializer was marked volatile when the local was;
     refresh its cached flag now that the local is ordinary.  */
  FOR_EACH_BB_FN (bb, fun)
    for (gimple_stmt_iterator gsi = gsi_start_bb (bb);
	 !gsi_end_p (gsi); gsi_next (&gsi))
      if (hb_slot_deferred_init_p (gsi_stmt (gsi), slot))
	gimple_set_has_volatile_ops (gsi_stmt (gsi), false);

  mark_virtual_operands_for_renaming (fun);

  return TODO_update_ssa;
}

} // anon namespace

const pass_data pass_data_herbceptions_return =
{
  GIMPLE_PASS, /* type */
  "herbceptions_return", /* name */
  OPTGROUP_NONE, /* optinfo_flags */
  TV_NONE, /* tv_id */
  ( PROP_cfg | PROP_ssa ), /* properties_required */
  0, /* properties_provided */
  0, /* properties_destroyed */
  0, /* todo_flags_start */
  0, /* todo_flags_finish */
};

class pass_herbceptions_return : public gimple_opt_pass
{
public:
  pass_herbceptions_return (gcc::context *ctxt)
    : gimple_opt_pass (pass_data_herbceptions_return, ctxt)
  {}

  /* opt_pass methods: */
  bool gate (function *fun) final override
    {
      return (optimize > 0
	      && fun->herbceptions_error_slot != NULL_TREE);
    }

  unsigned int execute (function *fun) final override;
}; // class pass_herbceptions_return

unsigned int
pass_herbceptions_return::execute (function *fun)
{
  return herbceptions_return_channel (fun);
}

gimple_opt_pass *
make_pass_herbceptions_return (gcc::context *ctxt)
{
  return new pass_herbceptions_return (ctxt);
}
