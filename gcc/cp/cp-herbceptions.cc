/* Herbceptions error-channel lowering for the C++ front end.

   The parser builds HERBCEPTION_* tree nodes for the deterministic
   error-channel language forms.  Before the body is genericized, this
   pass rewrites them into ordinary GENERIC that models the
   {value_or_error, failed_flag} channel ABI:

     T f() throws;
     T f() return_failure{E};

   Every spec'd function gets two locals:

     union { T value; P error; } __hb_result;   (P = __cxx_std_error
     or E; for sret/void results the payload is just P)
     bool __hb_failed;

   `return E' becomes `__hb_result.value = E; __hb_failed = 0;
   return;', `throw throws e' / `return_failure e' become
   `__hb_result.error = conv(e); __hb_failed = 1; return;', and calls
   to spec'd functions wrapped in HERBCEPTION_TRY_EXPR hoist a real
   call plus internal reads of the return-register union and the
   flag-register discriminant as pre-statements, with a
   context-dependent failure route (propagate out the channel, or
   jump to a `catch throws' dispatch).  The epilogue moves __hb_result
   into the return registers and the backend fuses `__hb_failed' into
   the flag write in front of `ret'.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "cp-tree.h"
#include "internal-fn.h"
#include "function.h"
#include "stor-layout.h"
#include "tree-iterator.h"
#include "target.h"
#include "langhooks.h"
#include "stringpool.h"
#include "varasm.h"
#include "print-tree.h"

struct hb_ctx
{
  tree fndecl;
  tree hspec;		/* HERBCEPTION_SPEC or NULL_TREE.  */
  tree error_type;	/* Channel payload type: __cxx_std_error or E.  */
  tree union_type;	/* union { T value; P error; } for this fn.  */
  tree result_var;	/* __hb_result local.  */
  tree failed_var;	/* __hb_failed local.  */

  /* Innermost enclosing `catch throws' dispatch, if any: calls route
     failures by binding HANDLER_VAR and jumping to DISP_LABEL.  */
  tree disp_label;
  tree handler_var;

  /* The caught error variable of the innermost `catch throws'
     handler body being walked (for `throw throws' rethrows).  */
  tree rethrow_var;

  /* Hoisted call machinery to be inserted before the current
     statement.  */
  tree pre;
};

static tree hb_lower_r (tree *, int *, void *);
static void hb_lower_stmt (tree *, hb_ctx *);

/* The channel payload type for a spec: E for return_failure{E},
   __cxx_std_error otherwise.  */

static tree
hb_error_type_for (tree hspec)
{
  if (hspec && HERBCEPTION_SPEC_KIND (hspec) == hsk_return_failure)
    return HERBCEPTION_SPEC_ERROR_TYPE (hspec);
  return herbceptions_cxx_std_error_node;
}

/* The type the channel stores for a `T' success value: a pointer for
   reference returns (the ABI collapses T&/T&&/T* to the same
   {ptr, i64} slot), T otherwise.  */

static tree
hb_channel_type (tree rettype)
{
  if (TREE_CODE (rettype) == REFERENCE_TYPE)
    return build_pointer_type (TREE_TYPE (rettype));
  return rettype;
}

/* Build union { C value; P error; } where C is the channel type of
   the success value (T, or T* for reference returns).  If T is void
   or returns in memory, the payload is just P.

   "Returns in memory" is aggregate_value_p, not the target's
   return_in_memory hook.  The C++ ABI decision for a result that
   needs a hidden pointer is TREE_ADDRESSABLE on its type (cp_genericize
   sets DECL_BY_REFERENCE from it), which return_in_memory does not
   answer for a non-trivially-copyable class that happens to fit in
   registers.  Asking the hook alone built a channel whose value member
   was such a class, so the union went BLKmode and backends that
   classify only by mode (aarch64) returned it in a BLKmode register --
   impossible to move out of the memory the channel has to live in.
   aggregate_value_p is the predicate the middle end itself asks.  */

static tree
hb_result_type (tree rettype, tree etype, tree fntype)
{
  if (etype == NULL_TREE || etype == error_mark_node)
    etype = herbceptions_cxx_std_error_node;
  if (VOID_TYPE_P (rettype)
      || (fntype && aggregate_value_p (rettype, fntype)))
    return etype;
  if (same_type_p (rettype, etype))
    return etype;
  tree t = lang_hooks.types.make_type (UNION_TYPE);
  tree valuef = build_decl (BUILTINS_LOCATION, FIELD_DECL,
			    get_identifier ("value"),
			    hb_channel_type (rettype));
  tree errorf = build_decl (BUILTINS_LOCATION, FIELD_DECL,
			    get_identifier ("error"), etype);
  DECL_FIELD_CONTEXT (valuef) = t;
  DECL_FIELD_CONTEXT (errorf) = t;
  DECL_CHAIN (valuef) = errorf;
  TYPE_FIELDS (t) = valuef;
  layout_type (t);
  return t;
}

/* The FIELD_DECL of UT holding the error payload (bare P, or the
   `error' member).  */

static tree
hb_error_field (tree ut, tree etype)
{
  if (!RECORD_OR_UNION_TYPE_P (ut) || same_type_p (ut, etype))
    return NULL_TREE;
  for (tree f = TYPE_FIELDS (ut); f; f = DECL_CHAIN (f))
    if (TREE_CODE (f) == FIELD_DECL && same_type_p (TREE_TYPE (f), etype))
      return f;
  return NULL_TREE;
}

/* The FIELD_DECL of UT holding the success value of type T.  */

static tree
hb_value_field (tree ut, tree t)
{
  if (!RECORD_OR_UNION_TYPE_P (ut))
    return NULL_TREE;
  for (tree f = TYPE_FIELDS (ut); f; f = DECL_CHAIN (f))
    if (TREE_CODE (f) == FIELD_DECL && same_type_p (TREE_TYPE (f), t))
      return f;
  return NULL_TREE;
}

/* The error-payload view of RESULT for channel type ETYPE.  */

static tree
hb_error_view (tree result, tree etype)
{
  tree f = hb_error_field (TREE_TYPE (result), etype);
  return f ? build3 (COMPONENT_REF, etype, result, f, NULL_TREE)
	   : result;
}

/* The raw storage slot (lvalue) holding a `T' success value in
   RESULT — the pointer field for reference types, the field itself
   otherwise.  */

static tree
hb_value_slot (tree result, tree t)
{
  tree ct = hb_channel_type (t);
  tree f = hb_value_field (TREE_TYPE (result), ct);
  return f ? build3 (COMPONENT_REF, ct, result, f, NULL_TREE) : result;
}

/* The success-value view of RESULT for value type T — the stored
   pointer dereferenced for reference types.  */

static tree
hb_value_view (tree result, tree t)
{
  tree slot = hb_value_slot (result, t);
  if (TREE_CODE (t) == REFERENCE_TYPE)
    return build1 (INDIRECT_REF, TREE_TYPE (t), slot);
  return slot;
}

/* Create an artificial local of TYPE named NAME.  */

static tree
hb_local (location_t loc, tree type, const char *name)
{
  tree v = build_decl (loc, VAR_DECL, get_identifier (name), type);
  DECL_ARTIFICIAL (v) = 1;
  DECL_CONTEXT (v) = current_function_decl;
  DECL_CHAIN (v) = NULL_TREE;
  return v;
}

/* Build `dst = val' at LOC.  */

static tree
hb_assign (location_t, tree dst, tree val)
{
  return build2 (MODIFY_EXPR, TREE_TYPE (dst), dst, val);
}

/* Append `failed = VAL' to LIST.  */

static void
hb_set_failed (tree list, hb_ctx *ctx, int val, location_t loc)
{
  append_to_statement_list
    (hb_assign (loc, ctx->failed_var,
		val ? boolean_true_node : boolean_false_node),
     &list);
}

/* The failure test `if (__hb_failed)' guarding the route to the error
   handler.  A herbceptions failure is an error path, so tell the branch
   predictor that the handler is rare: clang weights the same edge
   1:1000, and without a hint the optimizers fall back on the generic
   guess for a boolean test, which currently comes out at 34% taken and
   can if-convert the handler into the success path.  This is purely a
   hint -- `__builtin_expect_with_probability' never changes the value
   it is handed, and RTL expansion of the builtin yields its first
   argument unchanged.  */

static tree
hb_failed_test (tree f, location_t loc)
{
  tree fn = builtin_decl_explicit (BUILT_IN_EXPECT_WITH_PROBABILITY);
  if (!fn)
    return f;
  REAL_VALUE_TYPE rate;
  real_from_string (&rate, "0.001");
  return build_call_expr_loc (loc, fn, 3,
			      fold_convert (long_integer_type_node, f),
			      integer_one_node,
			      build_real (double_type_node, rate));
}

/* The static member function NAME of class TYPE as a callable decl,
   or NULL_TREE.  */

static tree
hb_static_member_fn (tree type, const char *name)
{
  tree m = lookup_member (type, get_identifier (name), /*protect=*/0,
			  /*want_type=*/false, tf_none);
  if (!m || m == error_mark_node)
    return NULL_TREE;
  if (TREE_CODE (m) == BASELINK)
    m = BASELINK_FUNCTIONS (m);
  return (TREE_CODE (m) == FUNCTION_DECL || OVL_P (m)) ? m : NULL_TREE;
}

/* Fabricate the error-channel value for a non-std::error operand of
   `throw throws':

     __cxx_std_error{ std::error_domain<T>::domain (),
		      std::error_domain<T>::code (expr) }

   per N2289 — error_domain<T> is the customization point; a
   `domain_alias_type' member (aliasing another error_domain<U>
   sharing the category) supplies the domain() to use.  The result is
   view-converted to TARGET if the channel slot isn't the builtin
   record itself (e.g. a std::error-shaped handler variable).  */

static tree
hb_fabricate_error (tree target, tree expr, location_t loc)
{
  tree type = TREE_TYPE (expr);
  tree fabtype = herbceptions_cxx_std_error_node;
  tree dom = NULL_TREE;
  if (std_node)
    {
      tree d = lookup_qualified_name (std_node, "error_domain");
      if (d && DECL_CLASS_TEMPLATE_P (d))
	{
	  tree args = make_tree_vec (1);
	  TREE_VEC_ELT (args, 0) = type;
	  dom = lookup_template_class (d, args, NULL_TREE, NULL_TREE,
				       tf_none);
	  if (dom && dom != error_mark_node)
	    dom = complete_type (dom);
	}
    }
  if (!dom || dom == error_mark_node || !COMPLETE_TYPE_P (dom)
      || TREE_CODE (dom) != RECORD_TYPE)
    {
      error_at (loc, "%<throw throws%> operand type %qT has no "
		"%<std::error_domain%> specialization", type);
      return error_mark_node;
    }

  /* domain() comes from the specialization, or from the record its
     `domain_alias_type' member names when present.  */
  tree dom_domain = dom;
  tree alias = lookup_member (dom, get_identifier ("domain_alias_type"),
			      0, /*want_type=*/true, tf_none);
  if (alias && TREE_CODE (alias) == TYPE_DECL)
    dom_domain = TREE_TYPE (alias);
  tree domain_fn = hb_static_member_fn (dom_domain, "domain");
  tree code_fn = hb_static_member_fn (dom, "code");
  if (!domain_fn || !code_fn)
    {
      error_at (loc, "%<throw throws%> operand type %qT has no usable "
		"%<std::error_domain%> specialization", type);
      return error_mark_node;
    }
  mark_used (domain_fn);
  mark_used (code_fn);

  tree dcall = build_call_expr_loc (loc, domain_fn, 0);
  tree ccall = build_call_expr_loc (loc, code_fn, 1, expr);
  if (dcall == error_mark_node || ccall == error_mark_node)
    return error_mark_node;

  /* A provably-null domain() is a hard error at the throw site —
     the fabricated std::error would be unrepresentable.  Evaluate a
     constexpr specialization's call.  */
  tree folded = maybe_constant_value (dcall);
  if (folded && (integer_zerop (folded) || null_ptr_cst_p (folded)))
    {
      error_at (loc, "%<std::error_domain%<%T%>::domain()%> evaluates "
		"to a null domain; the %<std::error%> payload cannot be "
		"fabricated", type);
      return error_mark_node;
    }

  /* { .domain = dcall, .code = ccall } in the ABI record's own
     field order.  When TARGET is a distinct record sharing the
     channel's two-word layout -- libherbceptions' std::error class is
     the common case -- build the constructor directly in TARGET
     instead of viewing a __cxx_std_error constructor.  That record is
     TREE_ADDRESSABLE, and gimplifying a VIEW_CONVERT_EXPR over an
     aggregate constructor needs a register temporary that
     create_tmp_var refuses to make for an addressable type.  */
  tree ctor_type = fabtype;
  if (!same_type_p (target, fabtype)
      && TREE_CODE (target) == RECORD_TYPE
      && COMPLETE_TYPE_P (target)
      && TYPE_SIZE (target) && TYPE_SIZE (fabtype)
      && tree_int_cst_equal (TYPE_SIZE (target), TYPE_SIZE (fabtype)))
    {
      unsigned nfields = 0;
      for (tree f = TYPE_FIELDS (target); f; f = DECL_CHAIN (f))
	if (TREE_CODE (f) == FIELD_DECL)
	  nfields++;
      if (nfields == 2)
	ctor_type = target;
    }

  vec<constructor_elt, va_gc> *elts = NULL;
  unsigned idx = 0;
  for (tree f = TYPE_FIELDS (ctor_type); f; f = DECL_CHAIN (f))
    {
      if (TREE_CODE (f) != FIELD_DECL)
	continue;
      tree val = (idx == 0) ? dcall : ccall;
      idx++;
      constructor_elt e = { f, convert (TREE_TYPE (f), val) };
      vec_safe_push (elts, e);
    }
  tree r = build_constructor (ctor_type, elts);
  if (!same_type_p (target, ctor_type))
    r = build1 (VIEW_CONVERT_EXPR, target, r);
  return r;
}

/* Convert EXPR to the channel payload type TARGET where possible.
   std::error-shaped records convert to __cxx_std_error (and back) by
   view, since they share the two-word layout; arbitrary types go
   through the std::error_domain<T> fabrication.  */

static tree
hb_to_error (tree target, tree expr, location_t loc)
{
  tree type = TREE_TYPE (expr);
  if (same_type_p (type, target))
    return expr;
  bool tstd = cxx_std_error_type_p (target)
    || same_type_p (target, herbceptions_cxx_std_error_node);
  bool sstd = cxx_std_error_type_p (type)
    || same_type_p (type, herbceptions_cxx_std_error_node);
  if (tstd && sstd)
    return build1 (VIEW_CONVERT_EXPR, target, expr);
  return hb_fabricate_error (target, expr, loc);
}

/* Emit `{ init?; __hb_failed = 0; return; }' — a plain success
   return.  INIT is the value write (or NULL).  */

static void
hb_emit_return (tree list, hb_ctx *ctx, tree init, location_t loc)
{
  if (init)
    append_to_statement_list (init, &list);
  hb_set_failed (list, ctx, 0, loc);
  tree r = build_stmt (loc, RETURN_EXPR, NULL_TREE);
  TREE_NO_WARNING (r) = 1;
  append_to_statement_list (r, &list);
}

/* Emit `{ RETVAL; __hb_failed = 0; return <retval>; }' for a success
   return whose value materializes through DECL_RESULT (an sret
   return).  RETVAL is normally the INIT_EXPR/MODIFY_EXPR writing
   DECL_RESULT.

   The write-through has to stay a separate statement and the return
   operand has to be DECL_RESULT itself.  gimplify_return_expr keeps the
   result as the return operand only when the operand is the bare
   RESULT_DECL (or absent); handed an INIT_EXPR it emits the assignment
   followed by a plain `return;'.  A value-less return leaves the sret
   buffer looking like an ordinary local that nothing reads, so GIMPLE
   DSE and CD-DCE drop the write-through and the caller gets whatever
   the slot happened to contain.  */

static void
hb_emit_return_result (tree list, hb_ctx *ctx, tree retval, location_t loc)
{
  if (retval)
    append_to_statement_list (retval, &list);
  hb_set_failed (list, ctx, 0, loc);
  tree r = build_stmt (loc, RETURN_EXPR, DECL_RESULT (ctx->fndecl));
  TREE_NO_WARNING (r) = 1;
  append_to_statement_list (r, &list);
}

/* Emit the failure exit/route for payload E at LOC into LIST:
   - inside a `catch throws' scope: bind the handler var, goto disp;
   - in a spec'd fn: `__hb_result.error = e; __hb_failed = 1; return;'
   - in main(): trap — an escaped error hits the main boundary;
   - otherwise an unhandled-failure error.  CALL_P marks the route as
     coming from a call failure; `throw throws' escaping a plain
     function is a hard error even in main.  DIAGNOSED suppresses the
     unhandled-failure diagnostic when the parser already rejected
     the call site.  */

static void
hb_emit_route (tree list, hb_ctx *ctx, tree e, location_t loc,
	       bool call_p = true, bool diagnosed = false)
{
  if (ctx->disp_label)
    {
      /* A handler variable of the wrong type was already diagnosed
	 by finish_herbceptions_handler_parms — do not cascade an
	 error_domain fabrication failure on top of it.  */
      if (ctx->handler_var
	  && cxx_std_error_type_p (TREE_TYPE (ctx->handler_var)))
	{
	  tree v = hb_to_error (TREE_TYPE (ctx->handler_var), e, loc);
	  if (v == error_mark_node)
	    return;
	  append_to_statement_list
	    (hb_assign (loc, ctx->handler_var, v), &list);
	}
      append_to_statement_list
	(build1 (GOTO_EXPR, void_type_node, ctx->disp_label), &list);
      return;
    }
  if (!ctx->hspec)
    {
      if (call_p && DECL_MAIN_P (ctx->fndecl))
	{
	  append_to_statement_list
	    (build_call_expr_loc (loc,
				  builtin_decl_explicit (BUILT_IN_TRAP),
				  0),
	     &list);
	  return;
	}
      if (!diagnosed)
	error_at (loc,
		  "an unhandled herbceptions failure cannot leave a "
		  "function without a herbceptions specification");
      return;
    }
  e = hb_to_error (ctx->error_type, e, loc);
  if (e == error_mark_node)
    return;
  append_to_statement_list
    (hb_assign (loc, hb_error_view (ctx->result_var, ctx->error_type), e),
     &list);
  hb_set_failed (list, ctx, 1, loc);
  tree r = build_stmt (loc, RETURN_EXPR, NULL_TREE);
  TREE_NO_WARNING (r) = 1;
  append_to_statement_list (r, &list);
}

/* The FIELD_DECL of aggregate R named NAME, or NULL_TREE.  */

static tree
hb_named_field (tree r, const char *name)
{
  tree id = get_identifier (name);
  for (tree f = TYPE_FIELDS (r); f; f = DECL_CHAIN (f))
    if (TREE_CODE (f) == FIELD_DECL && DECL_NAME (f) == id)
      return f;
  return NULL_TREE;
}

/* Lower HERBCEPTION_CATCH_EXPR: `catch return_failure (call)' — the
   call runs through the channel and yields the {value,error,failed}
   either-aggregate.  Requires a `return_failure{E}' callee; a plain
   `throws' call is rejected (its std::error payload needs a
   `catch throws' block handler).  */

static tree
hb_lower_catch (hb_ctx *ctx, tree stmt, location_t loc)
{
  tree call = TREE_OPERAND (stmt, 0);
  while (TREE_CODE (call) == HERBCEPTION_TRY_EXPR)
    call = TREE_OPERAND (call, 0);
  if (TREE_CODE (call) == TARGET_EXPR)
    call = TARGET_EXPR_INITIAL (call);

  tree fntype = NULL_TREE;
  if (TREE_CODE (call) == CALL_EXPR || TREE_CODE (call) == AGGR_INIT_EXPR)
    {
      tree fn = (TREE_CODE (call) == AGGR_INIT_EXPR
		 ? AGGR_INIT_EXPR_FN (call) : CALL_EXPR_FN (call));
      if (fn)
	{
	  fntype = TREE_TYPE (fn);
	  if (POINTER_TYPE_P (fntype))
	    fntype = TREE_TYPE (fntype);
	}
    }
  if (!fntype || !FUNC_OR_METHOD_TYPE_P (fntype)
      || !type_herbceptions_p (fntype))
    {
      error_at (loc, "%<catch return_failure%> requires a call to a "
		"function with a herbceptions specification");
      return error_mark_node;
    }

  /* Nested herbceptions nodes inside the call's arguments lower into
     ctx->pre and must precede the call itself.  */
  if (TREE_CODE (call) == CALL_EXPR)
    for (int i = 0; i < call_expr_nargs (call); ++i)
      cp_walk_tree (&CALL_EXPR_ARG (call, i), hb_lower_r, ctx, NULL);
  else if (TREE_CODE (call) == AGGR_INIT_EXPR)
    for (int i = 0; i < aggr_init_expr_nargs (call); ++i)
      cp_walk_tree (&AGGR_INIT_EXPR_ARG (call, i), hb_lower_r, ctx,
		    NULL);

  tree spec_list = TYPE_RAISES_EXCEPTIONS (fntype);
  tree hspec = HERBCEPTION_SPEC_P (spec_list)
    ? TREE_PURPOSE (spec_list) : NULL_TREE;
  if (!hspec
      || HERBCEPTION_SPEC_KIND (hspec) != hsk_return_failure)
    {
      error_at (loc, "%<catch return_failure%> cannot bind the "
		"implicit %<std::error%> channel of a %<throws%> "
		"function; use a %<try { } catch throws (std::error)%> "
		"block handler");
      return error_mark_node;
    }

  tree err = HERBCEPTION_SPEC_ERROR_TYPE (hspec);
  /* A call's TREE_TYPE strips the reference off a reference
     return — the declared return type carries it.  */
  tree ctype = TREE_TYPE (fntype);
  if (VOID_TYPE_P (ctype))
    ctype = err;

  tree union_type;
  if (TREE_CODE (call) == AGGR_INIT_EXPR
      || TREE_CODE (call) == TARGET_EXPR
      || aggregate_value_p (ctype, fntype))
    union_type = err;
  else
    union_type = hb_result_type (ctype, err, fntype);

  tree u = build_decl (loc, VAR_DECL, NULL_TREE, union_type);
  DECL_ARTIFICIAL (u) = 1;
  DECL_CONTEXT (u) = ctx->fndecl;
  tree f = build_decl (loc, VAR_DECL, NULL_TREE, boolean_type_node);
  DECL_ARTIFICIAL (f) = 1;
  DECL_CONTEXT (f) = ctx->fndecl;
  tree etype = TREE_TYPE (stmt);
  tree r = build_decl (loc, VAR_DECL, NULL_TREE, etype);
  DECL_ARTIFICIAL (r) = 1;
  DECL_CONTEXT (r) = ctx->fndecl;

  if (!ctx->pre)
    ctx->pre = alloc_stmt_list ();
  append_to_statement_list (build_stmt (loc, DECL_EXPR, u), &ctx->pre);
  append_to_statement_list (build_stmt (loc, DECL_EXPR, f), &ctx->pre);
  append_to_statement_list (build_stmt (loc, DECL_EXPR, r), &ctx->pre);

  /* The call and member binds stay inline at the catch-expr's
     position so operands produced by earlier elements of an
     enclosing COMPOUND stay visible.  A materialized (aggregate/sret)
     call goes into a channel-local — the peeled TARGET_EXPR's slot
     only binds inside that node.  */
  tree seq;
  tree valslot;
  if (TREE_CODE (call) == AGGR_INIT_EXPR
      || aggregate_value_p (ctype, fntype))
    {
      tree v = build_decl (loc, VAR_DECL, NULL_TREE, ctype);
      DECL_ARTIFICIAL (v) = 1;
      DECL_CONTEXT (v) = ctx->fndecl;
      append_to_statement_list (build_stmt (loc, DECL_EXPR, v),
				&ctx->pre);
      if (TREE_CODE (call) == AGGR_INIT_EXPR)
	{
	  AGGR_INIT_EXPR_SLOT (call) = v;
	  seq = call;
	}
      else
	seq = build2 (INIT_EXPR, void_type_node, v, call);
      valslot = v;
    }
  else
    {
      seq = call;
      valslot = hb_value_view (u, ctype);
    }
  seq = build2 (COMPOUND_EXPR, void_type_node, seq,
		hb_assign (loc, u,
			   build_call_expr_internal_loc
			   (loc, IFN_HERBCEPTION_RESULT,
			    TREE_TYPE (u), 0)));
  seq = build2 (COMPOUND_EXPR, void_type_node, seq,
		hb_assign (loc, f,
			   build_call_expr_internal_loc
			   (loc, IFN_HERBCEPTION_FAILED,
			    TREE_TYPE (f), 0)));

  /* Bind the either members: r.value = u.value, r.error = u.error,
     r.failed = f.  */
  tree errf = hb_error_field (union_type, err);
  tree errslot = errf
    ? build3 (COMPONENT_REF, err, u, errf, NULL_TREE) : u;
  tree rv = hb_named_field (etype, "value");
  tree re = hb_named_field (etype, "error");
  tree rf = hb_named_field (etype, "failed");
  seq = build2 (COMPOUND_EXPR, void_type_node, seq,
		hb_assign (loc,
			   build3 (COMPONENT_REF, ctype, r, rv, NULL_TREE),
			   valslot));
  seq = build2 (COMPOUND_EXPR, void_type_node, seq,
		hb_assign (loc,
			   build3 (COMPONENT_REF, err, r, re, NULL_TREE),
			   errslot));
  seq = build2 (COMPOUND_EXPR, void_type_node, seq,
		hb_assign (loc,
			   build3 (COMPONENT_REF, boolean_type_node, r,
				   rf, NULL_TREE),
			   f));
  return build2 (COMPOUND_EXPR, etype, seq, r);
}

/* Lower HERBCEPTION_THROW_EXPR / HERBCEPTION_FAIL_EXPR.  Returns a
   STATEMENT_LIST implementing the failure (valid only where a
   statement sequence can stand).  */

static tree
hb_lower_fail (hb_ctx *ctx, tree stmt, bool is_throw, location_t loc)
{
  tree expr = TREE_OPERAND (stmt, 0);
  tree list = alloc_stmt_list ();
  tree e;
  if (expr)
    {
      if (!is_throw && ctx->hspec
	  && HERBCEPTION_SPEC_KIND (ctx->hspec) == hsk_return_failure)
	e = convert (ctx->error_type, expr);
      else
	e = hb_to_error (ctx->error_type ? ctx->error_type
			 : herbceptions_cxx_std_error_node, expr, loc);
      if (e == error_mark_node)
	return build_stmt (loc, EXPR_STMT, error_mark_node);
    }
  else if (is_throw)
    {
      /* Bare `throw throws': rethrow the caught error through the
	 enclosing context.  */
      e = ctx->rethrow_var;
      if (!e)
	{
	  error_at (loc, "no caught error to rethrow");
	  return build_stmt (loc, EXPR_STMT, error_mark_node);
	}
    }
  else
    {
      /* Bare `return_failure;': a value-initialized E.  */
      e = build_value_init (ctx->error_type
			    ? ctx->error_type
			    : herbceptions_cxx_std_error_node,
			    tf_warning_or_error);
      if (!e || e == error_mark_node)
	return build_stmt (loc, EXPR_STMT, error_mark_node);
    }
  hb_emit_route (list, ctx, e, loc, /*call_p=*/false);
  return list;
}

/* Lower a spec'd call nested as a TARGET_EXPR's initializer —
   try_expr<call> inside target_expr<slot, ...>, the shape the parser
   produces for a discarded materialization (`make_s ();').  The
   TARGET_EXPR owns the slot's binding, so the call, channel reads,
   and the failure route must all stay inside its initializer:
   target<slot, { call; u = RESULT; f = FAILED; if (f) route }>.  */

static void
hb_lower_te_init (hb_ctx *ctx, tree te, tree call, tree fntype,
		  tree diagnosed, location_t loc)
{
  tree spec_list = TYPE_RAISES_EXCEPTIONS (fntype);
  tree hspec = HERBCEPTION_SPEC_P (spec_list)
    ? TREE_PURPOSE (spec_list) : NULL_TREE;
  tree err = hb_error_type_for (hspec);

  tree ctype = TREE_TYPE (fntype);
  /* A register-channel call does not write the TARGET_EXPR slot the way
     an aggregate/sret callee does; its result travels through the
     channel union, so it must be read out and stored into the slot.
     An aggregate/sret callee already materialises into the slot.  */
  bool via_channel = !(VOID_TYPE_P (ctype)
		       || TREE_CODE (call) == AGGR_INIT_EXPR
		       || TREE_CODE (call) == TARGET_EXPR
		       || aggregate_value_p (ctype, fntype));
  tree union_type = via_channel ? hb_result_type (ctype, err, fntype) : err;

  tree u = build_decl (loc, VAR_DECL, NULL_TREE, union_type);
  DECL_ARTIFICIAL (u) = 1;
  DECL_CONTEXT (u) = ctx->fndecl;
  tree f = build_decl (loc, VAR_DECL, NULL_TREE, boolean_type_node);
  DECL_ARTIFICIAL (f) = 1;
  DECL_CONTEXT (f) = ctx->fndecl;

  if (!ctx->pre)
    ctx->pre = alloc_stmt_list ();
  append_to_statement_list (build_stmt (loc, DECL_EXPR, u), &ctx->pre);
  append_to_statement_list (build_stmt (loc, DECL_EXPR, f), &ctx->pre);

  /* Nested herbceptions nodes inside the call's arguments lower into
     ctx->pre and must precede the call itself.  */
  if (TREE_CODE (call) == CALL_EXPR)
    for (int i = 0; i < call_expr_nargs (call); ++i)
      cp_walk_tree (&CALL_EXPR_ARG (call, i), hb_lower_r, ctx, NULL);
  else
    for (int i = 0; i < aggr_init_expr_nargs (call); ++i)
      cp_walk_tree (&AGGR_INIT_EXPR_ARG (call, i), hb_lower_r, ctx,
		    NULL);

  /* An aggregate/sret callee materialises into the slot the TARGET_EXPR
     owns.  The front end expresses that either with an AGGR_INIT_EXPR
     that already carries the slot, or -- for a deduced-`auto' return
     forwarding another call, a lambda's operator(), fast_io's
     posix_api_common chain -- with a plain CALL_EXPR that relies on
     gimplify_target_expr passing the slot as the hidden return buffer.
     The second form only works while the call is the TARGET_EXPR's
     initializer: this function replaces it with a void COMPOUND_EXPR,
     after which gimplify_target_expr takes its "the mere evaluation
     initialises the slot" path and the callee is handed no return
     buffer at all, leaving the caller reading an unwritten one.  Make
     the write explicit -- an assignment whose RHS is the sret call
     turns back into a return-slot call.  */
  tree callstmt = call;
  if (!via_channel && !VOID_TYPE_P (ctype)
      && aggregate_value_p (ctype, fntype)
      && TREE_CODE (call) == CALL_EXPR)
    callstmt = build2 (INIT_EXPR, void_type_node, TARGET_EXPR_SLOT (te), call);

  tree seq
    = build2 (COMPOUND_EXPR, void_type_node, callstmt,
	      hb_assign (loc, u,
			 build_call_expr_internal_loc
			 (loc, IFN_HERBCEPTION_RESULT, union_type, 0)));
  seq = build2 (COMPOUND_EXPR, void_type_node, seq,
		hb_assign (loc, f,
			   build_call_expr_internal_loc
			   (loc, IFN_HERBCEPTION_FAILED,
			    boolean_type_node, 0)));
  tree body = alloc_stmt_list ();
  tree errf = hb_error_field (union_type, err);
  tree errslot = errf
    ? build3 (COMPONENT_REF, err, u, errf, NULL_TREE) : u;
  hb_emit_route (body, ctx, errslot, loc, /*call_p=*/true,
		 diagnosed ? TREE_NO_WARNING (diagnosed) : false);
  seq = build2 (COMPOUND_EXPR, void_type_node, seq,
		build3 (COND_EXPR, void_type_node, hb_failed_test (f, loc),
			body ? body : void_node, NULL_TREE));
  if (via_channel)
    seq = build2 (COMPOUND_EXPR, void_type_node, seq,
		  hb_assign (loc, TARGET_EXPR_SLOT (te),
			     hb_value_view (u, ctype)));
  TARGET_EXPR_INITIAL (te) = seq;
}

/* Lower HERBCEPTION_TRY_EXPR: `try (call)' and the implicit wrapper
   on spec'd calls.  The call and channel reads hoist into ctx->pre;
   returns the value expression to stand in place of the node.  */

static tree
hb_lower_try (hb_ctx *ctx, tree stmt, location_t loc, bool discarded)
{
  /* stmt is normally a HERBCEPTION_TRY_EXPR wrapping the call, but
     raw CALL_EXPR/AGGR_INIT_EXPR nodes arrive here directly from
     hb_lower_r when a spec'd call bypassed the parser's auto-wrap
     (ctor-init, desugared calls, coroutine machinery).  */
  tree call = (TREE_CODE (stmt) == CALL_EXPR
	       || TREE_CODE (stmt) == AGGR_INIT_EXPR)
	      ? stmt : TREE_OPERAND (stmt, 0);
  tree ctype = TREE_TYPE (call);
  tree rettype = ctype;
  tree fntype = NULL_TREE;

  /* `try(call)' wraps an already auto-wrapped call — peel the
     inner try, the channel machinery is identical.  */
  while (TREE_CODE (call) == HERBCEPTION_TRY_EXPR)
    call = TREE_OPERAND (call, 0);

  /* For an aggregate or sret call the parser wraps the
     materialization, not the call itself:
     try_expr<target_expr<slot, call>> — peel the TARGET_EXPR to reach
     the call and keep the slot for the result.  */
  tree target = NULL_TREE;
  if (TREE_CODE (call) == TARGET_EXPR)
    {
      target = call;
      call = TARGET_EXPR_INITIAL (call);
    }

  /* For a reference-returning call the parser wraps the dereference:
     try_expr<indirect_ref<call>> — the call yields the referent
     pointer, the INDIRECT_REF is its reference-ness.  */
  tree indir = NULL_TREE;
  if (TREE_CODE (call) == INDIRECT_REF)
    {
      indir = call;
      call = TREE_OPERAND (call, 0);
    }

  if (TREE_CODE (call) == CALL_EXPR || TREE_CODE (call) == AGGR_INIT_EXPR)
    {
      tree fn = (TREE_CODE (call) == AGGR_INIT_EXPR
		 ? AGGR_INIT_EXPR_FN (call) : CALL_EXPR_FN (call));
      if (fn)
	{
	  fntype = TREE_TYPE (fn);
	  if (POINTER_TYPE_P (fntype))
	    fntype = TREE_TYPE (fntype);
	}
    }

  /* Non-call operands carry no channel.  */
  if (!fntype || !FUNC_OR_METHOD_TYPE_P (fntype)
      || !type_herbceptions_p (fntype))
    return call;
  /* The declared return type carries the reference that the call's
     TREE_TYPE strips.  */
  rettype = TREE_TYPE (fntype);

  /* Nested herbceptions nodes inside the call's arguments (a wrapped
     default argument, `f(g())') lower into ctx->pre and must precede
     the call itself.  */
  if (TREE_CODE (call) == CALL_EXPR)
    for (int i = 0; i < call_expr_nargs (call); ++i)
      cp_walk_tree (&CALL_EXPR_ARG (call, i), hb_lower_r, ctx, NULL);
  else
    for (int i = 0; i < aggr_init_expr_nargs (call); ++i)
      cp_walk_tree (&AGGR_INIT_EXPR_ARG (call, i), hb_lower_r, ctx,
		    NULL);

  tree spec_list = TYPE_RAISES_EXCEPTIONS (fntype);
  tree hspec = HERBCEPTION_SPEC_P (spec_list)
    ? TREE_PURPOSE (spec_list) : NULL_TREE;
  tree err = hb_error_type_for (hspec);

  tree union_type;
  if (VOID_TYPE_P (rettype) || TREE_CODE (call) == AGGR_INIT_EXPR
      || TREE_CODE (call) == TARGET_EXPR
      || aggregate_value_p (rettype, fntype))
    union_type = err;
  else
    union_type = hb_result_type (rettype, err, fntype);

  tree u = build_decl (loc, VAR_DECL, NULL_TREE, union_type);
  DECL_ARTIFICIAL (u) = 1;
  DECL_CONTEXT (u) = ctx->fndecl;
  tree f = build_decl (loc, VAR_DECL, NULL_TREE, boolean_type_node);
  DECL_ARTIFICIAL (f) = 1;
  DECL_CONTEXT (f) = ctx->fndecl;

  if (!ctx->pre)
    ctx->pre = alloc_stmt_list ();
  append_to_statement_list (build_stmt (loc, DECL_EXPR, u), &ctx->pre);
  append_to_statement_list (build_stmt (loc, DECL_EXPR, f), &ctx->pre);

  bool mem_ret = !VOID_TYPE_P (ctype) && !indir
		 && TREE_CODE (ctype) != REFERENCE_TYPE
		 && aggregate_value_p (rettype, fntype);
  if (target || TREE_CODE (call) == AGGR_INIT_EXPR || mem_ret)
    {
      /* A materialized (aggregate/sret) call: the callee writes into
	 the TARGET_EXPR's slot, or — for a raw AGGR_INIT_EXPR with no
	 enclosing TARGET_EXPR, as the frontend produces for a
	 declaration's initializer — the frontend's own result temp.
	 That slot only binds inside the node that owns it (a
	 TARGET_EXPR) or is never declared at all, so reading it past
	 the call escapes the binding (an ICE on expression-statement
	 and RESULT-init forms), while folding the channel checks into
	 the initializer makes the gimplifier elide the copy to the
	 consumer.  Materialize into a channel-local instead, declared
	 alongside u/f so the value is an ordinary function-scope object
	 in every context — the whole sequence stays inline at the
	 try-expr's position so operands produced by earlier elements of
	 an enclosing COMPOUND (the `new' comma-expansion's allocation
	 temp) are visible:
	 { v = call; u = RESULT; f = FAILED; if (f) route; v }.

	 A plain CALL_EXPR counts here too.  A desugared call the parser
	 never auto-wrapped -- a range-for's `__for_begin = begin
	 (__range)', which build_range_exp emits as a bare MODIFY_EXPR --
	 reaches this point with the declared return type in memory and
	 no materialization slot.  Evaluated as a bare expression the
	 callee is handed no return buffer at all: gimplify allocates a
	 throwaway one and __for_begin keeps whatever it had, so a
	 directory listing iterated `for (auto const &e : current(at))'
	 saw an unwritten first entry.  The front end records the hidden
	 return-slot pointer as CALL_EXPR_ARG (call, 0) only when it
	 applied the return-slot optimization itself, so that argument
	 cannot be assumed to be the slot -- for begin(__range) it is the
	 generator reference.  */
      /* The materialized object's type: TREE_TYPE of a raw
	 AGGR_INIT_EXPR is void (the node's value is the slot), so
	 take it from the slot — or the declared return type.  */
      tree vtype = ctype;
      if (TREE_CODE (call) == AGGR_INIT_EXPR && AGGR_INIT_EXPR_SLOT (call))
	vtype = TREE_TYPE (AGGR_INIT_EXPR_SLOT (call));
      if (VOID_TYPE_P (vtype))
	vtype = rettype;
      tree v = build_decl (loc, VAR_DECL, NULL_TREE, vtype);
      DECL_ARTIFICIAL (v) = 1;
      DECL_CONTEXT (v) = ctx->fndecl;
      append_to_statement_list (build_stmt (loc, DECL_EXPR, v),
				&ctx->pre);
      tree mat;
      if (TREE_CODE (call) == AGGR_INIT_EXPR)
	{
	  AGGR_INIT_EXPR_SLOT (call) = v;
	  mat = call;
	}
      else
	mat = build2 (INIT_EXPR, void_type_node, v, call);
      tree seq
	= build2 (COMPOUND_EXPR, void_type_node, mat,
		  hb_assign (loc, u,
			     build_call_expr_internal_loc
			     (loc, IFN_HERBCEPTION_RESULT,
			      TREE_TYPE (u), 0)));
      seq = build2 (COMPOUND_EXPR, void_type_node, seq,
		    hb_assign (loc, f,
			       build_call_expr_internal_loc
			       (loc, IFN_HERBCEPTION_FAILED,
				TREE_TYPE (f), 0)));
      {
	tree body = alloc_stmt_list ();
	tree errf = hb_error_field (union_type, err);
	tree errslot = errf
	  ? build3 (COMPONENT_REF, err, u, errf, NULL_TREE) : u;
	hb_emit_route (body, ctx, errslot, loc,
		       /*call_p=*/true, TREE_NO_WARNING (stmt));
	seq = build2 (COMPOUND_EXPR, void_type_node, seq,
		      build3 (COND_EXPR, void_type_node, hb_failed_test (f, loc),
			      body ? body : void_node, NULL_TREE));
      }
      if (discarded)
	return seq;
      return build2 (COMPOUND_EXPR, vtype, seq, v);
    }

  /* The same inline sequence for a register-channel call:
     { call; u = RESULT; f = FAILED; if (f) route; <value> } — only
     the u/f declarations hoist to ctx->pre.  */
  tree seq
    = build2 (COMPOUND_EXPR, void_type_node, call,
	      hb_assign (loc, u,
			 build_call_expr_internal_loc
			 (loc, IFN_HERBCEPTION_RESULT,
			  TREE_TYPE (u), 0)));
  seq = build2 (COMPOUND_EXPR, void_type_node, seq,
		hb_assign (loc, f,
			   build_call_expr_internal_loc
			   (loc, IFN_HERBCEPTION_FAILED,
			    TREE_TYPE (f), 0)));
  {
    tree body = alloc_stmt_list ();
    tree errf = hb_error_field (union_type, err);
    tree errslot = errf
      ? build3 (COMPONENT_REF, err, u, errf, NULL_TREE) : u;
    hb_emit_route (body, ctx, errslot, loc,
		   /*call_p=*/true, TREE_NO_WARNING (stmt));
    seq = build2 (COMPOUND_EXPR, void_type_node, seq,
		  build3 (COND_EXPR, void_type_node, hb_failed_test (f, loc),
			  body ? body : void_node, NULL_TREE));
  }

  /* The try-expr's value: u.value for scalar calls, the
     materialized channel-local for aggregate/sret calls (handled
     above), void otherwise.  */
  if (VOID_TYPE_P (ctype) || discarded)
    return seq;
  if (indir)
    {
      /* The dereference now applies to the channel's pointer slot —
	 *u.value is the referent lvalue the try-expr yields.  */
      TREE_OPERAND (indir, 0) = hb_value_slot (u, rettype);
      return build2 (COMPOUND_EXPR, ctype, seq, indir);
    }
  if (aggregate_value_p (rettype, fntype)
      && TREE_CODE (call) == CALL_EXPR)
    /* An sret callee wrote the value into the hidden return-slot
       argument.  */
    return build2 (COMPOUND_EXPR, ctype, seq,
		   build1 (INDIRECT_REF, ctype,
			   CALL_EXPR_ARG (call, 0)));
  if (TREE_CODE (ctype) == REFERENCE_TYPE)
    /* A reference-returning call: the try-expr's type is the
       reference, and the channel slot holds the referent's address.
       Hand the binding that pointer.  gimplify accepts a pointer
       where it expects a same-sized reference, whereas the
       INDIRECT_REF hb_value_view builds is typed as the referent, so
       `T &r = ++it' (a spec'd operator++ used as a value, not
       dereferenced) reached fold_convert_loc with a record argument
       and aborted.  */
    return build2 (COMPOUND_EXPR, ctype, seq,
		   hb_value_slot (u, rettype));
  return build2 (COMPOUND_EXPR, ctype, seq, hb_value_view (u, rettype));
}

/* ----------------------------------------------------------------
 * Legacy-EH boundary conversion (hosted targets only): a `throws'
 * function or a `catch throws' handler catches a legacy C++
 * exception escaping its scope and converts it through the
 * itanium-exception-ptr error domain — the {domain, code} pair the
 * runtime's __cxa_error_domain_itanium_exception_ptr /
 * __cxa_error_code_itanium_exception_ptr entry points fabricate —
 * exactly like clang's herbceptions lowering.  A `return_failure{E}'
 * function cannot fabricate an arbitrary E, so an escaping legacy
 * exception terminates (matching clang's __clang_call_terminate).
 * ---------------------------------------------------------------- */

static tree hb_begin_catch_fn, hb_end_catch_fn;
static tree hb_exc_dom_fn, hb_exc_code_fn;

static void
hb_eh_fndecls (void)
{
  if (hb_begin_catch_fn)
    return;
  hb_begin_catch_fn
    = push_library_fn (get_identifier ("__cxa_begin_catch"),
		       build_function_type_list (ptr_type_node,
						 ptr_type_node, NULL_TREE),
		       empty_except_spec, ECF_NOTHROW);
  /* __cxa_end_catch deliberately carries no exception spec: the
     libsupc++ declaration is unannotated, and declaring it noexcept
     here would warn on the mismatch.  */
  hb_end_catch_fn
    = push_library_fn (get_identifier ("__cxa_end_catch"),
		       build_function_type_list (void_type_node,
						 NULL_TREE),
		       NULL_TREE, 0);
  hb_exc_dom_fn
    = push_library_fn
      (get_identifier ("__cxa_error_domain_itanium_exception_ptr"),
       build_function_type_list (ptr_type_node, NULL_TREE),
       empty_except_spec, ECF_NOTHROW);
  hb_exc_code_fn
    = push_library_fn
      (get_identifier ("__cxa_error_code_itanium_exception_ptr"),
       build_function_type_list (uintptr_type_node,
				 uintptr_type_node, ptr_type_node,
				 ptr_type_node, ptr_type_node,
				 NULL_TREE),
       empty_except_spec, ECF_NOTHROW);
}

/* __cxa_begin_catch (__builtin_eh_pointer (0)) — the caught
   exception's object pointer.  */

static tree
hb_begin_catch_expr (location_t loc)
{
  hb_eh_fndecls ();
  return build_call_expr_loc (loc, hb_begin_catch_fn, 1,
			      build_exc_ptr ());
}

/* __builtin_eh_pointer (0) -- the _Unwind_Exception * the personality
   handed the landing pad -- passed straight to the fabrication entry
   point.  That one wants the unwind header, not the object
   __cxa_begin_catch would return: it classifies the header and adopts
   the in-flight reference.  It also retires the uncaught-exception
   count __cxa_throw added itself, so no __cxa_begin_catch may be
   entered here and no __cxa_end_catch may release the reference the
   minted code now owns.  clang passes the pointer through and calls
   neither; feeding it __cxa_begin_catch's object made the header
   compare as a foreign exception and abort.  */

static tree
hb_legacy_exc_ptr (void)
{
  hb_eh_fndecls ();
  return build_exc_ptr ();
}

/* Emit the std::error fabrication for the exception caught by
   __cxa_begin_catch: a channel-local pair bound to
   { __cxa_error_domain_itanium_exception_ptr (),
     __cxa_error_code_itanium_exception_ptr (1, obj, 0, 0) }.
   The pair is an explicit local — assigning an aggregate
   constructor of the ADDRESSABLE channel record would need a
   temporary create_tmp_var refuses to make for it.  Returns the
   pair decl after appending the sequence to LIST.  */

static tree
hb_emit_fab_pair (tree *list, location_t loc)
{
  hb_eh_fndecls ();
  tree fabtype = herbceptions_cxx_std_error_node;
  tree pairv = build_decl (loc, VAR_DECL, NULL_TREE, fabtype);
  DECL_ARTIFICIAL (pairv) = 1;
  DECL_CONTEXT (pairv) = current_function_decl;
  append_to_statement_list (build_stmt (loc, DECL_EXPR, pairv), list);
  tree df = hb_named_field (fabtype, "domain");
  tree cf = hb_named_field (fabtype, "code");
  append_to_statement_list
    (hb_assign (loc,
		build3 (COMPONENT_REF, const_ptr_type_node, pairv,
			df, NULL_TREE),
		build_call_expr_loc (loc, hb_exc_dom_fn, 0)),
     list);
  append_to_statement_list
    (hb_assign (loc,
		build3 (COMPONENT_REF, uintptr_type_node, pairv,
			cf, NULL_TREE),
		build_call_expr_loc (loc, hb_exc_code_fn, 4,
				     integer_one_node,
				     hb_legacy_exc_ptr (),
				     null_pointer_node,
				     null_pointer_node)),
     list);
  return pairv;
}

/* Build a catch-all CATCH_EXPR running STMTS with the implicit
   and, when ENTERED_CATCH, the __cxa_end_catch cleanup it requires.  For a
   `catch throws' conversion STMTS
   fabricate the handler variable and jump to the dispatch label; for
   a `throws' function boundary they write the channel and return.
   Emit GENERIC CATCH_EXPR directly — the earlier HANDLER form let
   build_stmt leave void_type_node in HANDLER_TYPE, producing a
   `catch (void)' filter (_ZTIv) that matches nothing.  */

static tree
hb_legacy_catch_all (location_t loc, tree stmts, bool entered_catch)
{
  /* Only a path that actually entered the catch may end it: the
     fabrication paths let the minted code adopt the exception's
     reference instead, so an __cxa_end_catch there would free it from
     under the code.  */
  tree body = stmts;
  if (entered_catch)
    {
      hb_eh_fndecls ();
      tree endc
	= build_stmt (loc, EXPR_STMT,
		      build_call_expr_loc (loc, hb_end_catch_fn, 0));
      body = build_stmt (loc, TRY_FINALLY_EXPR, stmts, endc);
    }
  return build2 (CATCH_EXPR, void_type_node, NULL_TREE, body);
}

/* The synthesized boundary for a `throws' function: an escaping
   legacy exception is caught by a catch-all that fabricates the
   std::error pair and returns it on the channel.  For a
   `return_failure{E}' function the channel cannot represent an
   arbitrary legacy exception — catch it and terminate.  */

static tree
hb_fn_boundary (hb_ctx *ctx, tree body, location_t loc, bool fabricate)
{
  tree action = alloc_stmt_list ();
  if (fabricate)
    {
      tree pairv = hb_emit_fab_pair (&action, loc);
      tree errf = hb_error_field (ctx->union_type, ctx->error_type);
      tree errslot = errf
	? build3 (COMPONENT_REF, ctx->error_type, ctx->result_var,
		  errf, NULL_TREE) : ctx->result_var;
      append_to_statement_list (hb_assign (loc, errslot, pairv),
				&action);
      append_to_statement_list (hb_assign (loc, ctx->failed_var,
					   boolean_true_node), &action);
      append_to_statement_list
	(build_stmt (loc, RETURN_EXPR, NULL_TREE), &action);
    }
  else
    {
      append_to_statement_list
	(build_stmt (loc, EXPR_STMT, hb_begin_catch_expr (loc)), &action);
      append_to_statement_list
	(build_stmt (loc, EXPR_STMT,
		     build_call_expr_loc
		     (loc,
		      builtin_decl_explicit (BUILT_IN_ABORT), 0)),
	 &action);
    }
  tree h = hb_legacy_catch_all (loc, action, !fabricate);
  tree tc = build_stmt (loc, TRY_CATCH_EXPR, body, h);
  tree list = alloc_stmt_list ();
  append_to_statement_list (tc, &list);
  return list;
}

/* Lower a TRY_BLOCK whose handlers may include HERBCEPTION_HANDLERs.
   Builds

     {
       e (the caught var decl);
       <lowered stmts, or TRY_CATCH_EXPR wrapping them for legacy
        catches>
       goto end;
     disp: <handler bodies>
     end:
     }                                                    */

static tree
hb_lower_try_block (hb_ctx *ctx, tree try_block)
{
  tree stmts = TRY_STMTS (try_block);
  tree handlers = TRY_HANDLERS (try_block);
  location_t loc = EXPR_LOCATION (try_block);

  /* Partition `catch throws' from legacy handlers.  TRY_HANDLERS is
     a STATEMENT_LIST of handler nodes, or a single handler.  */
  tree hb_handler = NULL_TREE;
  tree legacy = NULL_TREE;
  auto_vec<tree> hv;
  if (handlers && TREE_CODE (handlers) == STATEMENT_LIST)
    for (tree_stmt_iterator i = tsi_start (handlers); !tsi_end_p (i);
	 tsi_next (&i))
      hv.safe_push (tsi_stmt (i));
  else if (handlers)
    hv.safe_push (handlers);
  for (tree h : hv)
    {
      if (TREE_CODE (h) == HERBCEPTION_HANDLER)
	{
	  if (!hb_handler)
	    hb_handler = h;
	}
      else
	{
	  /* Rebuild the HANDLER — build_stmt leaves void_type_node in
	     TREE_TYPE, which HANDLER_TYPE reads as the caught type:
	     a `catch (int)' would degenerate into `catch (void)'.
	     Preserve the type.  */
	  tree nh = build_stmt (EXPR_LOCATION (h), HANDLER,
				HANDLER_PARMS (h), HANDLER_BODY (h));
	  TREE_TYPE (nh) = HANDLER_TYPE (h);
	  append_to_statement_list (nh, &legacy);
	}
    }

  if (!hb_handler)
    {
      /* No `catch throws' handler: calls inside either propagate
	 (spec'd fn) or are unhandled; legacy handlers still wrap the
	 statements.  */
      hb_lower_stmt (&stmts, ctx);
      if (legacy)
	return build_stmt (loc, TRY_CATCH_EXPR, stmts, legacy);
      return stmts;
    }

  tree handler_var = HERBCEPTION_HANDLER_PARMS (hb_handler);
  tree disp = create_artificial_label (loc);
  tree end = create_artificial_label (loc);

  /* Walk the try statements with the dispatch context set.  */
  tree save_disp = ctx->disp_label;
  tree save_hvar = ctx->handler_var;
  ctx->disp_label = disp;
  ctx->handler_var = handler_var;
  hb_lower_stmt (&stmts, ctx);
  ctx->disp_label = save_disp;
  ctx->handler_var = save_hvar;

  /* `catch throws' also catches legacy C++ exceptions — append a
     catch-all (after any typed legacy catches) that fabricates the
     handler's error from the exception object and jumps to the same
     dispatch.  Hosted targets only; under -fno-exceptions legacy
     throws can't exist here anyway.  */
  if (flag_exceptions)
    {
      tree fab = alloc_stmt_list ();
      if (handler_var)
	{
	  tree pairv = hb_emit_fab_pair (&fab, loc);
	  tree v = hb_to_error (TREE_TYPE (handler_var), pairv, loc);
	  append_to_statement_list (hb_assign (loc, handler_var, v),
				    &fab);
	}
      else
	append_to_statement_list
	  (build_stmt (loc, EXPR_STMT, hb_begin_catch_expr (loc)),
	   &fab);
      append_to_statement_list
	(build_stmt (loc, GOTO_EXPR, disp), &fab);
      if (!legacy)
	legacy = alloc_stmt_list ();
      append_to_statement_list (hb_legacy_catch_all (loc, fab,
						     handler_var == NULL_TREE),
				&legacy);
    }

  tree list = alloc_stmt_list ();
  if (handler_var)
    {
      DECL_CONTEXT (handler_var) = ctx->fndecl;
      DECL_CHAIN (handler_var) = NULL_TREE;
      DECL_SEEN_IN_BIND_EXPR_P (handler_var) = 1;
      append_to_statement_list (build_stmt (loc, DECL_EXPR, handler_var),
				&list);
    }
  if (legacy)
    append_to_statement_list
      (build_stmt (loc, TRY_CATCH_EXPR, stmts, legacy), &list);
  else
    append_to_statement_list_force (stmts, &list);
  append_to_statement_list (build1 (GOTO_EXPR, void_type_node, end),
			    &list);
  append_to_statement_list (build_stmt (loc, LABEL_EXPR, disp), &list);

  /* Emit the `catch throws' handler body; the payload is already
     bound at the dispatching call site.  */
  {
    tree save_rt = ctx->rethrow_var;
    ctx->rethrow_var = handler_var;
    tree body = HERBCEPTION_HANDLER_BODY (hb_handler);
    hb_lower_stmt (&body, ctx);
    append_to_statement_list_force (body, &list);
    ctx->rethrow_var = save_rt;
  }

  append_to_statement_list (build_stmt (loc, LABEL_EXPR, end), &list);

  tree vars = handler_var ? handler_var : NULL_TREE;
  return build3 (BIND_EXPR, void_type_node, vars, list, NULL_TREE);
}

/* Lower RETURN_EXPR in a spec'd function into `{ ...; return; }'.  */

static tree
hb_lower_return (hb_ctx *ctx, tree ret, location_t loc)
{
  tree op = TREE_OPERAND (ret, 0);
  tree list = alloc_stmt_list ();
  if (!ctx->hspec)
    return ret;

  if (op == NULL_TREE)
    {
      /* `return;' — plain success.  */
      hb_emit_return (list, ctx, NULL_TREE, loc);
      return list;
    }

  if (op == DECL_RESULT (ctx->fndecl))
    {
      /* The front end applied the return slot optimization: the
	 operand is DECL_RESULT itself and the value was already
	 written through it by the preceding statements.  Keep the
	 RESULT_DECL as the return operand (see
	 hb_emit_return_result) -- rewriting this into a value-less
	 `return;' throws the operand away, and GIMPLE then treats
	 the sret buffer as a local that nothing reads.  */
      hb_emit_return_result (list, ctx, NULL_TREE, loc);
      return list;
    }

  if (TREE_CODE (op) == INIT_EXPR || TREE_CODE (op) == MODIFY_EXPR)
    {
      tree lhs = TREE_OPERAND (op, 0);
      if (DECL_P (lhs) && lhs == DECL_RESULT (ctx->fndecl))
	{
	  tree rtype = TREE_TYPE (TREE_TYPE (ctx->fndecl));
	  if (!VOID_TYPE_P (rtype)
	      && !aggregate_value_p (rtype,
						  TREE_TYPE (ctx->fndecl)))
	    {
	      /* Register-sized return: write into the result local's
		 value slot so the epilogue can forward it — the bare
		 payload local when T and the error type coincide.
		 For a reference-returning function the slot holds a
		 pointer — store the referent's address.  The union was
		 built from the declared return type; DECL_RESULT's own
		 type is the referent for reference returns.  */
	      tree slot = hb_value_slot (ctx->result_var, rtype);
	      tree rhs = TREE_OPERAND (op, 1);
	      if (TREE_CODE (rtype) == REFERENCE_TYPE)
		{
		  /* A reference RESULT_DECL takes its operand through
		     convert_to_reference — (T&) X where X is `&expr'
		     of a (possibly INDIRECT_REF'd) referent — and
		     convert_from_reference marks implicit derefs with
		     INDIRECT_REF over a T& operand.  Peel the wrappers
		     down to the pointer that already represents the
		     reference and store it; only materialize `&expr'
		     when the operand is the referent lvalue itself.
		     Taking `&' of a reference-typed VAR_DECL yields
		     the address of its hidden pointer storage, not
		     the referent's address.  */
		  for (;;)
		    {
		      tree_code code = TREE_CODE (rhs);
		      if (code == NOP_EXPR || code == NON_LVALUE_EXPR
			  || code == VIEW_CONVERT_EXPR
			  || code == CONVERT_EXPR)
			rhs = TREE_OPERAND (rhs, 0);
		      else if (code == ADDR_EXPR
			       && TREE_CODE (TREE_OPERAND (rhs, 0))
				  == INDIRECT_REF)
			rhs = TREE_OPERAND (TREE_OPERAND (rhs, 0), 0);
		      else
			break;
		    }
		  if (INDIRECT_REF_P (rhs))
		    rhs = TREE_OPERAND (rhs, 0);
		  if (TREE_CODE (TREE_TYPE (rhs)) != REFERENCE_TYPE
		      && !POINTER_TYPE_P (TREE_TYPE (rhs)))
		    rhs = build1 (ADDR_EXPR, TREE_TYPE (slot), rhs);
		  if (!same_type_p (TREE_TYPE (slot), TREE_TYPE (rhs)))
		    rhs = fold_convert (TREE_TYPE (slot), rhs);
		}
	      tree init = build2 (TREE_CODE (op), TREE_TYPE (slot),
				  slot, rhs);
	      hb_emit_return (list, ctx, init, loc);
	      return list;
	    }
	  /* sret/void: the DECL_RESULT write-through materializes the
	     value; keep it as the return operand so the slot is live.  */
	  hb_emit_return_result (list, ctx, op, loc);
	  return list;
	}
    }

  /* Anything else: evaluate the operand then return.  */
  hb_emit_return (list, ctx, build_stmt (loc, EXPR_STMT, op), loc);
  return list;
}

/* cp_walk_tree callback lowering the expression-level herbceptions
   nodes.  Statement-level nodes (TRY_BLOCK, RETURN_EXPR, throw/fail
   statements) are handled by hb_lower_list instead.  */

static tree
hb_lower_r (tree *stmt_p, int *walk_subtrees, void *data)
{
  hb_ctx *ctx = (hb_ctx *) data;
  tree stmt = *stmt_p;
  location_t loc = location_of (stmt);

  switch (TREE_CODE (stmt))
    {
    case EXPR_STMT:
      {
	/* A spec'd call whose value the statement discards lowers to
	   the bound-safe shape — its materialized result must not be
	   read outside the TARGET_EXPR that binds it.  */
	tree op = EXPR_STMT_EXPR (stmt);
	while (TREE_CODE (op) == NOP_EXPR
	       && VOID_TYPE_P (TREE_TYPE (op)))
	  op = TREE_OPERAND (op, 0);
	bool spec = TREE_CODE (op) == HERBCEPTION_TRY_EXPR;
	if (!spec
	    && (TREE_CODE (op) == CALL_EXPR
		|| TREE_CODE (op) == AGGR_INIT_EXPR))
	  {
	    tree fn = (TREE_CODE (op) == AGGR_INIT_EXPR
		       ? AGGR_INIT_EXPR_FN (op) : CALL_EXPR_FN (op));
	    tree fntype = fn ? TREE_TYPE (fn) : NULL_TREE;
	    if (fntype && POINTER_TYPE_P (fntype))
	      fntype = TREE_TYPE (fntype);
	    spec = fntype && FUNC_OR_METHOD_TYPE_P (fntype)
		   && type_herbceptions_p (fntype);
	  }
	if (spec)
	  {
	    EXPR_STMT_EXPR (stmt)
	      = hb_lower_try (ctx, op, loc, /*discarded=*/true);
	    *walk_subtrees = 0;
	  }
	break;
      }

    case TARGET_EXPR:
      {
	/* The parser nests the auto-wrap inside a materialization —
	   target_expr<slot, try_expr<call>> for a discarded aggregate
	   call.  The TARGET_EXPR owns the slot's binding, so the
	   machinery lowers in its initializer.  */
	tree init = TARGET_EXPR_INITIAL (stmt);
	tree c = init;
	while (c && TREE_CODE (c) == HERBCEPTION_TRY_EXPR)
	  c = TREE_OPERAND (c, 0);
	if (c && (TREE_CODE (c) == CALL_EXPR
		  || TREE_CODE (c) == AGGR_INIT_EXPR))
	  {
	    tree fn = (TREE_CODE (c) == AGGR_INIT_EXPR
		       ? AGGR_INIT_EXPR_FN (c) : CALL_EXPR_FN (c));
	    tree fntype = fn ? TREE_TYPE (fn) : NULL_TREE;
	    if (fntype && POINTER_TYPE_P (fntype))
	      fntype = TREE_TYPE (fntype);
	    if (fntype && FUNC_OR_METHOD_TYPE_P (fntype)
		&& type_herbceptions_p (fntype))
	      {
		hb_lower_te_init (ctx, stmt, c, fntype, init, loc);
		*walk_subtrees = 0;
	      }
	  }
	break;
      }

    case HERBCEPTION_TRY_EXPR:
      *stmt_p = hb_lower_try (ctx, stmt, loc, /*discarded=*/false);
      *walk_subtrees = 0;
      break;

    case HERBCEPTION_THROW_EXPR:
      {
	tree e = TREE_OPERAND (stmt, 0);
	if (!e)
	  e = ctx->rethrow_var;
	if (!e)
	  {
	    error_at (loc, "no caught error to rethrow");
	    *stmt_p = error_mark_node;
	  }
	else
	  {
	    hb_emit_route (ctx->pre ? ctx->pre
			   : (ctx->pre = alloc_stmt_list ()),
			   ctx, e, loc, /*call_p=*/false);
	    *stmt_p = void_node;
	  }
	*walk_subtrees = 0;
	break;
      }

    case HERBCEPTION_FAIL_EXPR:
      {
	/* Bare `return_failure;' carries a value-initialized E.  */
	tree e = TREE_OPERAND (stmt, 0);
	if (!e)
	  e = build_value_init (ctx->error_type
				? ctx->error_type
				: herbceptions_cxx_std_error_node,
				tf_warning_or_error);
	if (!e || e == error_mark_node)
	  {
	    *stmt_p = error_mark_node;
	    *walk_subtrees = 0;
	    break;
	  }
	hb_emit_route (ctx->pre ? ctx->pre : (ctx->pre = alloc_stmt_list ()),
		       ctx, e, loc, /*call_p=*/false);
	*stmt_p = void_node;
	*walk_subtrees = 0;
      }
      break;

    case HERBCEPTION_CATCH_EXPR:
      *stmt_p = hb_lower_catch (ctx, stmt, loc);
      *walk_subtrees = 0;
      break;

    case TRY_BLOCK:
      /* A try-block reached through a statement-level wrapper
	 (must_not_throw_expr, cleanups) — lower it structurally so
	 `catch throws' handlers register the dispatch context.  */
      *stmt_p = hb_lower_try_block (ctx, stmt);
      *walk_subtrees = 0;
      break;

    case HERBCEPTION_HANDLER:
      /* Handled structurally by hb_lower_try_block.  */
      *walk_subtrees = 0;
      break;

    case CALL_EXPR:
    case AGGR_INIT_EXPR:
      {
	/* A spec'd call that reached here unwrapped — the parser only
	   auto-wraps calls built through postfix-expression handling;
	   constructor/initialization calls (L::L(&l,1) inside a decl's
	   DECL_INITIAL), builtin desugars, and coroutine machinery
	   produce raw CALL_EXPR/AGGR_INIT_EXPR nodes that would
	   otherwise sail through with their channel result unchecked.  */
	tree fn = (TREE_CODE (stmt) == CALL_EXPR
		   ? CALL_EXPR_FN (stmt) : AGGR_INIT_EXPR_FN (stmt));
	tree fntype = fn ? TREE_TYPE (fn) : NULL_TREE;
	if (fntype && POINTER_TYPE_P (fntype))
	  fntype = TREE_TYPE (fntype);
	if (fntype && FUNC_OR_METHOD_TYPE_P (fntype)
	    && type_herbceptions_p (fntype))
	  {
	    *stmt_p = hb_lower_try (ctx, stmt, loc, /*discarded=*/false);
	    *walk_subtrees = 0;
	  }
	break;
      }

    case DECL_EXPR:
      /* A decl's initializer carries expression-level nodes that the
	 default walk doesn't reach (DECL_EXPR_DECL's DECL_INITIAL).  */
      if (tree d = DECL_EXPR_DECL (stmt))
	if (VAR_P (d) && DECL_INITIAL (d))
	  cp_walk_tree (&DECL_INITIAL (d), hb_lower_r, ctx, NULL);
      *walk_subtrees = 0;
      break;

    default:
      break;
    }
  return NULL_TREE;
}

/* Emit any machinery hoisted into ctx->pre immediately before the
   node *tp, replacing it with a statement list {pre..., tp}.  The
   contract of hb_lower_stmt is that it never leaves ctx->pre filled:
   every splice point absorbs the hoisted statements into the scope
   where the hoist happened.  */

static void
hb_splice_pre (tree *tp, hb_ctx *ctx)
{
  if (!ctx->pre)
    return;
  tree l = alloc_stmt_list ();
  append_to_statement_list (ctx->pre, &l);
  append_to_statement_list_force (*tp, &l);
  *tp = l;
  ctx->pre = NULL_TREE;
}

/* Fold the hoisted statements in ctx->pre into a COMPOUND_EXPR chain
   ending in EXPR — for expression positions (loop conditions and
   iterate expressions, if/switch conditions, ?: operands, &&/||
   operands, comma, initializers) where the lowered machinery must run
   inline with the expression rather than being hoisted to statement
   level.  The gimplifier treats each operand statement (DECL_EXPR,
   EXPR_STMT, MODIFY_EXPR, COND_EXPR holding the if(f) route) as an
   expression and evaluates them in order.  Returns EXPR unchanged
   when nothing was hoisted.  */

static tree
hb_pre_compound (hb_ctx *ctx, tree expr)
{
  if (!ctx->pre)
    return expr;
  tree acc = NULL_TREE;
  for (tree_stmt_iterator i = tsi_start (ctx->pre); !tsi_end_p (i);
       tsi_next (&i))
    {
      tree s = tsi_stmt (i);
      acc = acc ? build2 (COMPOUND_EXPR, TREE_TYPE (s), acc, s) : s;
    }
  ctx->pre = NULL_TREE;
  return build2 (COMPOUND_EXPR, TREE_TYPE (expr), acc, expr);
}

/* The recursive statement walker.  */

static void
hb_lower_stmt (tree *tp, hb_ctx *ctx)
{
  tree stmt = *tp;
  if (!stmt)
    return;

  switch (TREE_CODE (stmt))
    {
    case STATEMENT_LIST:
      {
	tree newl = alloc_stmt_list ();
	for (tree_stmt_iterator i = tsi_start (stmt); !tsi_end_p (i);
	     tsi_next (&i))
	  {
	    tree *sp = tsi_stmt_ptr (i);
	    hb_lower_stmt (sp, ctx);
	    append_to_statement_list_force (*sp, &newl);
	  }
	*tp = newl;
	break;
      }

    case BIND_EXPR:
      hb_lower_stmt (&BIND_EXPR_BODY (stmt), ctx);
      break;

    case EH_SPEC_BLOCK:
      hb_lower_stmt (&EH_SPEC_STMTS (stmt), ctx);
      break;

    case CLEANUP_POINT_EXPR:
      /* The operand can be a statement list (after the hoisted
	 machinery lands there) or a bare expression.  */
      hb_lower_stmt (&TREE_OPERAND (stmt, 0), ctx);
      break;

    case IF_STMT:
      if (IF_COND (stmt))
	{
	  cp_walk_tree (&IF_COND (stmt), hb_lower_r, ctx, NULL);
	  IF_COND (stmt) = hb_pre_compound (ctx, IF_COND (stmt));
	}
      hb_splice_pre (tp, ctx);
      hb_lower_stmt (&THEN_CLAUSE (stmt), ctx);
      hb_lower_stmt (&ELSE_CLAUSE (stmt), ctx);
      break;

    case WHILE_STMT:
      hb_lower_stmt (&WHILE_BODY (stmt), ctx);
      if (WHILE_COND (stmt))
	{
	  cp_walk_tree (&WHILE_COND (stmt), hb_lower_r, ctx, NULL);
	  WHILE_COND (stmt) = hb_pre_compound (ctx, WHILE_COND (stmt));
	}
      hb_splice_pre (tp, ctx);
      break;

    case DO_STMT:
      hb_lower_stmt (&DO_BODY (stmt), ctx);
      if (DO_COND (stmt))
	{
	  cp_walk_tree (&DO_COND (stmt), hb_lower_r, ctx, NULL);
	  DO_COND (stmt) = hb_pre_compound (ctx, DO_COND (stmt));
	}
      hb_splice_pre (tp, ctx);
      break;

    case FOR_STMT:
      hb_lower_stmt (&FOR_INIT_STMT (stmt), ctx);
      if (FOR_COND (stmt))
	{
	  cp_walk_tree (&FOR_COND (stmt), hb_lower_r, ctx, NULL);
	  FOR_COND (stmt) = hb_pre_compound (ctx, FOR_COND (stmt));
	}
      if (FOR_EXPR (stmt))
	{
	  cp_walk_tree (&FOR_EXPR (stmt), hb_lower_r, ctx, NULL);
	  FOR_EXPR (stmt) = hb_pre_compound (ctx, FOR_EXPR (stmt));
	}
      hb_splice_pre (tp, ctx);
      hb_lower_stmt (&FOR_BODY (stmt), ctx);
      break;

    case RANGE_FOR_STMT:
      hb_lower_stmt (&RANGE_FOR_INIT_STMT (stmt), ctx);
      hb_lower_stmt (&RANGE_FOR_BODY (stmt), ctx);
      break;

    case SWITCH_STMT:
      if (SWITCH_STMT_COND (stmt))
	{
	  cp_walk_tree (&SWITCH_STMT_COND (stmt), hb_lower_r, ctx, NULL);
	  SWITCH_STMT_COND (stmt)
	    = hb_pre_compound (ctx, SWITCH_STMT_COND (stmt));
	}
      hb_splice_pre (tp, ctx);
      hb_lower_stmt (&SWITCH_STMT_BODY (stmt), ctx);
      break;

    case HANDLER:
      hb_lower_stmt (&HANDLER_BODY (stmt), ctx);
      break;

    case TRY_CATCH_EXPR:
      hb_lower_stmt (&TREE_OPERAND (stmt, 0), ctx);
      hb_lower_stmt (&TREE_OPERAND (stmt, 1), ctx);
      break;

    case CLEANUP_STMT:
      hb_lower_stmt (&CLEANUP_BODY (stmt), ctx);
      break;

    case TRY_BLOCK:
      *tp = hb_lower_try_block (ctx, stmt);
      break;

    case RETURN_EXPR:
      if (TREE_OPERAND (stmt, 0))
	cp_walk_tree (&TREE_OPERAND (stmt, 0), hb_lower_r, ctx, NULL);
      *tp = hb_lower_return (ctx, stmt, location_of (stmt));
      /* The lowered form is a statement list; prepend hoisted
	 machinery to it.  */
      if (ctx->pre)
	{
	  tree l = alloc_stmt_list ();
	  append_to_statement_list (ctx->pre, &l);
	  append_to_statement_list_force (*tp, &l);
	  *tp = l;
	  ctx->pre = NULL_TREE;
	}
      break;

    case HERBCEPTION_THROW_EXPR:
      if (TREE_OPERAND (stmt, 0))
	cp_walk_tree (&TREE_OPERAND (stmt, 0), hb_lower_r, ctx, NULL);
      {
	tree l = ctx->pre ? ctx->pre : alloc_stmt_list ();
	ctx->pre = NULL_TREE;
	append_to_statement_list
	  (hb_lower_fail (ctx, stmt, true, location_of (stmt)), &l);
	*tp = l;
      }
      break;

    case HERBCEPTION_FAIL_EXPR:
      if (TREE_OPERAND (stmt, 0))
	cp_walk_tree (&TREE_OPERAND (stmt, 0), hb_lower_r, ctx, NULL);
      {
	tree l = ctx->pre ? ctx->pre : alloc_stmt_list ();
	ctx->pre = NULL_TREE;
	append_to_statement_list
	  (hb_lower_fail (ctx, stmt, false, location_of (stmt)), &l);
	*tp = l;
      }
      break;

    default:
      /* Leaf statement: walk for nested expression-level nodes
	 (try-exprs inside initializers, conditions, calls).  */
      cp_walk_tree (tp, hb_lower_r, ctx, NULL);
      hb_splice_pre (tp, ctx);
      break;
    }
}

/* Entry point: lower the herbceptions nodes in FNDECL's saved body
   into the deterministic error channel.  Called from cp_genericize
   before cp_genericize_tree.  */

void
genericize_herbceptions_body (tree fndecl)
{
  if (!flag_herbceptions)
    return;

  tree spec_list = TYPE_RAISES_EXCEPTIONS (TREE_TYPE (fndecl));
  tree hspec = HERBCEPTION_SPEC_P (spec_list)
    ? TREE_PURPOSE (spec_list) : NULL_TREE;

  hb_ctx ctx;
  memset (&ctx, 0, sizeof (ctx));
  ctx.fndecl = fndecl;
  ctx.hspec = hspec;
  ctx.error_type = herbceptions_cxx_std_error_node;

  if (hspec)
    {
      ctx.error_type = hb_error_type_for (hspec);
      ctx.union_type
	= hb_result_type (TREE_TYPE (TREE_TYPE (fndecl)),
			  ctx.error_type, TREE_TYPE (fndecl));
      location_t loc = DECL_SOURCE_LOCATION (fndecl);
      ctx.result_var = hb_local (loc, ctx.union_type, "__hb_result");
      ctx.failed_var = hb_local (loc, boolean_type_node, "__hb_failed");
      /* The channel slots are read only by the epilogue — outside
	 the gimple the middle end can see — so their stores would look
	 dead.  Keep them volatile until the channel becomes an
	 integral part of the call/return model.  */
      TREE_THIS_VOLATILE (ctx.result_var) = 1;
      TREE_THIS_VOLATILE (ctx.failed_var) = 1;
      /* __hb_failed stays a plain stack slot: any register the body
	 writes it through can be clobbered by calls and scope-exit
	 cleanups between the store and the return.  expand_function_end
	 loads it into the architecture's flag register on the shared
	 return edge — after all cleanups — and the epilogue fuses that
	 register into the carry flag at ret.  */

      cfun->herbceptions_error_slot = ctx.result_var;
      cfun->herbceptions_failed_slot = ctx.failed_var;

      /* Prepend `{ decl __hb_result; decl __hb_failed = false; }' —
	 the discriminant defaults to success so that falling off the
	 end or a plain return never reports a garbage flag.  */
      tree list = alloc_stmt_list ();
      append_to_statement_list (build_stmt (loc, DECL_EXPR,
					    ctx.result_var), &list);
      append_to_statement_list (build_stmt (loc, DECL_EXPR,
					    ctx.failed_var), &list);
      append_to_statement_list
	(hb_assign (loc, ctx.failed_var, boolean_false_node), &list);
      tree body = DECL_SAVED_TREE (fndecl);
      if (TREE_CODE (body) == BIND_EXPR)
	{
	  BIND_EXPR_VARS (body)
	    = chainon (BIND_EXPR_VARS (body), ctx.result_var);
	  BIND_EXPR_VARS (body)
	    = chainon (BIND_EXPR_VARS (body), ctx.failed_var);
	  append_to_statement_list_force (BIND_EXPR_BODY (body), &list);
	  BIND_EXPR_BODY (body) = list;
	}
      else
	{
	  append_to_statement_list_force (body, &list);
	  DECL_SAVED_TREE (fndecl)
	    = build3 (BIND_EXPR, void_type_node,
		      chainon (ctx.result_var, ctx.failed_var),
		      list, NULL_TREE);
	}
    }

  hb_lower_stmt (&DECL_SAVED_TREE (fndecl), &ctx);
  if (ctx.pre)
    {
      tree l = alloc_stmt_list ();
      append_to_statement_list (ctx.pre, &l);
      append_to_statement_list_force (DECL_SAVED_TREE (fndecl), &l);
      DECL_SAVED_TREE (fndecl) = l;
    }

  /* The legacy-EH boundary: a spec'd function wraps its (now
     channel-lowered) body in a catch-all so a legacy exception
     escaping it converts to the std::error channel — or terminates
     for return_failure{E}, which cannot fabricate an arbitrary E.
     Hosted only: under -fno-exceptions a legacy throw can't be
     raised or caught at all.  */
  if (hspec && flag_exceptions)
    {
      bool fabricate
	= HERBCEPTION_SPEC_KIND (hspec) != hsk_return_failure;
      tree body = DECL_SAVED_TREE (fndecl);
      location_t bloc = DECL_SOURCE_LOCATION (fndecl);
      if (TREE_CODE (body) == BIND_EXPR)
	{
	  tree inner = BIND_EXPR_BODY (body);
	  BIND_EXPR_BODY (body)
	    = hb_fn_boundary (&ctx, inner, bloc, fabricate);
	}
      else
	DECL_SAVED_TREE (fndecl)
	  = hb_fn_boundary (&ctx, body, bloc, fabricate);
    }
}

/* --------------------------------------------------------------------
 * Herbceptions channel traits (`__is_herbceptions_*' / `__has_*').
 * These mirror the `__is_nothrow_*'/'__has_nothrow_*' family: the
 * operation is resolved with the same machinery, then the resolved
 * function's spec is checked for the `throws' channel.
 * -------------------------------------------------------------------- */

/* True if TYPE is a `return_failure{E}' function type (or ptr/ref to
   one).  */

bool
hb_is_return_failure_fn (tree type)
{
  if (!type)
    return false;
  if (TYPE_REF_P (type) || POINTER_TYPE_P (type))
    type = TREE_TYPE (type);
  if (!FUNC_OR_METHOD_TYPE_P (type))
    return false;
  tree spec_list = TYPE_RAISES_EXCEPTIONS (type);
  tree hspec = HERBCEPTION_SPEC_P (spec_list)
    ? TREE_PURPOSE (spec_list) : NULL_TREE;
  return (hspec
	  && HERBCEPTION_SPEC_KIND (hspec) == hsk_return_failure);
}

/* Walk cb for hb_expr_call_fntype.  */

static tree
hb_find_call (tree *tp, int *walk_subtrees, void *data)
{
  tree *out = (tree *) data;
  tree t = *tp;
  if ((TREE_CODE (t) == CALL_EXPR || TREE_CODE (t) == AGGR_INIT_EXPR)
      && !*out)
    *out = t;
  return NULL_TREE;
}

/* The resolved callee's function type inside EXPR (the result of a
   `constructible'/'invocable'/'assignable'-style resolution), or
   NULL_TREE.  */

static tree
hb_expr_call_fntype (tree expr)
{
  if (!expr || expr == error_mark_node)
    return NULL_TREE;
  tree call = NULL_TREE;
  cp_walk_tree (&expr, hb_find_call, &call, NULL);
  if (!call)
    return NULL_TREE;
  tree fn = (TREE_CODE (call) == AGGR_INIT_EXPR
	     ? AGGR_INIT_EXPR_FN (call) : CALL_EXPR_FN (call));
  if (!fn)
    return NULL_TREE;
  tree ft = TREE_TYPE (fn);
  if (POINTER_TYPE_P (ft))
    ft = TREE_TYPE (ft);
  return FUNC_OR_METHOD_TYPE_P (ft) ? ft : NULL_TREE;
}

/* True if the resolved operation EXPR calls a function declared with a
   `throws' or `return_failure{E}' spec.  */

bool
hb_op_throws (tree expr)
{
  return type_herbceptions_p (hb_expr_call_fntype (expr));
}

/* True if the resolved operation EXPR calls a function declared
   `throws' (or a dependent `throws(expr)') — the implicit
   std::error channel.  `return_failure{E}' uses a different
   channel and does not satisfy `{ expr } throws'.  */

bool
hb_op_throws_spec (tree expr)
{
  tree ft = hb_expr_call_fntype (expr);
  if (!ft)
    return false;
  tree spec_list = TYPE_RAISES_EXCEPTIONS (ft);
  tree hspec = HERBCEPTION_SPEC_P (spec_list)
    ? TREE_PURPOSE (spec_list) : NULL_TREE;
  return (hspec
	  && (HERBCEPTION_SPEC_KIND (hspec) == hsk_throws
	      || HERBCEPTION_SPEC_KIND (hspec) == hsk_throws_expr));
}

/* `__is_herbceptions_throwsable(T)': T can be thrown via `throw throws'
   — a complete `std::error_domain<T>' exists.  Looks the domain
   template up in `std' and requires the instantiation to be complete;
   an undefined primary template (incomplete type) means "not
   throwsable".  */

bool
hb_throwsable_p (tree type)
{
  if (!flag_herbceptions || !type || type == error_mark_node
      || dependent_type_p (type))
    return false;
  /* The implicit channel type (and any `std::error'-shaped record) is
     always throwsable.  */
  if (herbceptions_cxx_std_error_node
      && (same_type_p (type, herbceptions_cxx_std_error_node)
	  || cxx_std_error_type_p (type)))
    return true;
  if (!std_node)
    return false;
  tree d = lookup_qualified_name (std_node, "error_domain");
  if (!d || !DECL_CLASS_TEMPLATE_P (d))
    return false;
  tree args = make_tree_vec (1);
  TREE_VEC_ELT (args, 0) = type;
  tree inst = lookup_template_class (d, args, NULL_TREE, NULL_TREE,
				     tf_none);
  if (!inst || inst == error_mark_node)
    return false;
  /* A complete, non-dependent error_domain<T> means T is throwsable.  */
  inst = complete_type (inst);
  return inst && TREE_CODE (inst) == RECORD_TYPE
	 && COMPLETE_TYPE_P (inst);
}

/* `__has_herbceptions_throws_{constructor,copy,assign,move_assign}(T)':
   the relevant special member of T is declared `throws'.  Trivial and
   implicitly-declared members never carry the channel; non-class types
   cannot.  */

static bool
hb_throws_spec_fn_p (tree fn)
{
  return (fn && TREE_CODE (fn) == FUNCTION_DECL
	  && FUNC_OR_METHOD_TYPE_P (TREE_TYPE (fn))
	  && type_herbceptions_p (TREE_TYPE (fn)));
}

bool
hb_has_throws_member (tree type, special_function_kind sfk)
{
  if (!CLASS_TYPE_P (type))
    return false;

  if (sfk == sfk_copy_assignment || sfk == sfk_move_assignment)
    {
      /* operator= overloads.  */
      tree fns = lookup_fnfields (TYPE_BINFO (type),
				  ovl_op_identifier (false, NOP_EXPR),
				  0, tf_none);
      for (ovl_iterator it (fns); it; ++it)
	if (hb_throws_spec_fn_p (*it)
	    && special_function_p (*it) == sfk)
	  return true;
      return false;
    }

  /* Constructors (sfk_constructor for a usable default ctor,
     sfk_copy_constructor for the copy).  */
  for (tree fn : ovl_range (CLASSTYPE_CONSTRUCTORS (type)))
    {
      if (!hb_throws_spec_fn_p (fn))
	continue;
      if (sfk == sfk_copy_constructor)
	{
	  if (copy_fn_p (fn) > 0)
	    return true;
	}
      else if (sfk == sfk_constructor)
	{
	  /* `isDefaultConstructor': callable with no args — no
	     required user parameters.  */
	  if (!copy_fn_p (fn) && !move_fn_p (fn)
	      && (!FUNCTION_FIRST_USER_PARMTYPE (fn)
		  || FUNCTION_FIRST_USER_PARMTYPE (fn)
		     == void_list_node))
	    return true;
	}
    }
  return false;
}

/* `__invoke_herbceptions_return_failure_result<F>': the synthetic
   `struct { value_type; error_type; }' carrying F's success return
   type and its `return_failure{E}' error type (`void' when F is not a
   `return_failure' function).  The members are typedefs like
   `std::invoke_result's.  */

tree
hb_invoke_return_failure_result (tree type)
{
  tree fntype = type;
  if (TYPE_REF_P (fntype) || POINTER_TYPE_P (fntype))
    fntype = TREE_TYPE (fntype);

  tree value_type = void_type_node;
  tree error_type = void_type_node;
  if (FUNC_OR_METHOD_TYPE_P (fntype))
    {
      value_type = TREE_TYPE (fntype);
      tree spec_list = TYPE_RAISES_EXCEPTIONS (fntype);
      tree hspec = HERBCEPTION_SPEC_P (spec_list)
	? TREE_PURPOSE (spec_list) : NULL_TREE;
      if (hspec
	  && HERBCEPTION_SPEC_KIND (hspec) == hsk_return_failure)
	error_type = HERBCEPTION_SPEC_ERROR_TYPE (hspec);
    }

  location_t loc = BUILTINS_LOCATION;
  tree etype = lang_hooks.types.make_type (RECORD_TYPE);
  etype = begin_class_definition (etype);
  tree vtd = build_decl (loc, TYPE_DECL, get_identifier ("value_type"),
			 value_type);
  tree etd = build_decl (loc, TYPE_DECL, get_identifier ("error_type"),
			 error_type);
  DECL_CONTEXT (vtd) = etype;
  DECL_CONTEXT (etd) = etype;
  DECL_CHAIN (vtd) = etd;
  TYPE_FIELDS (etype) = vtd;
  TYPE_BINFO (etype) = make_tree_binfo (0);
  BINFO_OFFSET (TYPE_BINFO (etype)) = size_zero_node;
  BINFO_TYPE (TYPE_BINFO (etype)) = etype;
  etype = finish_struct (etype, NULL_TREE);
  return etype;
}
