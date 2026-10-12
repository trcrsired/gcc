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

/* Build union { T value; P error; }.  If T is void or returns in
   memory, the payload is just P.  */

static tree
hb_result_type (tree rettype, tree etype, tree fntype)
{
  if (etype == NULL_TREE || etype == error_mark_node)
    etype = herbceptions_cxx_std_error_node;
  if (VOID_TYPE_P (rettype)
      || (fntype && targetm.calls.return_in_memory (rettype, fntype)))
    return etype;
  if (same_type_p (rettype, etype))
    return etype;
  tree t = lang_hooks.types.make_type (UNION_TYPE);
  tree valuef = build_decl (BUILTINS_LOCATION, FIELD_DECL,
			    get_identifier ("value"), rettype);
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
    if (same_type_p (TREE_TYPE (f), etype))
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
    if (same_type_p (TREE_TYPE (f), t))
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

/* The success-value view of RESULT for value type T.  */

static tree
hb_value_view (tree result, tree t)
{
  tree f = hb_value_field (TREE_TYPE (result), t);
  return f ? build3 (COMPONENT_REF, t, result, f, NULL_TREE) : result;
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

  /* { .domain = dcall, .code = ccall } in the ABI record's own
     field order.  */
  vec<constructor_elt, va_gc> *elts = NULL;
  for (tree f = TYPE_FIELDS (fabtype); f; f = DECL_CHAIN (f))
    {
      if (TREE_CODE (f) != FIELD_DECL)
	continue;
      tree val = id_equal (DECL_NAME (f), "domain") ? dcall : ccall;
      constructor_elt e = { f, convert (TREE_TYPE (f), val) };
      vec_safe_push (elts, e);
    }
  tree r = build_constructor (fabtype, elts);
  if (!same_type_p (target, fabtype))
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

/* Emit the failure exit/route for payload E at LOC into LIST:
   - inside a `catch throws' scope: bind the handler var, goto disp;
   - in a spec'd fn: `__hb_result.error = e; __hb_failed = 1; return;'
   - in main(): trap — an escaped error hits the main boundary;
   - otherwise an unhandled-failure error.  CALL_P marks the route as
     coming from a call failure; `throw throws' escaping a plain
     function is a hard error even in main.  */

static void
hb_emit_route (tree list, hb_ctx *ctx, tree e, location_t loc,
	       bool call_p = true)
{
  if (ctx->disp_label)
    {
      if (ctx->handler_var)
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
    if (DECL_NAME (f) == id)
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
  tree ctype = TREE_TYPE (call);
  if (VOID_TYPE_P (ctype))
    ctype = err;

  tree union_type;
  if (TREE_CODE (call) == AGGR_INIT_EXPR
      || TREE_CODE (call) == TARGET_EXPR
      || targetm.calls.return_in_memory (ctype, fntype))
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
  append_to_statement_list (build_stmt (loc, EXPR_STMT, call), &ctx->pre);
  append_to_statement_list
    (hb_assign (loc, u,
		build_call_expr_internal_loc (loc, IFN_HERBCEPTION_RESULT,
					      TREE_TYPE (u), 0)),
     &ctx->pre);
  append_to_statement_list
    (hb_assign (loc, f,
		build_call_expr_internal_loc (loc, IFN_HERBCEPTION_FAILED,
					      TREE_TYPE (f), 0)),
     &ctx->pre);

  /* Bind the either members: r.value = u.value, r.error = u.error,
     r.failed = f.  */
  tree errf = hb_error_field (union_type, err);
  tree errslot = errf
    ? build3 (COMPONENT_REF, err, u, errf, NULL_TREE) : u;
  tree valf = hb_value_field (union_type, ctype);
  tree valslot = valf
    ? build3 (COMPONENT_REF, ctype, u, valf, NULL_TREE) : u;
  tree rv = hb_named_field (etype, "value");
  tree re = hb_named_field (etype, "error");
  tree rf = hb_named_field (etype, "failed");
  append_to_statement_list
    (hb_assign (loc,
		build3 (COMPONENT_REF, ctype, r, rv, NULL_TREE),
		valslot),
     &ctx->pre);
  append_to_statement_list
    (hb_assign (loc,
		build3 (COMPONENT_REF, err, r, re, NULL_TREE),
		errslot),
     &ctx->pre);
  append_to_statement_list
    (hb_assign (loc,
		build3 (COMPONENT_REF, boolean_type_node, r, rf, NULL_TREE),
		f),
     &ctx->pre);
  return r;
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
  else
    {
      /* Bare `throw throws': rethrow the caught error through the
	 enclosing context.  */
      gcc_assert (is_throw);
      e = ctx->rethrow_var;
      if (!e)
	{
	  error_at (loc, "no caught error to rethrow");
	  return build_stmt (loc, EXPR_STMT, error_mark_node);
	}
    }
  hb_emit_route (list, ctx, e, loc, /*call_p=*/false);
  return list;
}

/* Lower HERBCEPTION_TRY_EXPR: `try (call)' and the implicit wrapper
   on spec'd calls.  The call and channel reads hoist into ctx->pre;
   returns the value expression to stand in place of the node.  */

static tree
hb_lower_try (hb_ctx *ctx, tree stmt, location_t loc)
{
  /* stmt is normally a HERBCEPTION_TRY_EXPR wrapping the call, but
     raw CALL_EXPR/AGGR_INIT_EXPR nodes arrive here directly from
     hb_lower_r when a spec'd call bypassed the parser's auto-wrap
     (ctor-init, desugared calls, coroutine machinery).  */
  tree call = (TREE_CODE (stmt) == CALL_EXPR
	       || TREE_CODE (stmt) == AGGR_INIT_EXPR)
	      ? stmt : TREE_OPERAND (stmt, 0);
  tree ctype = TREE_TYPE (call);
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
  if (VOID_TYPE_P (ctype) || TREE_CODE (call) == AGGR_INIT_EXPR
      || TREE_CODE (call) == TARGET_EXPR
      || targetm.calls.return_in_memory (ctype, fntype))
    union_type = err;
  else
    union_type = hb_result_type (ctype, err, fntype);

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

  if (target)
    {
      /* A materialized (aggregate/sret) call: the slot the callee
	 writes binds inside the TARGET_EXPR's own scope, so the call
	 must stay inside — hoisting it would leave the slot referenced
	 before its binding.  The channel reads and the failure route
	 run inline after the materialization instead:
	 { target<slot, call>; u = RESULT; f = FAILED; if (f) route;
	   slot }.  */
      tree seq
	= build2 (COMPOUND_EXPR, void_type_node, target,
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
	hb_emit_route (body, ctx, errslot, loc);
	seq = build2 (COMPOUND_EXPR, void_type_node, seq,
		      build3 (COND_EXPR, void_type_node, f,
			      body ? body : void_node, NULL_TREE));
      }
      return build2 (COMPOUND_EXPR, ctype, seq,
		     TARGET_EXPR_SLOT (target));
    }

  append_to_statement_list (build_stmt (loc, EXPR_STMT, call), &ctx->pre);

  /* u = .HERBCEPTION_RESULT (); f = .HERBCEPTION_FAILED (); */
  append_to_statement_list
    (hb_assign (loc, u,
		build_call_expr_internal_loc (loc, IFN_HERBCEPTION_RESULT,
					      TREE_TYPE (u), 0)),
     &ctx->pre);
  append_to_statement_list
    (hb_assign (loc, f,
		build_call_expr_internal_loc (loc, IFN_HERBCEPTION_FAILED,
					      TREE_TYPE (f), 0)),
     &ctx->pre);

  /* if (f) <route> */
  {
    tree body = alloc_stmt_list ();
    tree errf = hb_error_field (union_type, err);
    tree errslot = errf
      ? build3 (COMPONENT_REF, err, u, errf, NULL_TREE) : u;
    hb_emit_route (body, ctx, errslot, loc);
    tree cond = build3 (COND_EXPR, void_type_node, f,
			body ? body : void_node, NULL_TREE);
    append_to_statement_list (cond, &ctx->pre);
  }

  /* The try-expr's value: u.value for scalar calls, the operand
     itself for sret materializations, void otherwise.  */
  if (VOID_TYPE_P (ctype))
    return void_node;
  if (TREE_CODE (call) == AGGR_INIT_EXPR)
    return AGGR_INIT_EXPR_SLOT (call);
  if (TREE_CODE (call) == TARGET_EXPR)
    return TARGET_EXPR_SLOT (call);
  if (targetm.calls.return_in_memory (ctype, fntype)
      && TREE_CODE (call) == CALL_EXPR)
    /* An sret callee wrote the value into the hidden return-slot
       argument.  */
    return build1 (INDIRECT_REF, ctype, CALL_EXPR_ARG (call, 0));
  return hb_value_view (u, ctype);
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
	append_to_statement_list
	  (build_stmt (EXPR_LOCATION (h), HANDLER,
		       HANDLER_PARMS (h), HANDLER_BODY (h)),
	   &legacy);
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

  if (TREE_CODE (op) == INIT_EXPR || TREE_CODE (op) == MODIFY_EXPR)
    {
      tree lhs = TREE_OPERAND (op, 0);
      if (DECL_P (lhs) && lhs == DECL_RESULT (ctx->fndecl))
	{
	  if (ctx->union_type != ctx->error_type)
	    {
	      /* Register-sized return: write into the union's value
		 field so the epilogue can forward it.  */
	      tree slot = hb_value_view (ctx->result_var,
					 TREE_TYPE (lhs));
	      tree init = build2 (TREE_CODE (op), TREE_TYPE (slot),
				  slot, TREE_OPERAND (op, 1));
	      hb_emit_return (list, ctx, init, loc);
	      return list;
	    }
	  /* sret/void: the DECL_RESULT write-through already
	     materializes the value; the channel carries only the
	     error.  */
	  hb_emit_return (list, ctx, op, loc);
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
    case HERBCEPTION_TRY_EXPR:
      *stmt_p = hb_lower_try (ctx, stmt, loc);
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
      hb_emit_route (ctx->pre ? ctx->pre : (ctx->pre = alloc_stmt_list ()),
		     ctx, TREE_OPERAND (stmt, 0), loc, /*call_p=*/false);
      *stmt_p = void_node;
      *walk_subtrees = 0;
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
	    *stmt_p = hb_lower_try (ctx, stmt, loc);
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
