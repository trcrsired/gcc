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
   word-aligned byte offset.  The stored value must fit the channel; a
   scalar wider than a word is still carried whole when the channel is a
   single integer value (e.g. PDP-11's four-byte channel).  Aggregates
   are handled by hb_convertible_aggregate_store_p below; the
   automatic-variable initializer of the whole local needs no conversion
   and is left for DCE to remove once the local is dead.  */

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
  HOST_WIDE_INT csize = int_size_in_bytes (TREE_TYPE (slot));
  if (size <= 0 || csize <= 0 || size > csize)
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

/* Return the number of word-sized pieces of SLOT_TYPE's channel when the
   target carries it as a register group, or zero when it is a single
   value.  This mirrors herbceptions_channel_needs_split_p in
   internal-fn.cc: a record, or a union holding a non-scalar member, is
   split at word boundaries.  */

static unsigned int
hb_channel_words (tree slot_type)
{
  bool split = false;

  if (TREE_CODE (slot_type) == RECORD_TYPE)
    split = true;
  else if (TREE_CODE (slot_type) == UNION_TYPE)
    for (tree f = TYPE_FIELDS (slot_type); f; f = DECL_CHAIN (f))
      if (TREE_CODE (f) == FIELD_DECL
	  && !is_gimple_reg_type (TREE_TYPE (f)))
	{
	  split = true;
	  break;
	}

  if (!split)
    return 0;

  HOST_WIDE_INT size = int_size_in_bytes (slot_type);
  if (size <= 0 || size % UNITS_PER_WORD != 0)
    return 0;
  return size / UNITS_PER_WORD;
}

/* Return the mask of channel words the return payload calls in BB write.
   A piece covers every word its bytes touch.  */

static unsigned int
hb_payload_mask (basic_block bb)
{
  unsigned int mask = 0;

  for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
       gsi_next (&gsi))
    {
      gimple *stmt = gsi_stmt (gsi);
      if (!is_gimple_call (stmt)
	  || !gimple_call_internal_p (stmt, IFN_HERBCEPTION_RETURN_PAYLOAD))
	continue;

      tree val = gimple_call_arg (stmt, 0);
      HOST_WIDE_INT off = tree_to_shwi (gimple_call_arg (stmt, 1));
      HOST_WIDE_INT size = int_size_in_bytes (TREE_TYPE (val));
      if (size <= 0)
	size = UNITS_PER_WORD;

      for (HOST_WIDE_INT o = off; o < off + size; o += UNITS_PER_WORD)
	if (o % UNITS_PER_WORD == 0)
	  {
	    unsigned int w = o / UNITS_PER_WORD;
	    if (w < 32)
	      mask |= 1u << w;
	  }
    }
  return mask;
}

/* Define the channel words in MASK that GSI's block does not write (see
   hb_payload_mask) with zeroes.  */

static void
hb_fill_channel_words (gimple_stmt_iterator *gsi, unsigned int nwords,
		       unsigned int mask)
{
  tree wtype = build_nonstandard_integer_type (BITS_PER_WORD, 1);

  for (unsigned int w = 0; w < nwords; w++)
    if (!(mask & (1u << w)))
      {
	gcall *call = gimple_build_call_internal
	  (IFN_HERBCEPTION_RETURN_PAYLOAD, 2,
	   build_int_cst (wtype, 0),
	   build_int_cst (sizetype, w * UNITS_PER_WORD));
	gsi_insert_before (gsi, call, GSI_SAME_STMT);
      }
}

/* Every path to the epilogue must define every piece of a split return
   channel, or the register allocator keeps the pieces that a return
   edge leaves undefined live-in from function entry, across every call.
   The front end writes exactly the members a return makes observable: a
   successful value return leaves the error member undefined.  Give each
   block that writes the channel the missing pieces as zeroes; the bits
   of an undefined member are unobservable, since the caller only reads
   the member the failure flag selects.  Filling happens in the block
   that writes the other pieces, so one edge's fill cannot overwrite a
   value another edge wrote.  */

static void
hb_complete_return_channel (function *fun, tree slot)
{
  unsigned int nwords = hb_channel_words (TREE_TYPE (slot));
  if (nwords < 2 || nwords > 32)
    return;

  unsigned int all = (1u << nwords) - 1;
  basic_block bb;

  FOR_EACH_BB_FN (bb, fun)
    {
      unsigned int mask = hb_payload_mask (bb);
      if (mask == 0 || mask == all)
	continue;

      gimple_stmt_iterator gsi = gsi_last_nondebug_bb (bb);
      hb_fill_channel_words (&gsi, nwords, mask);
    }
}

/* True if STMT may run between a direct channel write and the return:
   stores to the failure discriminant, other payload writes, and control
   flow.  A call or asm could clobber the return registers, and a read
   could still need a value the write replaced.

   Ordinary computations are harmless: the return registers stay live
   from the payload write to the use before the return, so neither the
   register allocator nor reload can put anything else in them.  The
   declared result is the exception -- it can already name a return
   register before this point.  */

static bool
hb_after_direct_payload_p (gimple *stmt)
{
  if (is_gimple_debug (stmt))
    return true;

  switch (gimple_code (stmt))
    {
    case GIMPLE_LABEL:
    case GIMPLE_NOP:
    case GIMPLE_PREDICT:
    case GIMPLE_GOTO:
    case GIMPLE_COND:
    case GIMPLE_RETURN:
      return true;

    case GIMPLE_ASSIGN:
      return (get_base_address (gimple_assign_lhs (stmt))
	      != DECL_RESULT (current_function_decl));

    case GIMPLE_CALL:
      return (gimple_call_internal_p (stmt, IFN_HERBCEPTION_RETURN_PAYLOAD));

    default:
      return false;
    }
}

/* True if BB may write its payload pieces straight into the return
   registers.  Every successor must be a call-free return block: those
   pieces then stay put until `ret'.  */

static bool
hb_block_direct_p (basic_block bb)
{
  bool seen = false;

  for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
       gsi_next (&gsi))
    {
      gimple *stmt = gsi_stmt (gsi);

      if (is_gimple_call (stmt)
	  && gimple_call_internal_p (stmt, IFN_HERBCEPTION_RETURN_PAYLOAD))
	{
	  seen = true;
	  continue;
	}

      if (seen && !hb_after_direct_payload_p (stmt))
	return false;
    }

  if (!seen)
    return true;

  gimple *last = last_nondebug_stmt (bb);
  if (last && gimple_code (last) == GIMPLE_RETURN)
    return true;

  edge e;
  edge_iterator ei;
  FOR_EACH_EDGE (e, ei, bb->succs)
    {
      if (e->flags & (EDGE_ABNORMAL | EDGE_EH | EDGE_FAKE))
	return false;

      gimple *dlast = last_nondebug_stmt (e->dest);
      if (!dlast || gimple_code (dlast) != GIMPLE_RETURN)
	return false;

      for (gimple_stmt_iterator gsi = gsi_start_bb (e->dest);
	   !gsi_end_p (gsi); gsi_next (&gsi))
	if (is_gimple_call (gsi_stmt (gsi)) || gimple_code (gsi_stmt (gsi))
						  == GIMPLE_ASM)
	  return false;
    }

  return true;
}

/* True if every block that writes the payload can write it straight
   into the return registers.  The transform is all-or-nothing, since
   the carrier is chosen once for the whole function.  */

static bool
hb_direct_channel_p (function *fun)
{
  basic_block bb;
  FOR_EACH_BB_FN (bb, fun)
    if (!hb_block_direct_p (bb))
      return false;
  return true;
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

  /* When nothing can clobber the return registers between a payload
     store and the return, the pieces can be written there directly and
     the epilogue need not assemble the channel at all.  */
  fun->herbceptions_direct_channel
    = hb_direct_channel_p (fun);
  if (!fun->herbceptions_direct_channel)
    hb_complete_return_channel (fun, slot);

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
