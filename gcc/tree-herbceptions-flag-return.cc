/* Fold the herbceptions return discriminant to a constant on each edge.
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

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "backend.h"
#include "insn-codes.h"
#include "rtl.h"
#include "tree.h"
#include "gimple.h"
#include "cfghooks.h"
#include "tree-pass.h"
#include "ssa.h"
#include "tree-ssa.h"
#include "optabs-tree.h"
#include "insn-config.h"
#include "gimple-pretty-print.h"
#include "fold-const.h"
#include "cfganal.h"
#include "gimple-iterator.h"
#include "tree-cfg.h"
#include "internal-fn.h"
#include "cfgloop.h"
#include "tree-dfa.h"
#include "predict.h"

/* A 'throws' function returns {payload, discriminant}: the front end
   stores the discriminant into an artificial volatile local before every
   return, and expand_function_end reloads it into the target flag
   register on the shared return edge, after all cleanups have run.

   The stores are constants, but the CFG merge in the middle end turns
   the value reaching the return into a PHI, so the reload materializes a
   value only to turn it back into the flag.  This pass gives every edge
   into a return block its own copy of the block and replaces the reload
   with an IFN_HERBCEPTION_SET_FAILED that sets the flag directly from
   the constant the edge implies.  The register the value would have
   been materialized in is then dead.

   The transform is all-or-nothing: unless every path to the exit block
   ends in a return that sets the flag, expand_function_end keeps
   emitting the load.  */

namespace {

/* Return the last store to DECL in BB, or NULL if there is none.  */

static gassign *
last_store_to (basic_block bb, tree decl)
{
  gassign *last = NULL;
  for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
       gsi_next (&gsi))
    {
      gassign *assign = dyn_cast <gassign *> (gsi_stmt (gsi));
      if (!assign)
	continue;
      tree lhs = gimple_assign_lhs (assign);
      if (TREE_CODE (lhs) == SSA_NAME)
	continue;
      if (get_base_address (lhs) == decl)
	last = assign;
    }
  return last;
}

/* Return the value 0 or 1 that V is known to take on edge E, or -1 if it
   is not known.  V is a boolean: a PHI argument, a boolean negation, or
   the condition of the branch that leads into the merge.  */

static int
bool_value_on_edge (tree v, edge e, int depth)
{
  if (!v || depth > 16)
    return -1;

  if (TREE_CODE (v) == INTEGER_CST)
    return integer_zerop (v) ? 0 : 1;

  if (TREE_CODE (v) != SSA_NAME)
    return -1;

  gimple *def = SSA_NAME_DEF_STMT (v);
  if (gassign *assign = dyn_cast <gassign *> (def))
    {
      tree_code code = gimple_assign_rhs_code (assign);
      if (code == TRUTH_NOT_EXPR)
	{
	  int inner = bool_value_on_edge (gimple_assign_rhs1 (assign), e,
					 depth + 1);
	  return inner < 0 ? -1 : !inner;
	}
      if ((code == EQ_EXPR || code == NE_EXPR)
	  && integer_zerop (gimple_assign_rhs2 (assign)))
	{
	  int inner = bool_value_on_edge (gimple_assign_rhs1 (assign), e,
					 depth + 1);
	  if (inner < 0)
	    return -1;
	  return code == NE_EXPR ? inner : !inner;
	}
    }

  /* The branch that enters the block may decide it.  */
  if (e->src == ENTRY_BLOCK_PTR_FOR_FN (cfun))
    return -1;
  gimple_stmt_iterator gsi = gsi_last_bb (e->src);
  if (!gsi_end_p (gsi))
    if (gcond *cond = dyn_cast <gcond *> (gsi_stmt (gsi)))
      {
	tree lhs = gimple_cond_lhs (cond);
	tree rhs = gimple_cond_rhs (cond);
	tree_code code = gimple_cond_code (cond);
	tree op = NULL_TREE;
	bool negated = false;
	if ((code == NE_EXPR || code == EQ_EXPR) && integer_zerop (rhs))
	  {
	    op = lhs;
	    negated = code == EQ_EXPR;
	  }
	else if ((code == NE_EXPR || code == EQ_EXPR) && integer_zerop (lhs))
	  {
	    op = rhs;
	    negated = code == EQ_EXPR;
	  }
	if (op == v)
	  {
	    bool on_true = (e->flags & EDGE_TRUE_VALUE) != 0;
	    return on_true != negated ? 1 : 0;
	  }
	return -1;
      }

  /* Otherwise look further back along a linear path.  SimplifyCFG leaves
     empty merge blocks in front of the return, and the branch that decides
     V can sit a few blocks up the single-successor chain.  V is an SSA
     name, so no block on the chain can change it; the branch outcome is
     still the one that selected this path.  */
  if (EDGE_COUNT (e->src->succs) == 1 && EDGE_COUNT (e->src->preds) == 1)
    {
      edge pe = single_pred_edge (e->src);
      if (pe->flags & (EDGE_ABNORMAL | EDGE_FAKE))
	return -1;
      return bool_value_on_edge (v, pe, depth + 1);
    }
  return -1;
}

/* Return the value that reaches return block BB through VAL on edge E,
   resolving BB's own PHI for that edge.  */

static tree
flag_value_on_edge (tree val, basic_block bb, edge e)
{
  if (TREE_CODE (val) == SSA_NAME)
    if (gphi *phi = dyn_cast <gphi *> (SSA_NAME_DEF_STMT (val)))
      if (gimple_bb (phi) == bb)
	for (unsigned i = 0; i < gimple_phi_num_args (phi); i++)
	  {
	    edge pe = gimple_phi_arg_edge (phi, i);
	    if (pe->src == e->src && pe->dest == e->dest)
	      return gimple_phi_arg_def (phi, i);
	  }
  return val;
}

/* Return the discriminant value that reaches edge E of return block BB
   through VAL, the value stored into the failure slot, or NULL if it is
   not a constant on that edge.  */

static tree
resolve_flag_on_edge (tree val, basic_block bb, edge e)
{
  tree v = flag_value_on_edge (val, bb, e);
  if (TREE_CODE (v) == INTEGER_CST)
    return v;

  int known = bool_value_on_edge (v, e, 0);
  if (known < 0)
    return NULL_TREE;
  return build_int_cst (boolean_type_node, known);
}

/* Return true if BB does nothing but forward control to its single
   successor.  Such a block carries no work, so it can be duplicated on
   each incoming edge to give the return one edge per path.  */

static bool
transparent_block_p (basic_block bb)
{
  if (EDGE_COUNT (bb->succs) != 1)
    return false;
  edge e = single_succ_edge (bb);
  if (e->flags & (EDGE_ABNORMAL | EDGE_EH | EDGE_FAKE))
    return false;
  for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
       gsi_next (&gsi))
    {
      gimple *stmt = gsi_stmt (gsi);
      if (is_gimple_debug (stmt) || gimple_code (stmt) == GIMPLE_LABEL)
	continue;
      return gimple_code (stmt) == GIMPLE_GOTO;
    }
  return true;
}

/* Bits describing which values of the failure slot can reach the end of
   a block.  */

enum slot_value
{
  SLOT_VALUE_0 = 1,
  SLOT_VALUE_1 = 2,
  SLOT_VALUE_OTHER = 4
};

/* Return the mask of values the failure slot SLOT can have at the end of
   BB, given IN as the mask at its entry.  */

static unsigned
slot_values_after (basic_block bb, tree slot, unsigned in)
{
  gassign *store = last_store_to (bb, slot);
  if (!store)
    return in;

  tree val = gimple_assign_rhs1 (store);
  if (TREE_CODE (val) != INTEGER_CST)
    return SLOT_VALUE_OTHER;
  return integer_zerop (val) ? SLOT_VALUE_0 : SLOT_VALUE_1;
}

/* Compute, for every block, the mask of failure-slot values that can
   reach its end.  The front end stores only constants into the slot, so
   this is a small union dataflow problem; a block whose mask is a single
   value has a statically known discriminant at its end.  */

static void
compute_slot_values (function *fun, tree slot, unsigned *out)
{
  unsigned n = last_basic_block_for_fn (fun);
  unsigned *queued = XCNEWVEC (unsigned, n);
  bool *visited = XCNEWVEC (bool, n);
  auto_vec <basic_block> worklist;
  basic_block entry = ENTRY_BLOCK_PTR_FOR_FN (fun);

  out[entry->index] = slot_values_after (entry, slot, 0);
  visited[entry->index] = true;
  queued[entry->index] = 1;
  worklist.safe_push (entry);

  while (!worklist.is_empty ())
    {
      basic_block bb = worklist.pop ();
      queued[bb->index] = 0;

      if (bb != entry)
	{
	  unsigned in = 0;
	  edge e;
	  edge_iterator ei;
	  FOR_EACH_EDGE (e, ei, bb->preds)
	    in |= out[e->src->index];
	  unsigned vals = slot_values_after (bb, slot, in);
	  if (visited[bb->index] && vals == out[bb->index])
	    continue;
	  out[bb->index] = vals;
	  visited[bb->index] = true;
	}

      edge e;
      edge_iterator ei;
      FOR_EACH_EDGE (e, ei, bb->succs)
	{
	  if (e->flags & EDGE_FAKE)
	    continue;
	  basic_block dest = e->dest;
	  if (dest == EXIT_BLOCK_PTR_FOR_FN (fun))
	    continue;
	  if (!queued[dest->index])
	    {
	      queued[dest->index] = 1;
	      worklist.safe_push (dest);
	    }
	}
    }

  free (queued);
  free (visited);
}

/* Insert IFN_HERBCEPTION_SET_FAILED (CST) just before the return of BB,
   after every cleanup.  */

static void
insert_set_failed (basic_block bb, tree cst)
{
  gimple_stmt_iterator gsi = gsi_last_nondebug_bb (bb);
  gcc_checking_assert (gimple_code (gsi_stmt (gsi)) == GIMPLE_RETURN);
  gcall *call = gimple_build_call_internal (IFN_HERBCEPTION_SET_FAILED,
					    1, cst);
  gimple_set_location (call, gimple_location (gsi_stmt (gsi)));
  gsi_insert_before (&gsi, call, GSI_SAME_STMT);
}

/* One edge into a return block, with the discriminant it implies.  */

struct edge_flag
{
  edge e;
  basic_block ret_bb;
  tree cst;
  bool duplicate;
};

/* Resolve every edge that reaches a return block.  When an edge comes from
   a transparent merge block, the branch that decided the flag is further
   up and the same value reaches the return from more than one path: check
   that every one of those paths implies a constant and report NEED_SPLIT
   so the caller can give each path an edge of its own.  */

static bool
collect_edges (function *fun, tree slot, basic_block exit_bb,
	       unsigned *slot_out, auto_vec <edge_flag, 8> &flags,
	       bool *need_split)
{
  *need_split = false;
  flags.truncate (0);

  edge e;
  edge_iterator ei;
  FOR_EACH_EDGE (e, ei, exit_bb->preds)
    {
      /* A fake edge stands for an infinite loop or a noreturn call: it
	 never reaches the return label, so it needs no flag.  */
      if (e->flags & EDGE_FAKE)
	continue;
      basic_block bb = e->src;
      if (!dyn_cast <greturn *> (last_nondebug_stmt (bb)))
	return false;
      if (EDGE_COUNT (bb->preds) == 0)
	continue;
      if (bb->loop_father && bb->loop_father->header == bb)
	return false;

      gassign *store = last_store_to (bb, slot);
      if (store && !gimple_assign_single_p (store))
	return false;
      tree val = store ? gimple_assign_rhs1 (store) : NULL_TREE;

      bool duplicate = EDGE_COUNT (bb->preds) > 1;
      edge pe;
      edge_iterator pei;
      FOR_EACH_EDGE (pe, pei, bb->preds)
	{
	  if (duplicate
	      && (pe->src == ENTRY_BLOCK_PTR_FOR_FN (fun)
		  || !can_duplicate_block_on_edge_p (pe)))
	    return false;

	  tree cst = NULL_TREE;
	  if (store)
	    cst = resolve_flag_on_edge (val, bb, pe);
	  else
	    {
	      unsigned vals = slot_out[pe->src->index];
	      if (vals == SLOT_VALUE_0)
		cst = build_int_cst (boolean_type_node, 0);
	      else if (vals == SLOT_VALUE_1)
		cst = build_int_cst (boolean_type_node, 1);
	    }
	  if (cst)
	    {
	      flags.safe_push ({pe, bb, cst, duplicate});
	      continue;
	    }
	  if (!store)
	    return false;

	  tree v = flag_value_on_edge (val, bb, pe);
	  if (!v || !transparent_block_p (pe->src)
	      || EDGE_COUNT (pe->src->preds) < 2)
	    return false;
	  edge qe;
	  edge_iterator qei;
	  FOR_EACH_EDGE (qe, qei, pe->src->preds)
	    if (qe->src == ENTRY_BLOCK_PTR_FOR_FN (fun)
		|| !can_duplicate_block_on_edge_p (qe)
		|| bool_value_on_edge (v, qe, 0) < 0)
	      return false;
	  *need_split = true;
	}
    }
  return true;
}

/* Split every transparent merge block in front of a return block, so that
   each path reaches the return through an edge of its own.  */

static void
split_transparent_predecessors (basic_block exit_bb)
{
  auto_vec <basic_block, 8> rets;
  edge e;
  edge_iterator ei;
  FOR_EACH_EDGE (e, ei, exit_bb->preds)
    if (!(e->flags & EDGE_FAKE))
      rets.safe_push (e->src);

  initialize_original_copy_tables ();
  for (basic_block ret : rets)
    {
      auto_vec <basic_block, 8> preds;
      edge pe;
      edge_iterator pei;
      FOR_EACH_EDGE (pe, pei, ret->preds)
	preds.safe_push (pe->src);

      for (basic_block pred : preds)
	{
	  if (!transparent_block_p (pred) || EDGE_COUNT (pred->preds) < 2)
	    continue;

	  auto_vec <edge, 8> ins;
	  FOR_EACH_EDGE (pe, pei, pred->preds)
	    ins.safe_push (pe);
	  for (edge qe : ins)
	    {
	      basic_block copy = duplicate_block (pred, qe, qe->src);
	      add_phi_args_after_copy (&copy, 1, NULL);
	    }
	  delete_basic_block (pred);
	}
    }
  free_original_copy_tables ();
}

static unsigned int
herbceptions_flag_return (function *fun)
{
  tree slot = fun->herbceptions_failed_slot;
  basic_block exit_bb = EXIT_BLOCK_PTR_FOR_FN (fun);

  if (!slot || EDGE_COUNT (exit_bb->preds) == 0)
    return 0;

  /* The dataflow below cannot see stores made through an escaped
     address.  The front end keeps the slot private.  */
  if (TREE_ADDRESSABLE (slot))
    return 0;

  /* Work out which constants can reach each block's end.  The stores are
     not necessarily in the return block itself: for a throw inside a
     loop, the front end stores the flag in the throwing block and then
     merges with the normal loop exit in a store-less return block.  */
  auto_vec <unsigned> slot_out;
  slot_out.safe_grow_cleared (last_basic_block_for_fn (fun));
  compute_slot_values (fun, slot, slot_out.address ());

  /* Every path to the exit block must run through a return whose flag we
     can rewrite; otherwise the shared load has to stay.  */
  auto_vec <edge_flag, 8> flags;
  bool need_split = false;
  if (!collect_edges (fun, slot, exit_bb, slot_out.address (), flags,
		      &need_split))
    return 0;

  bool cfg_changed = false;
  if (need_split)
    {
      split_transparent_predecessors (exit_bb);
      cfg_changed = true;

      /* The CFG changed, so redo the value dataflow and the collection on
	 the new edges.  Every edge now resolves directly.  */
      slot_out.release ();
      slot_out.safe_grow_cleared (last_basic_block_for_fn (fun));
      compute_slot_values (fun, slot, slot_out.address ());
      if (!collect_edges (fun, slot, exit_bb, slot_out.address (), flags,
			  &need_split))
	{
	  /* The splitting above is semantics-preserving; give up the fold.  */
	  loops_state_set (LOOPS_NEED_FIXUP);
	  return TODO_cleanup_cfg;
	}
    }

  if (flags.is_empty ())
    return cfg_changed ? TODO_cleanup_cfg : 0;

  /* Past this point nothing can fail.  Each edge gets the return block
     with the flag set as its last statement.  */
  bool changed = false;
  initialize_original_copy_tables ();
  for (edge_flag &f : flags)
    {
      basic_block target = f.ret_bb;
      if (f.duplicate)
	{
	  target = duplicate_block (f.ret_bb, f.e, f.e->src);
	  flush_pending_stmts (f.e);
	  add_phi_args_after_copy (&target, 1, NULL);
	  changed = true;
	}
      insert_set_failed (target, f.cst);
    }
  free_original_copy_tables ();

  fun->herbceptions_flag_folded = true;

  if (dump_file && (dump_flags & TDF_DETAILS))
    fprintf (dump_file, "Folded the herbceptions return flag on %u edge%s.\n",
	     flags.length (), flags.length () == 1 ? "" : "s");

  if (!changed)
    /* The inserted call has a virtual operand, so SSA form needs
       updating even when no block was duplicated.  */
    return TODO_update_ssa;
  loops_state_set (LOOPS_NEED_FIXUP);
  return TODO_update_ssa | TODO_cleanup_cfg;
}

} // anon namespace

const pass_data pass_data_herbceptions_flag_return =
{
  GIMPLE_PASS, /* type */
  "herbceptions", /* name */
  OPTGROUP_NONE, /* optinfo_flags */
  TV_NONE, /* tv_id */
  ( PROP_cfg | PROP_ssa ), /* properties_required */
  0, /* properties_provided */
  0, /* properties_destroyed */
  0, /* todo_flags_start */
  0, /* todo_flags_finish */
};

class pass_herbceptions_flag_return : public gimple_opt_pass
{
public:
  pass_herbceptions_flag_return (gcc::context *ctxt)
    : gimple_opt_pass (pass_data_herbceptions_flag_return, ctxt)
  {}

  /* opt_pass methods: */
  bool gate (function *fun) final override
    {
#ifdef HAVE_herbceptions_set_failed
      return (optimize > 0
	      && fun->herbceptions_failed_slot != NULL_TREE
	      && !optimize_function_for_size_p (fun));
#else
      return false;
#endif
    }

  unsigned int execute (function *fun) final override;
}; // class pass_herbceptions_flag_return

unsigned int
pass_herbceptions_flag_return::execute (function *fun)
{
  return herbceptions_flag_return (fun);
}

gimple_opt_pass *
make_pass_herbceptions_flag_return (gcc::context *ctxt)
{
  return new pass_herbceptions_flag_return (ctxt);
}
