/*-----------------------------------------------------------------------------
 *
 * prov_rewriter_main.c
 *		Main entry point to the provenance rewriter.
 *		
 *		AUTHOR: lord_pretzel
 *
 *		
 *
 *-----------------------------------------------------------------------------
 */

#include "log/logger.h"
#include "instrumentation/timing_instrumentation.h"
#include "configuration/option.h"

#include "provenance_rewriter/prov_rewriter.h"
#include "provenance_rewriter/prov_utility.h"
#include "provenance_rewriter/game_provenance/gp_main.h"
#include "provenance_rewriter/semiring_combiner/sc_main.h"
#include "provenance_rewriter/pi_cs_rewrites/pi_cs_main.h"
#include "provenance_rewriter/pi_cs_rewrites/pi_cs_composable.h"
#include "provenance_rewriter/update_and_transaction/prov_update_and_transaction.h"
#include "provenance_rewriter/transformation_rewrites/transformation_prov_main.h"
#include "provenance_rewriter/uncertainty_rewrites/uncert_rewriter.h"
#include "provenance_rewriter/xml_rewrites/xml_prov_main.h"
#include "temporal_queries/temporal_rewriter.h"

#include "model/query_operator/query_operator.h"
#include "model/query_operator/query_operator_model_checker.h"
#include "model/datalog/datalog_model.h"
#include "analysis_and_translate/analyze_dl.h"
#include "model/query_operator/operator_property.h"
#include "model/node/nodetype.h"
#include "model/list/list.h"
#include "model/set/set.h"

#include "utility/string_utils.h"

/* function declarations */
static QueryOperator *findProvenanceComputations (QueryOperator *op, Set *haveSeen);
static QueryOperator *rewriteProvenanceComputation (ProvenanceComputation *op);
static void markTableAccessAndAggregation (QueryOperator *op);
static QueryOperator *addTopAggForCoarse (QueryOperator *op);

/* function definitions */
Node *
provRewriteQBModel (Node *qbModel)
{
    if (isA(qbModel, List))
        return (Node *) provRewriteQueryList((List *) qbModel);
    else if (IS_OP(qbModel))
        return (Node *) provRewriteQuery((QueryOperator *) qbModel);
    else if (IS_DL_NODE(qbModel))
    {
        createRelToRuleMap(qbModel);
        return (Node *) rewriteForGP(qbModel);
    }
    FATAL_LOG("cannot rewrite node <%s>", nodeToString(qbModel));

    return NULL;
}

List *
provRewriteQueryList (List *list)
{
    FOREACH(QueryOperator,q,list)
        q_his_cell->data.ptr_value = provRewriteQuery(q);

    return list;
}

QueryOperator *
provRewriteQuery (QueryOperator *input)
{
    Set *seen = PSET();
    Node *inputProp = input->properties;

    QueryOperator *result = findProvenanceComputations(input, seen);
    result->properties = inputProp;

    return result;
}


static QueryOperator *
findProvenanceComputations (QueryOperator *op, Set *haveSeen)
{
    // is provenance computation? then rewrite
    if (isA(op, ProvenanceComputation))
        return rewriteProvenanceComputation((ProvenanceComputation *) op);

    // else search for children with provenance
    FOREACH(QueryOperator,c,op->inputs)
    {
        if (!hasSetElem(haveSeen, c))
        {
            addToSet(haveSeen, c);
            findProvenanceComputations(c, haveSeen);
        }
    }

    return op;
}

static QueryOperator *
rewriteProvenanceComputation (ProvenanceComputation *op)
{
    QueryOperator *result;
    boolean requiresPostFiltering = FALSE;

    // for a sequence of updates of a transaction merge the sequence into a single
    // query before rewrite.
    if (op->inputType == PROV_INPUT_UPDATE_SEQUENCE
            || op->inputType == PROV_INPUT_TRANSACTION
            || op->inputType == PROV_INPUT_REENACT
            || op->inputType == PROV_INPUT_REENACT_WITH_TIMES)
    {
        START_TIMER("rewrite - merge update reenactments");
        mergeUpdateSequence(op);
        STOP_TIMER("rewrite - merge update reenactments");

        // need to restrict to updated rows?
        if ((op->inputType == PROV_INPUT_TRANSACTION
                || op->inputType == PROV_INPUT_REENACT_WITH_TIMES
                || op->inputType == PROV_INPUT_REENACT)
                && HAS_STRING_PROP(op,PROP_PC_ONLY_UPDATED))
        {
            START_TIMER("rewrite - restrict to updated rows");
            restrictToUpdatedRows(op);
            requiresPostFiltering = HAS_STRING_PROP(op,PROP_PC_REQUIRES_POSTFILTERING);
            STOP_TIMER("rewrite - restrict to updated rows");
        }
    }

    if (op->inputType == PROV_INPUT_TEMPORAL_QUERY)
    {
        return rewriteImplicitTemporal((QueryOperator *) op);
    }

    if (op->inputType == PROV_INPUT_UNCERTAIN_QUERY)
    {
        return rewriteUncert((QueryOperator *) op);
    }

    // turn operator graph into a tree since provenance rewrites currently expect a tree
    if (isRewriteOptionActivated(OPTION_TREEIFY_OPERATOR_MODEL))
    {
        treeify((QueryOperator *) op);
        INFO_OP_LOG("treeified operator model:", op);
        DEBUG_NODE_BEATIFY_LOG("treeified operator model:", op);
        ASSERT(isTree((QueryOperator *) op));
    }

    //semiring comb operations
    boolean isCombinerActivated = isSemiringCombinerActivatedOp((QueryOperator *) op);

    // apply provenance rewriting if required
    switch(op->provType)
    {
        case PROV_PI_CS:
            if (isRewriteOptionActivated(OPTION_PI_CS_USE_COMPOSABLE))
                result =  rewritePI_CSComposable(op);
            else
                result = rewritePI_CS(op);
            removeParent(result, (QueryOperator *) op);

            //semiring comb operations
            if(isCombinerActivated)
            {
                Node *addExpr;
                Node *multExpr;

                addExpr = getSemiringCombinerAddExpr((QueryOperator *) op);
                multExpr = getSemiringCombinerMultExpr((QueryOperator *) op);

                INFO_LOG("user has provied a semiring combiner: %s:\n\n%s", beatify(nodeToString(addExpr)), beatify(nodeToString(multExpr)));
                result = addSemiringCombiner(result,addExpr,multExpr);
                INFO_OP_LOG("Add semiring combiner:", result);
            }

            break;
        case PROV_COARSE_GRAINED:
            // add annotations for table access and for combiners (aggregation)
        	markTableAccessAndAggregation((QueryOperator *) op);
            result = rewritePI_CS(op);
            removeParent(result, (QueryOperator *) op);
            // write method that adds aggregation on top
            result = addTopAggForCoarse(result);
            break;
        case PROV_TRANSFORMATION:
            result =  rewriteTransformationProvenance((QueryOperator *) op);
            break;
        case PROV_XML:
            result = rewriteXML(op); //TODO
            break;
        case PROV_NONE:
            result = OP_LCHILD(op);
            break;
    }

    // for reenactment we may have to postfilter results if only rows affected by the transaction should be shown
    if (requiresPostFiltering)
    {
        START_TIMER("rewrite - restrict to updated rows by postfiltering");
        result = filterUpdatedInFinalResult(op, result);
        STOP_TIMER("rewrite - restrict to updated rows by postfiltering");
        INFO_OP_LOG("after adding selection for postfiltering", result);
    }

    return result;
}

static void
markTableAccessAndAggregation (QueryOperator *op)
{
      FOREACH(QueryOperator, o, op->inputs)
	  {
           if(isA(o,TableAccessOperator))
           {
        	   DEBUG_LOG("mark tableAccessOperator.");
        	   SET_BOOL_STRING_PROP(o, PROP_COARSE_GRAINED_TABLEACCESS_MARK);
           }
           if(isA(o,AggregationOperator))
           {
        	   DEBUG_LOG("mark tableAccessOperator.");
        	   SET_BOOL_STRING_PROP(o, PROP_PC_SC_AGGR_OPT);
        	   //SET_BOOL_STRING_PROP(o, PROP_COARSE_GRAINED_AGGREGATION_MARK);
           }

           markTableAccessAndAggregation(o);
	  }
}

static QueryOperator *
addTopAggForCoarse (QueryOperator *op)
{
    List *provAttr = getOpProvenanceAttrNames(op);
    List *projExpr = NIL;
    int cnt = 0;
    List *provPosList = NIL;

    FOREACH(char, c, provAttr)
    {
    	provPosList = appendToTailOfListInt(provPosList, cnt);
    	AttributeReference *a = createAttrsRefByName(op, c);
    	FunctionCall *f = createFunctionCall ("BITORAGG", singleton(a));
    	projExpr = appendToTailOfList(projExpr, f);
    	cnt ++;
    }

    ProjectionOperator *newOp = createProjectionOp(projExpr, op, NIL, provAttr);
    newOp->op.provAttrs = provPosList;

    op->parents = singleton(newOp);


	return (QueryOperator *) newOp;
}

