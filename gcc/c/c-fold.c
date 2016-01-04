/* Support for fully folding sub-trees of an expression for C compiler.
   Copyright (C) 1992-2016 Free Software Foundation, Inc.

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
#include "target.h"
#include "function.h"
#include "bitmap.h"
#include "c-tree.h"
#include "intl.h"
#include "gimplify.h"

static tree c_fully_fold_internal (tree expr, bool, bool *, bool *, bool);

/* If DISABLE is true, stop issuing warnings.  This is used when
   parsing code that we know will not be executed.  This function may
   be called multiple times, and works as a stack.  */

static void
c_disable_warnings (bool disable)
{
  if (disable)
    {
      ++c_inhibit_evaluation_warnings;
      fold_defer_overflow_warnings ();
    }
}

/* If ENABLE is true, reenable issuing warnings.  */

static void
c_enable_warnings (bool enable)
{
  if (enable)
    {
      --c_inhibit_evaluation_warnings;
      fold_undefer_and_ignore_overflow_warnings ();
    }
}

/* Fully fold EXPR, an expression that was not folded (beyond integer
   constant expressions and null pointer constants) when being built
   up.  If IN_INIT, this is in a static initializer and certain
   changes are made to the folding done.  Clear *MAYBE_CONST if
   MAYBE_CONST is not NULL and EXPR is definitely not a constant
   expression because it contains an evaluated operator (in C99) or an
   operator outside of sizeof returning an integer constant (in C90)
   not permitted in constant expressions, or because it contains an
   evaluated arithmetic overflow.  (*MAYBE_CONST should typically be
   set to true by callers before calling this function.)  Return the
   folded expression.  Function arguments have already been folded
   before calling this function, as have the contents of SAVE_EXPR,
   TARGET_EXPR, BIND_EXPR, VA_ARG_EXPR, OBJ_TYPE_REF and
   C_MAYBE_CONST_EXPR.  */

tree
c_fully_fold (tree expr, bool in_init, bool *maybe_const)
{
  tree ret;
  tree eptype = NULL_TREE;
  bool dummy = true;
  bool maybe_const_itself = true;
  location_t loc = EXPR_LOCATION (expr);

  if (!maybe_const)
    maybe_const = &dummy;
  if (TREE_CODE (expr) == EXCESS_PRECISION_EXPR)
    {
      eptype = TREE_TYPE (expr);
      expr = TREE_OPERAND (expr, 0);
    }
  ret = c_fully_fold_internal (expr, in_init, maybe_const,
			       &maybe_const_itself, false);
  if (eptype)
    ret = fold_convert_loc (loc, eptype, ret);
  *maybe_const &= maybe_const_itself;
  return ret;
}

/* Internal helper for c_fully_fold.  EXPR and IN_INIT are as for
   c_fully_fold.  *MAYBE_CONST_OPERANDS is cleared because of operands
   not permitted, while *MAYBE_CONST_ITSELF is cleared because of
   arithmetic overflow (for C90, *MAYBE_CONST_OPERANDS is carried from
   both evaluated and unevaluated subexpressions while
   *MAYBE_CONST_ITSELF is carried from only evaluated
   subexpressions).  FOR_INT_CONST indicates if EXPR is an expression
   with integer constant operands, and if any of the operands doesn't
   get folded to an integer constant, don't fold the expression itself.  */

static tree
c_fully_fold_internal (tree expr, bool in_init, bool *maybe_const_operands,
		       bool *maybe_const_itself, bool for_int_const)
{
  tree ret = expr;
  enum tree_code code = TREE_CODE (expr);
  enum tree_code_class kind = TREE_CODE_CLASS (code);
  location_t loc = EXPR_LOCATION (expr);
  tree op0, op1, op2, op3;
  tree orig_op0, orig_op1, orig_op2;
  bool op0_const = true, op1_const = true, op2_const = true;
  bool op0_const_self = true, op1_const_self = true, op2_const_self = true;
  bool nowarning = TREE_NO_WARNING (expr);
  bool unused_p;
  source_range old_range;

  /* Constants, declarations, statements, errors, SAVE_EXPRs and
     anything else not counted as an expression cannot usefully be
     folded further at this point.  */
  if (!IS_EXPR_CODE_CLASS (kind)
      || kind == tcc_statement
      || code == SAVE_EXPR)
    return expr;

  if (IS_EXPR_CODE_CLASS (kind))
    old_range = EXPR_LOCATION_RANGE (expr);

  /* Operands of variable-length expressions (function calls) have
     already been folded, as have __builtin_* function calls, and such
     expressions cannot occur in constant expressions.  */
  if (kind == tcc_vl_exp)
    {
      *maybe_const_operands = false;
      ret = fold (expr);
      goto out;
    }

  if (code == C_MAYBE_CONST_EXPR)
    {
      tree pre = C_MAYBE_CONST_EXPR_PRE (expr);
      tree inner = C_MAYBE_CONST_EXPR_EXPR (expr);
      if (C_MAYBE_CONST_EXPR_NON_CONST (expr))
	*maybe_const_operands = false;
      if (C_MAYBE_CONST_EXPR_INT_OPERANDS (expr))
	{
	  *maybe_const_itself = false;
	  inner = c_fully_fold_internal (inner, in_init, maybe_const_operands,
					 maybe_const_itself, true);
	}
      if (pre && !in_init)
	ret = build2 (COMPOUND_EXPR, TREE_TYPE (expr), pre, inner);
      else
	ret = inner;
      goto out;
    }

  /* Assignment, increment, decrement, function call and comma
     operators, and statement expressions, cannot occur in constant
     expressions if evaluated / outside of sizeof.  (Function calls
     were handled above, though VA_ARG_EXPR is treated like a function
     call here, and statement expressions are handled through
     C_MAYBE_CONST_EXPR to avoid folding inside them.)  */
  switch (code)
    {
    case MODIFY_EXPR:
    case PREDECREMENT_EXPR:
    case PREINCREMENT_EXPR:
    case POSTDECREMENT_EXPR:
    case POSTINCREMENT_EXPR:
    case COMPOUND_EXPR:
      *maybe_const_operands = false;
      break;

    case VA_ARG_EXPR:
    case TARGET_EXPR:
    case BIND_EXPR:
    case OBJ_TYPE_REF:
      *maybe_const_operands = false;
      ret = fold (expr);
      goto out;

    default:
      break;
    }

  /* Fold individual tree codes as appropriate.  */
  switch (code)
    {
    case COMPOUND_LITERAL_EXPR:
      /* Any non-constancy will have been marked in a containing
	 C_MAYBE_CONST_EXPR; there is no more folding to do here.  */
      goto out;

    case COMPONENT_REF:
      orig_op0 = op0 = TREE_OPERAND (expr, 0);
      op1 = TREE_OPERAND (expr, 1);
      op2 = TREE_OPERAND (expr, 2);
      op0 = c_fully_fold_internal (op0, in_init, maybe_const_operands,
				   maybe_const_itself, for_int_const);
      STRIP_TYPE_NOPS (op0);
      if (op0 != orig_op0)
	ret = build3 (COMPONENT_REF, TREE_TYPE (expr), op0, op1, op2);
      if (ret != expr)
	{
	  TREE_READONLY (ret) = TREE_READONLY (expr);
	  TREE_THIS_VOLATILE (ret) = TREE_THIS_VOLATILE (expr);
	}
      goto out;

    case ARRAY_REF:
      orig_op0 = op0 = TREE_OPERAND (expr, 0);
      orig_op1 = op1 = TREE_OPERAND (expr, 1);
      op2 = TREE_OPERAND (expr, 2);
      op3 = TREE_OPERAND (expr, 3);
      op0 = c_fully_fold_internal (op0, in_init, maybe_const_operands,
				   maybe_const_itself, for_int_const);
      STRIP_TYPE_NOPS (op0);
      op1 = c_fully_fold_internal (op1, in_init, maybe_const_operands,
				   maybe_const_itself, for_int_const);
      STRIP_TYPE_NOPS (op1);
      op1 = decl_constant_value_for_optimization (op1);
      if (op0 != orig_op0 || op1 != orig_op1)
	ret = build4 (ARRAY_REF, TREE_TYPE (expr), op0, op1, op2, op3);
      if (ret != expr)
	{
	  TREE_READONLY (ret) = TREE_READONLY (expr);
	  TREE_SIDE_EFFECTS (ret) = TREE_SIDE_EFFECTS (expr);
	  TREE_THIS_VOLATILE (ret) = TREE_THIS_VOLATILE (expr);
	}
      ret = fold (ret);
      goto out;

    case COMPOUND_EXPR:
    case MODIFY_EXPR:
    case PREDECREMENT_EXPR:
    case PREINCREMENT_EXPR:
    case POSTDECREMENT_EXPR:
    case POSTINCREMENT_EXPR:
    case PLUS_EXPR:
    case MINUS_EXPR:
    case MULT_EXPR:
    case POINTER_PLUS_EXPR:
    case TRUNC_DIV_EXPR:
    case CEIL_DIV_EXPR:
    case FLOOR_DIV_EXPR:
    case TRUNC_MOD_EXPR:
    case RDIV_EXPR:
    case EXACT_DIV_EXPR:
    case LSHIFT_EXPR:
    case RSHIFT_EXPR:
    case BIT_IOR_EXPR:
    case BIT_XOR_EXPR:
    case BIT_AND_EXPR:
    case LT_EXPR:
    case LE_EXPR:
    case GT_EXPR:
    case GE_EXPR:
    case EQ_EXPR:
    case NE_EXPR:
    case COMPLEX_EXPR:
    case TRUTH_AND_EXPR:
    case TRUTH_OR_EXPR:
    case TRUTH_XOR_EXPR:
    case UNORDERED_EXPR:
    case ORDERED_EXPR:
    case UNLT_EXPR:
    case UNLE_EXPR:
    case UNGT_EXPR:
    case UNGE_EXPR:
    case UNEQ_EXPR:
      /* Binary operations evaluating both arguments (increment and
	 decrement are binary internally in GCC).  */
      orig_op0 = op0 = TREE_OPERAND (expr, 0);
      orig_op1 = op1 = TREE_OPERAND (expr, 1);
      op0 = c_fully_fold_internal (op0, in_init, maybe_const_operands,
				   maybe_const_itself, for_int_const);
      STRIP_TYPE_NOPS (op0);
      if (code != MODIFY_EXPR
	  && code != PREDECREMENT_EXPR
	  && code != PREINCREMENT_EXPR
	  && code != POSTDECREMENT_EXPR
	  && code != POSTINCREMENT_EXPR)
	op0 = decl_constant_value_for_optimization (op0);
      /* The RHS of a MODIFY_EXPR was fully folded when building that
	 expression for the sake of conversion warnings.  */
      if (code != MODIFY_EXPR)
	op1 = c_fully_fold_internal (op1, in_init, maybe_const_operands,
				     maybe_const_itself, for_int_const);
      STRIP_TYPE_NOPS (op1);
      op1 = decl_constant_value_for_optimization (op1);

      if (for_int_const && (TREE_CODE (op0) != INTEGER_CST
			    || TREE_CODE (op1) != INTEGER_CST))
	goto out;

      if (op0 != orig_op0 || op1 != orig_op1 || in_init)
	ret = in_init
	  ? fold_build2_initializer_loc (loc, code, TREE_TYPE (expr), op0, op1)
	  : fold_build2_loc (loc, code, TREE_TYPE (expr), op0, op1);
      else
	ret = fold (expr);
      if (TREE_OVERFLOW_P (ret)
	  && !TREE_OVERFLOW_P (op0)
	  && !TREE_OVERFLOW_P (op1))
	overflow_warning (EXPR_LOC_OR_LOC (expr, input_location), ret);
      if (code == LSHIFT_EXPR
	  && TREE_CODE (orig_op0) != INTEGER_CST
	  && TREE_CODE (TREE_TYPE (orig_op0)) == INTEGER_TYPE
	  && TREE_CODE (op0) == INTEGER_CST
	  && c_inhibit_evaluation_warnings == 0
	  && tree_int_cst_sgn (op0) < 0)
	warning_at (loc, OPT_Wshift_negative_value,
		    "left shift of negative value");
      if ((code == LSHIFT_EXPR || code == RSHIFT_EXPR)
	  && TREE_CODE (orig_op1) != INTEGER_CST
	  && TREE_CODE (op1) == INTEGER_CST
	  && (TREE_CODE (TREE_TYPE (orig_op0)) == INTEGER_TYPE
	      || TREE_CODE (TREE_TYPE (orig_op0)) == FIXED_POINT_TYPE)
	  && TREE_CODE (TREE_TYPE (orig_op1)) == INTEGER_TYPE
	  && c_inhibit_evaluation_warnings == 0)
	{
	  if (tree_int_cst_sgn (op1) < 0)
	    warning_at (loc, OPT_Wshift_count_negative,
			(code == LSHIFT_EXPR
			 ? G_("left shift count is negative")
			 : G_("right shift count is negative")));
	  else if (compare_tree_int (op1,
				     TYPE_PRECISION (TREE_TYPE (orig_op0)))
		   >= 0)
	    warning_at (loc, OPT_Wshift_count_overflow,
			(code == LSHIFT_EXPR
			 ? G_("left shift count >= width of type")
			 : G_("right shift count >= width of type")));
	}
      if (code == LSHIFT_EXPR
	  /* If either OP0 has been folded to INTEGER_CST...  */
	  && ((TREE_CODE (orig_op0) != INTEGER_CST
	       && TREE_CODE (TREE_TYPE (orig_op0)) == INTEGER_TYPE
	       && TREE_CODE (op0) == INTEGER_CST)
	      /* ...or if OP1 has been folded to INTEGER_CST...  */
	      || (TREE_CODE (orig_op1) != INTEGER_CST
		  && TREE_CODE (TREE_TYPE (orig_op1)) == INTEGER_TYPE
		  && TREE_CODE (op1) == INTEGER_CST))
	  && c_inhibit_evaluation_warnings == 0)
	/* ...then maybe we can detect an overflow.  */
	maybe_warn_shift_overflow (loc, op0, op1);
      if ((code == TRUNC_DIV_EXPR
	   || code == CEIL_DIV_EXPR
	   || code == FLOOR_DIV_EXPR
	   || code == EXACT_DIV_EXPR
	   || code == TRUNC_MOD_EXPR)
	  && TREE_CODE (orig_op1) != INTEGER_CST
	  && TREE_CODE (op1) == INTEGER_CST
	  && (TREE_CODE (TREE_TYPE (orig_op0)) == INTEGER_TYPE
	      || TREE_CODE (TREE_TYPE (orig_op0)) == FIXED_POINT_TYPE)
	  && TREE_CODE (TREE_TYPE (orig_op1)) == INTEGER_TYPE)
	warn_for_div_by_zero (loc, op1);
      goto out;

    case INDIRECT_REF:
    case FIX_TRUNC_EXPR:
    case FLOAT_EXPR:
    CASE_CONVERT:
    case ADDR_SPACE_CONVERT_EXPR:
    case VIEW_CONVERT_EXPR:
    case NON_LVALUE_EXPR:
    case NEGATE_EXPR:
    case BIT_NOT_EXPR:
    case TRUTH_NOT_EXPR:
    case ADDR_EXPR:
    case CONJ_EXPR:
    case REALPART_EXPR:
    case IMAGPART_EXPR:
      /* Unary operations.  */
      orig_op0 = op0 = TREE_OPERAND (expr, 0);
      op0 = c_fully_fold_internal (op0, in_init, maybe_const_operands,
				   maybe_const_itself, for_int_const);
      STRIP_TYPE_NOPS (op0);
      if (code != ADDR_EXPR && code != REALPART_EXPR && code != IMAGPART_EXPR)
	op0 = decl_constant_value_for_optimization (op0);

      if (for_int_const && TREE_CODE (op0) != INTEGER_CST)
	goto out;

      /* ??? Cope with user tricks that amount to offsetof.  The middle-end is
	 not prepared to deal with them if they occur in initializers.  */
      if (op0 != orig_op0
	  && code == ADDR_EXPR
	  && (op1 = get_base_address (op0)) != NULL_TREE
	  && INDIRECT_REF_P (op1)
	  && TREE_CONSTANT (TREE_OPERAND (op1, 0)))
	ret = fold_convert_loc (loc, TREE_TYPE (expr), fold_offsetof_1 (op0));
      else if (op0 != orig_op0 || in_init)
	ret = in_init
	  ? fold_build1_initializer_loc (loc, code, TREE_TYPE (expr), op0)
	  : fold_build1_loc (loc, code, TREE_TYPE (expr), op0);
      else
	ret = fold (expr);
      if (code == INDIRECT_REF
	  && ret != expr
	  && INDIRECT_REF_P (ret))
	{
	  TREE_READONLY (ret) = TREE_READONLY (expr);
	  TREE_SIDE_EFFECTS (ret) = TREE_SIDE_EFFECTS (expr);
	  TREE_THIS_VOLATILE (ret) = TREE_THIS_VOLATILE (expr);
	}
      switch (code)
	{
	case FIX_TRUNC_EXPR:
	case FLOAT_EXPR:
	CASE_CONVERT:
	  /* Don't warn about explicit conversions.  We will already
	     have warned about suspect implicit conversions.  */
	  break;

	default:
	  if (TREE_OVERFLOW_P (ret) && !TREE_OVERFLOW_P (op0))
	    overflow_warning (EXPR_LOCATION (expr), ret);
	  break;
	}
      goto out;

    case TRUTH_ANDIF_EXPR:
    case TRUTH_ORIF_EXPR:
      /* Binary operations not necessarily evaluating both
	 arguments.  */
      orig_op0 = op0 = TREE_OPERAND (expr, 0);
      orig_op1 = op1 = TREE_OPERAND (expr, 1);
      op0 = c_fully_fold_internal (op0, in_init, &op0_const, &op0_const_self,
				   for_int_const);
      STRIP_TYPE_NOPS (op0);

      unused_p = (op0 == (code == TRUTH_ANDIF_EXPR
			  ? truthvalue_false_node
			  : truthvalue_true_node));
      c_disable_warnings (unused_p);
      op1 = c_fully_fold_internal (op1, in_init, &op1_const, &op1_const_self,
				   for_int_const);
      STRIP_TYPE_NOPS (op1);
      c_enable_warnings (unused_p);

      if (for_int_const
	  && (TREE_CODE (op0) != INTEGER_CST
	      /* Require OP1 be an INTEGER_CST only if it's evaluated.  */
	      || (!unused_p && TREE_CODE (op1) != INTEGER_CST)))
	goto out;

      if (op0 != orig_op0 || op1 != orig_op1 || in_init)
	ret = in_init
	  ? fold_build2_initializer_loc (loc, code, TREE_TYPE (expr), op0, op1)
	  : fold_build2_loc (loc, code, TREE_TYPE (expr), op0, op1);
      else
	ret = fold (expr);
      *maybe_const_operands &= op0_const;
      *maybe_const_itself &= op0_const_self;
      if (!(flag_isoc99
	    && op0_const
	    && op0_const_self
	    && (code == TRUTH_ANDIF_EXPR
		? op0 == truthvalue_false_node
		: op0 == truthvalue_true_node)))
	*maybe_const_operands &= op1_const;
      if (!(op0_const
	    && op0_const_self
	    && (code == TRUTH_ANDIF_EXPR
		? op0 == truthvalue_false_node
		: op0 == truthvalue_true_node)))
	*maybe_const_itself &= op1_const_self;
      goto out;

    case COND_EXPR:
      orig_op0 = op0 = TREE_OPERAND (expr, 0);
      orig_op1 = op1 = TREE_OPERAND (expr, 1);
      orig_op2 = op2 = TREE_OPERAND (expr, 2);
      op0 = c_fully_fold_internal (op0, in_init, &op0_const, &op0_const_self,
				   for_int_const);

      STRIP_TYPE_NOPS (op0);
      c_disable_warnings (op0 == truthvalue_false_node);
      op1 = c_fully_fold_internal (op1, in_init, &op1_const, &op1_const_self,
				   for_int_const);
      STRIP_TYPE_NOPS (op1);
      c_enable_warnings (op0 == truthvalue_false_node);

      c_disable_warnings (op0 == truthvalue_true_node);
      op2 = c_fully_fold_internal (op2, in_init, &op2_const, &op2_const_self,
				   for_int_const);
      STRIP_TYPE_NOPS (op2);
      c_enable_warnings (op0 == truthvalue_true_node);

      if (for_int_const
	  && (TREE_CODE (op0) != INTEGER_CST
	      /* Only the evaluated operand must be an INTEGER_CST.  */
	      || (op0 == truthvalue_true_node
		  ? TREE_CODE (op1) != INTEGER_CST
		  : TREE_CODE (op2) != INTEGER_CST)))
	goto out;

      if (op0 != orig_op0 || op1 != orig_op1 || op2 != orig_op2)
	ret = fold_build3_loc (loc, code, TREE_TYPE (expr), op0, op1, op2);
      else
	ret = fold (expr);
      *maybe_const_operands &= op0_const;
      *maybe_const_itself &= op0_const_self;
      if (!(flag_isoc99
	    && op0_const
	    && op0_const_self
	    && op0 == truthvalue_false_node))
	*maybe_const_operands &= op1_const;
      if (!(op0_const
	    && op0_const_self
	    && op0 == truthvalue_false_node))
	*maybe_const_itself &= op1_const_self;
      if (!(flag_isoc99
	    && op0_const
	    && op0_const_self
	    && op0 == truthvalue_true_node))
	*maybe_const_operands &= op2_const;
      if (!(op0_const
	    && op0_const_self
	    && op0 == truthvalue_true_node))
	*maybe_const_itself &= op2_const_self;
      goto out;

    case EXCESS_PRECISION_EXPR:
      /* Each case where an operand with excess precision may be
	 encountered must remove the EXCESS_PRECISION_EXPR around
	 inner operands and possibly put one around the whole
	 expression or possibly convert to the semantic type (which
	 c_fully_fold does); we cannot tell at this stage which is
	 appropriate in any particular case.  */
      gcc_unreachable ();

    default:
      /* Various codes may appear through folding built-in functions
	 and their arguments.  */
      goto out;
    }

 out:
  /* Some folding may introduce NON_LVALUE_EXPRs; all lvalue checks
     have been done by this point, so remove them again.  */
  nowarning |= TREE_NO_WARNING (ret);
  STRIP_TYPE_NOPS (ret);
  if (nowarning && !TREE_NO_WARNING (ret))
    {
      if (!CAN_HAVE_LOCATION_P (ret))
	ret = build1 (NOP_EXPR, TREE_TYPE (ret), ret);
      TREE_NO_WARNING (ret) = 1;
    }
  if (ret != expr)
    {
      protected_set_expr_location (ret, loc);
      if (IS_EXPR_CODE_CLASS (kind))
	set_source_range (ret, old_range.m_start, old_range.m_finish);
    }
  return ret;
}

/* If not optimizing, EXP is not a VAR_DECL, or EXP has array type,
   return EXP.  Otherwise, return either EXP or its known constant
   value (if it has one), but return EXP if EXP has mode BLKmode.  ???
   Is the BLKmode test appropriate?  */

tree
decl_constant_value_for_optimization (tree exp)
{
  tree ret;

  if (!optimize
      || !VAR_P (exp)
      || TREE_CODE (TREE_TYPE (exp)) == ARRAY_TYPE
      || DECL_MODE (exp) == BLKmode)
    return exp;

  ret = decl_constant_value (exp);
  /* Avoid unwanted tree sharing between the initializer and current
     function's body where the tree can be modified e.g. by the
     gimplifier.  */
  if (ret != exp && TREE_STATIC (exp))
    ret = unshare_expr (ret);
  return ret;
}
