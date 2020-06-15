/*-----------------------------------------------------------------------------
 *
 * update_ps_main.c
 *
 *
 *      AUTHOR: Pengyuan Li
 *
 *
 *
 *-----------------------------------------------------------------------------
 */

/*
 * Header files
 */
#include "common.h"
#include "configuration/option.h"
#include "log/logger.h"
#include "mem_manager/mem_mgr.h"

#include "model/node/nodetype.h"
#include "model/query_operator/query_operator.h"
#include "model/query_operator/operator_property.h"
#include "model/expression/expression.h"
#include "model/list/list.h"
#include "model/set/hashmap.h"
#include "model/bitset/bitset.h"

#include "provenance_rewriter/pi_cs_rewrites/pi_cs_main.h"
#include "provenance_rewriter/update_ps/update_ps_main.h"
#include "provenance_rewriter/prov_rewriter.h"
#include "provenance_rewriter/prov_utility.h"
#include "provenance_rewriter/coarse_grained/coarse_grained_rewrite.h"

#include "sql_serializer/sql_serializer.h"
#include "metadata_lookup/metadata_lookup.h"

/*
 * Macro
 */
#define DELETE_RULE_1 0 // DROP
#define DELETE_RULE_2 1 // DELETE(APPROXIMATE)
#define DELETE_RULE_3 3 // DELETE(ACCURATE)

#define INSERT_RULE_1 100 // INSERT(APPROXIMATE)
#define INSERT_RULE_2 101 // INSERT(ACCURATE)

#define UPDATE_RULE_1 200 // DELETE(APPROXIMATE) -> INSERT(APPROXIMATE)
#define UPDATE_RULE_2 201 // DELETE(APPROXIMATE) -> INSERT(ACCURATE)
#define UPDATE_RULE_3 202 // DELETE(ACCURATE) -> INSERT(APPROXIMATE)
#define UPDATE_RULE_4 203 // DELETE(ACCURATE) -> INSERT(ACCURATE)

/*
 * Function Declaration
 */

//DELETE
static char* update_ps_delete(QueryOperator *query, QueryOperator *updateQuery,
		psInfo *PSInfo, int ruleNum);
static char* update_ps_delete_drop(QueryOperator *query,
		QueryOperator *updateQuery, psInfo *PSInfo);
static char* update_ps_delete_approximate(QueryOperator *query,
		QueryOperator *updateQuery, psInfo *PSInfo);
static char* update_ps_delete_accurate(QueryOperator *query,
		QueryOperator *updateQuery, psInfo *PSInfo);

//INSERT
static char* update_ps_insert(QueryOperator *query, QueryOperator *updateQuery,
		psInfo *PSInfo, int ruleNum);
static char* update_ps_insert_approximate(QueryOperator *query,
		QueryOperator *updateQuery, psInfo *PSInfo);
static char* update_ps_insert_accurate(QueryOperator *query,
		QueryOperator *updateQuery, psInfo *PSInfo);

//UPDATE
static char* update_ps_update(QueryOperator *query, QueryOperator *updateQuery,
		psInfo *PSInfo, int ruleNum);
static char* update_ps_del_ins_1(QueryOperator *query,
		QueryOperator *updateQuery, psInfo *PSInfo);

//OTHER AUXILIARY METHODS
static char* getUpdatedTable(QueryOperator *op);
static psAttrInfo* getUpdatedTablePSAttrInfo(psInfo *PSInfo, char *tableName);
static List* getAllTables(psInfo *PSInfo);
static char* createResultComponent(char *tableName, char *psAttr, char *ps);
static ProjectionOperator* createDummyProjTree(QueryOperator *updateQuery);
static void reversePSInfo(psInfo *PSInfo, char *updatedTable);
static QueryOperator* rewriteTableAccessOperator(TableAccessOperator *op,
		psInfo *PSInfo);
static boolean getTableAccessOps(Node *op, List **l);
static BitSet* bitOrResults(HashMap *old, HashMap *new, StringInfo *result);

/*
 * Function Implementation
 */
char*
update_ps(ProvenanceComputation *qbModel) {

	//initialize some parameters to get the ps, left chile(update statement) and right child(query);
	ProvenanceComputation *op = qbModel;
	Node *coarsePara = NULL;
	psInfo *psPara = NULL;

	coarsePara = (Node*) getStringProperty((QueryOperator*) op,
	PROP_PC_COARSE_GRAINED);
	psPara = createPSInfo(coarsePara);

	DEBUG_LOG("use coarse grained fragment parameters: %s",
			nodeToString((Node* ) psPara));

	/*
	 * get the left and right childred respectively;
	 * left child is a update statement
	 * right child is a normal query
	 */

	QueryOperator *op1 = (QueryOperator*) op;
	QueryOperator *rChild = OP_RCHILD(op1);
	QueryOperator *lChild = (QueryOperator*) OP_LCHILD(op1);
	op1->inputs = singleton(rChild);

	removeParent(lChild, (QueryOperator*) op);

	DEBUG_NODE_BEATIFY_LOG(
			"\n#######################\n \t Query:\n#######################\n",
			rChild);
//	DEBUG_NODE_BEATIFY_LOG(
//			"\n#######################\n \t PS INFO:\n#######################\n",
//			psPara);
	DEBUG_NODE_BEATIFY_LOG(
			"\n#######################\n \t Update query:\n#######################\n",
			lChild);
	INFO_OP_LOG(
			"\n#######################\n \t Update query:\n#######################\n",
			lChild);

	char *result = NULL;

	markTableAccessAndAggregation((QueryOperator*) op, (Node*) psPara);

	//mark the number of table - used in provenance scratch
	markNumOfTableAccess((QueryOperator*) op);
	bottomUpPropagateLevelAggregation((QueryOperator*) op, psPara);

	QueryOperator *newQuery = rewritePI_CS(op);
	newQuery = addTopAggForCoarse(newQuery);
//	result = update_ps_insert(newQuery, lChild, psPara, INSERT_RULE_1);
	int operation = 2;

	if (operation == 1) {
		result = update_ps_delete(newQuery, lChild, psPara, DELETE_RULE_3);
	} else if (operation == 2) {
		result = update_ps_insert(newQuery, lChild, psPara, INSERT_RULE_2);
	} else if (operation == 3) {
		result = update_ps_update(newQuery, lChild, psPara, UPDATE_RULE_1);
	}

	return result;
}

/*
 * DELETE OPERATION UPDATING
 */
static char*
update_ps_delete(QueryOperator *query, QueryOperator *updateQuery,
		psInfo *PSInfo, int ruleNum) {
	char *result = NULL;
	switch (ruleNum) {
	case DELETE_RULE_1:
		result = update_ps_delete_drop(query, updateQuery, PSInfo);
		break;

	case DELETE_RULE_2:
		result = update_ps_delete_approximate(query, updateQuery, PSInfo);
		break;
	case DELETE_RULE_3:
		result = update_ps_delete_accurate(query, updateQuery, PSInfo);
		break;
	}
	return result;
}

static char*
update_ps_delete_drop(QueryOperator *query, QueryOperator *updateQuery,
		psInfo *PSInfo) {

	//GET UPDATED TABLE
	char *updatedTableName = getUpdatedTable((QueryOperator*) updateQuery);
	if (!updatedTableName)
		return NULL;

	//GET UPDATE TABLE PS
	psAttrInfo *updatedTablePSInfo = getUpdatedTablePSAttrInfo(PSInfo,
			updatedTableName);
	if (!updatedTablePSInfo)
		return NULL; // the updated table does not relate to the query

	//get all the tables in the query;
	List *tableList = getAllTables(PSInfo);

	StringInfo result = makeStringInfo();
	appendStringInfo(result, "%s", "{");
	for (int i = 0; i < LIST_LENGTH(tableList); i++) {

		char *tableName =
				(char*) ((Constant*) getNthOfListP(tableList, i))->value;
		List *psAttrInfoList = (List*) getMapString(PSInfo->tablePSAttrInfos,
				tableName);

		/*
		 *  One table could have multiple partition attributes.
		 *  Iterate to get all partition attributes and the provenance sketchs.
		 */
		for (int j = 0; j < LIST_LENGTH(psAttrInfoList); j++) {
			psAttrInfo *info = getNthOfListP(psAttrInfoList, j);

			appendStringInfo(result, "%s",
					createResultComponent(tableName, info->attrName, "NULL"));

		}
	}
	appendStringInfo(result, "%s", "}");

	return result->data;

}

static char*
update_ps_delete_approximate(QueryOperator *query, QueryOperator *updateQuery,
		psInfo *PSInfo) {
	char *updatedTableName = getUpdatedTable((QueryOperator*) updateQuery);
	if (!updatedTableName)
		return NULL;

	List *tableList = getAllTables(PSInfo);

	StringInfo result = makeStringInfo();
	appendStringInfo(result, "%s", "{");
	//iteratoion to get all the participation tables' ps info
	for (int i = 0; i < LIST_LENGTH(tableList); i++) {

		char *tableName =
				(char*) ((Constant*) getNthOfListP(tableList, i))->value;
		List *psAttrInfoList = (List*) getMapString(PSInfo->tablePSAttrInfos,
				tableName);

		/*
		 *  One table could have multiple partition attributes.
		 *  Iterate to get all partition attributes and the provenance sketchs.
		 */
		for (int j = 0; j < LIST_LENGTH(psAttrInfoList); j++) {
			psAttrInfo *info = getNthOfListP(psAttrInfoList, j);

			appendStringInfo(result, "%s",
					createResultComponent(tableName, info->attrName,
							bitSetToString(info->BitVector)));

		}
	}
	appendStringInfo(result, "%s", "}");

	return result->data;
}

static char*
update_ps_delete_accurate(QueryOperator *query, QueryOperator *updateQuery,
		psInfo *PSInfo) {
	/*
	 *	Capture PS of ((PS_r - Delta_r) join PS_s)
	 */

	/*
	 * TODO: (optimization) check if the updated table is related to the sql, for example: updated r but the query run on s and t;
	 *
	 */

	//GET ALL THE TABLE ACCESS OPERATOR TO REWRITE
	List *taList = NIL;
	getTableAccessOps((Node*) query, &taList);

	char *updatedTable = getUpdatedTable(updateQuery);
	for (int i = 0; i < LIST_LENGTH(taList); i++) {
		TableAccessOperator *taOp = (TableAccessOperator*) getNthOfListP(taList,
				i);

		List *parent = ((QueryOperator*) taOp)->parents;
		QueryOperator *rewriteOp = rewriteTableAccessOperator(taOp, PSInfo);
		((QueryOperator*) getHeadOfListP(parent))->inputs = replaceNode(
				((QueryOperator*) getHeadOfListP(parent))->inputs, taOp,
				rewriteOp);

		if (streq(taOp->tableName, updatedTable)) {
			// in this contidional case: construct PS_r - Delta_r;
			Node *notCond = copyObject(
					((SelectionOperator*) updateQuery)->cond);
			List *conds = LIST_MAKE(notCond,
					((SelectionOperator* )rewriteOp)->cond);
			((SelectionOperator*) rewriteOp)->cond = andExprList(conds);
			DEBUG_NODE_BEATIFY_LOG("WHAT IS THE SEL CO:",
					((SelectionOperator* ) rewriteOp)->cond);

		}
	}

	//GET CAPTURE SQL
	char *capSql = serializeOperatorModel((Node*) query);

	//CAPTURE NEW PS
	List *attrNames = getAttrNames(query->schema);
	HashMap *psMap = getPS(capSql, attrNames);
	DEBUG_NODE_BEATIFY_LOG("WHAT IS THE NWE PS", psMap);

	//GET RESULT AND RETURN
	StringInfo result = makeStringInfo();
	List *keys = getKeys(PSInfo->tablePSAttrInfos);
	for (int i = 0; i < LIST_LENGTH(keys); i++) {
		char *tableName = (char*) ((Constant*) getNthOfListP(keys, i))->value;
		List *psAttrInfos = (List*) getMapString(PSInfo->tablePSAttrInfos,
				tableName);

		for (int j = 0; j < LIST_LENGTH(psAttrInfos); j++) {
			psAttrInfo *info = (psAttrInfo*) getNthOfListP(psAttrInfos, j);
			char *attrName = info->attrName;
			StringInfo str = makeStringInfo();
			appendStringInfo(str, "\"%s_%s_%s", "prov", tableName, attrName);

			List *keys2 = getKeys(psMap);
			for (int k = 0; k < LIST_LENGTH(keys2); k++) {
				char *ss = ((Constant*) getNthOfListP(keys2, k))->value;
				if (strncmp(str->data, ss, strlen(str->data)) == 0) {
					Constant *newPSValue = (Constant*) getMapString(psMap, ss);
					int bitSetLength = info->BitVector->length;
					BitSet *bitSet = newBitSet(bitSetLength);
					int bitSetIntValue = *((int*) newPSValue->value);

					int index = info->BitVector->length - 1;

					//set bit for each position;
					while (bitSetIntValue > 0) {
						if (bitSetIntValue % 2 == 1) {
							setBit(bitSet, index, 1);
						}
						index--;
						bitSetIntValue /= 2;
					}
					char *bits = bitSetToString(bitSet);

					//reverse the bits;
					for (index = 0; index < bitSetLength / 2; index++) {
						bits[index] ^= bits[bitSetLength - index - 1];
						bits[bitSetLength - index - 1] ^= bits[index];
						bits[index] ^= bits[bitSetLength - index - 1];
					}

					bitSet = stringToBitset(bits);

					appendStringInfo(result, "%s",
							createResultComponent(tableName, attrName,
									bitSetToString(bitSet)));

				}
			}
		}
	}

	return result->data;
}

/*
 * INSERT OPRATION UPDATING
 */

static char*
update_ps_insert(QueryOperator *query, QueryOperator *updateQuery,
		psInfo *PSInfo, int ruleNum) {
	char *result = NULL;
	switch (ruleNum) {
	case INSERT_RULE_1:
		result = update_ps_insert_approximate(query, updateQuery, PSInfo);
		break;
	case INSERT_RULE_2:
		result = update_ps_insert_accurate(query, updateQuery, PSInfo);
		break;
	}
	return result;
}
static char*
update_ps_insert_accurate(QueryOperator *query, QueryOperator *updateQuery,
		psInfo *PSInfo) {
	//delta tuple join whole table;


	char *updatedTable = getUpdatedTable(updateQuery);
	ProjectionOperator *proOpDummy = createDummyProjTree(updateQuery);
	List *taList = NIL;
	getTableAccessOps((Node*) query, &taList);

	for (int i = 0; i < LIST_LENGTH(taList); i++) {
		TableAccessOperator *taOp = (TableAccessOperator*) getNthOfListP(taList,
				i);
		if (streq(taOp->tableName, updatedTable)) {

			List *parent = ((QueryOperator*) taOp)->parents;
			((QueryOperator*) getHeadOfListP(parent))->inputs = replaceNode(
					((QueryOperator*) getHeadOfListP(parent))->inputs, taOp,
					proOpDummy);

			proOpDummy->op.parents = singleton(parent);
		}
	}

	char *capSql = serializeOperatorModel((Node*) query);

	List *attrNames = getAttrNames(query->schema);
	DEBUG_LOG("PS Attr Names : %s", stringListToString(attrNames));
	HashMap *psMap = getPS(capSql, attrNames);

	// TODO Here we can optimize by check the result of psMap, if all the ps is 0, which means that there are no new fragment to be added.

	StringInfo result = makeStringInfo();
	appendStringInfo(result, "%s", "{");
	bitOrResults(PSInfo->tablePSAttrInfos, psMap, &result);
	appendStringInfo(result, "%s", "}");

	return result->data;

//	return NULL;
}
static char*
update_ps_insert_approximate(QueryOperator *query, QueryOperator *updateQuery,
		psInfo *PSInfo) {
	char *updatedTable = getUpdatedTable(updateQuery);

	//check the Dalta tuple's ps, and directly set it to '1' if it is '0';

	List *psAttrInfos = (List*) getMapString(PSInfo->tablePSAttrInfos,
			updatedTable);
	char *psAttr = NULL;
//	int index = -1;
	psAttrInfo *updatedTableInfo = NULL;
	for (int i = 0; i < LIST_LENGTH(psAttrInfos); i++) {
		psAttrInfo *info = (psAttrInfo*) getNthOfListP(psAttrInfos, i);
		psAttr = info->attrName;
		updatedTableInfo = info;
	}

	int psAttrValue = -1;
	FOREACH(QueryOperator, o, updateQuery->inputs)
	{
		if (isA(o, ConstRelOperator)) {
			List *attrList = o->schema->attrDefs;
			for (int i = 0; i < LIST_LENGTH(attrList); i++) {
				AttributeDef *def = (AttributeDef*) getNthOfListP(attrList, i);
				if (streq(def->attrName, psAttr)) {
					List *values = ((ConstRelOperator*) o)->values;
					DEBUG_NODE_BEATIFY_LOG("what is nodessss:",
							(Constant* ) getNthOfListP(values, i));
					psAttrValue =
							*((int*) ((Constant*) getNthOfListP(values, i))->value);

				}
			}
			break;
		}
	}
//	DEBUG_NODE_BEATIFY_LOG("previous psInfo:", PSInfo);
	List *ranges = updatedTableInfo->rangeList;
	for (int i = 1; i < LIST_LENGTH(ranges); i++) {
		Constant *previous = (Constant*) getNthOfListP(ranges, i - 1);
		Constant *current = (Constant*) getNthOfListP(ranges, i);

		int pValue = *((int*) previous->value);
		int cValue = *((int*) current->value);

		if (psAttrValue >= pValue && psAttrValue < cValue) {
//			DEBUG_LOG("the modify position: %d", i - 1);
			setBit(updatedTableInfo->BitVector, i - 1, 1);
		}

	}
//	DEBUG_NODE_BEATIFY_LOG("previoussssss psInfo:", PSInfo);
//
//	DEBUG_LOG("psattrrrrrrrrr: %s, and its value: %d", psAttr, psAttrValue);
	ProjectionOperator *proOpDummy = createDummyProjTree(updateQuery);

	psInfo *reservedPS = (psInfo*) copyObject(PSInfo);

	reversePSInfo(PSInfo, updatedTable);

	List *taList = NIL;
	getTableAccessOps((Node*) query, &taList);

	//For each TableAccessOperator, rewrite it with the 1 as 0 and 0 as 1 or make a dummy table only when it is the updated table;
	for (int i = 0; i < LIST_LENGTH(taList); i++) {
		TableAccessOperator *taOp = (TableAccessOperator*) getNthOfListP(taList,
				i);
		if (!streq(taOp->tableName, updatedTable)) {
			//rewrite current tableaccess; with specific ranges;
			List *parent = ((QueryOperator*) taOp)->parents;
			QueryOperator *rewriteOp = rewriteTableAccessOperator(
					(TableAccessOperator*) taOp, PSInfo);
			((QueryOperator*) getHeadOfListP(parent))->inputs = replaceNode(
					((QueryOperator*) getHeadOfListP(parent))->inputs, taOp,
					rewriteOp);

		} else {
			List *parent = ((QueryOperator*) taOp)->parents;
			((QueryOperator*) getHeadOfListP(parent))->inputs = replaceNode(
					((QueryOperator*) getHeadOfListP(parent))->inputs, taOp,
					proOpDummy);

			proOpDummy->op.parents = singleton(parent);
		}
	}

	char *capSql = serializeOperatorModel((Node*) query);

	//RUN
	List *attrNames = getAttrNames(query->schema);
	DEBUG_LOG("PS Attr Names : %s", stringListToString(attrNames));
	HashMap *psMap = getPS(capSql, attrNames);

	// TODO Here we can optimize by check the result of psMap, if all the ps is 0, which means that there are no new fragment to be added.

	StringInfo result = makeStringInfo();
	appendStringInfo(result, "%s", "{");
	bitOrResults(reservedPS->tablePSAttrInfos, psMap, &result);
	appendStringInfo(result, "%s", "}");

	return result->data;
}

/*
 * UPDATE OPERATION UPDATING
 */

static char*
update_ps_update(QueryOperator *query, QueryOperator *updateQuery,
		psInfo *PSInfo, int ruleNum) {
	char *result = NULL;
	switch (ruleNum) {
	case UPDATE_RULE_1:
		result = update_ps_del_ins_1(query, updateQuery, PSInfo);
		break;

	case UPDATE_RULE_2:
		break;
	case UPDATE_RULE_3:
		break;
	case UPDATE_RULE_4:
		break;
	}
	return result;
}

static char*
update_ps_del_ins_1(QueryOperator *query, QueryOperator *updateQuery,
		psInfo *PSInfo) {
	/*
	 * DELETE: APPROXIMATE
	 * INSERT: APPROXIMATE
	 */

	// CHECK IF THE UPDATED TABLE IS RELATED TO TABLES IN THE QUERIES
	char *updatedTable = getUpdatedTable(updateQuery);
	List *allParticipatedTables = NIL;
	getTableAccessOps((Node*) query, &allParticipatedTables);
	boolean isFind = FALSE;
	FOREACH(TableAccessOperator, op, allParticipatedTables)
	{
		if (streq(op->tableName, updatedTable)) {
			isFind = TRUE;
			break;
		}
	}
	if (!isFind) {
		return NULL;
	}

	//DELETE: KEEP THE PS
	//INSERT:

	List *projExprs = ((ProjectionOperator*) updateQuery)->projExprs;
	List *conds = NIL;
	for (int i = 0; i < LIST_LENGTH(projExprs); i++) {
		Node *curr = (Node*) getNthOfListP(projExprs, i);
		if (isA(curr, CaseExpr)) {
			List *whenClause = ((CaseExpr*) curr)->whenClauses;
			for (int j = 0; j < LIST_LENGTH(whenClause); j++) {
				Node *node = getNthOfListP(whenClause, j);
				if (isA(node, CaseWhen)) {
					DEBUG_LOG("make condds");
					Operator *op = createOpExpr("=",
							LIST_MAKE(copyObject(((CaseExpr* )curr)->elseRes),
									copyObject(((CaseWhen* ) node)->then)));
					conds = appendToTailOfList(conds, op);
				}
			}
		}
	}

	//TODO HERE CAN OPTIMIZE, CHECK IF THE NEW INSERT TUPLES BELONGS TO PS_r BASED ON PS ATTRIBUTE, IF YES, DELTA_r JOIN X(PS_S) ELSE DELTA_r JOIN S.

	psInfo *reservedPS = (psInfo*) copyObject(PSInfo);
	reversePSInfo(PSInfo, updatedTable);

	for (int i = 0; i < LIST_LENGTH(allParticipatedTables); i++) {

		TableAccessOperator *taOp = (TableAccessOperator*) getNthOfListP(
				allParticipatedTables, i);
		if (streq(updatedTable, taOp->tableName)) {
			List *parent = ((QueryOperator*) taOp)->parents;
			Node *selAndConds = andExprList(conds);
			SelectionOperator *selOp = createSelectionOp(selAndConds,
					(QueryOperator*) taOp, parent,
					getAttrNames(taOp->op.schema));
			((QueryOperator*) getHeadOfListP(parent))->inputs = replaceNode(
					((QueryOperator*) getHeadOfListP(parent))->inputs, taOp,
					selOp);
		}

	}

	char *capSql = serializeOperatorModel((Node*) query);

	DEBUG_LOG("%s", capSql);
	List *attrNames = getAttrNames(query->schema);
	DEBUG_LOG("PS Attr Names : %s", stringListToString(attrNames));
	HashMap *psMap = getPS(capSql, attrNames);

	DEBUG_NODE_BEATIFY_LOG("WHAT IS NEW PS", psMap);
	StringInfo result = makeStringInfo();
	appendStringInfo(result, "%s", "{");
	bitOrResults(reservedPS->tablePSAttrInfos, psMap, &result);
	appendStringInfo(result, "%s", "}");

	return result->data;

	return sql;
}

/*
 * OTHER METHODS
 */
static BitSet*
bitOrResults(HashMap *old, HashMap *new, StringInfo *result) {
	DEBUG_NODE_BEATIFY_LOG("previous hashMap", old);
	DEBUG_NODE_BEATIFY_LOG("new hashMap", new);


	List *keys = getKeys(old);

	for (int i = 0; i < LIST_LENGTH(keys); i++) {
		char *tableName = (char*) ((Constant*) getNthOfListP(keys, i))->value;

		List *psAttrInfos = (List*) getMapString(old, tableName);
		for (int j = 0; j < LIST_LENGTH(psAttrInfos); j++) {
			psAttrInfo *info = (psAttrInfo*) getNthOfListP(psAttrInfos, j);
			char *attrName = info->attrName;

			StringInfo str = makeStringInfo();
			appendStringInfo(str, "\"%s_%s_%s", "prov", tableName, attrName);

			List *keys2 = getKeys(new);
			for (int k = 0; k < LIST_LENGTH(keys2); k++) {
				char *ss = ((Constant*) getNthOfListP(keys2, k))->value;
				if (strncmp(str->data, ss, strlen(str->data)) == 0) {
					Constant *newPSValue = (Constant*) getMapString(new, ss);
					int bitSetLength = info->BitVector->length;
					BitSet *bitSet = newBitSet(bitSetLength);
					DEBUG_NODE_BEATIFY_LOG("what is the generated bit set:", bitSet);
					unsigned long bitSetIntValue = *((unsigned long*) newPSValue->value);
					DEBUG_LOG("what is the unsigned value: %ld\n", bitSetIntValue);

//					int index = info->BitVector->length - 1;

					//set bit for each position;
//					while (bitSetIntValue > 0) {
//						if (bitSetIntValue % 2 == 1) {
//							setBit(bitSet, index, 1);
//						}
//						index--;
//						bitSetIntValue /= 2;
//					}
//					char *bits = bitSetToString(bitSet);
//
//					//reverse the bits;
//					for (index = 0; index < bitSetLength / 2; index++) {
//						bits[index] ^= bits[bitSetLength - index - 1];
//						bits[bitSetLength - index - 1] ^= bits[index];
//						bits[index] ^= bits[bitSetLength - index - 1];
//					}
					int index = 0;
					while(bitSetIntValue > 0) {
						if(bitSetIntValue % 2 == 1) {
							setBit(bitSet, index, 1);
						} else {
							setBit(bitSet, index, 0);
						}
						index++;
						bitSetIntValue /= 2;

					}
					while(index < bitSetLength) {
						setBit(bitSet, index++, 0);
					}
//					bitSet = stringToBitset(bits);
//					bitSet = longToBitSet( *((unsigned long*) newPSValue->value));

//					DEBUG_LOG("previous BITSET:%ld", *((unsigned long*) info->BitVector->value));
					DEBUG_NODE_BEATIFY_LOG("previous BITSET:%ld", info->BitVector);
					DEBUG_NODE_BEATIFY_LOG("new BITSET:%ld",bitSet);
					DEBUG_LOG("#########################################");
					info->BitVector = bitOr(info->BitVector, bitSet);

					appendStringInfo(*result, "%s",
							createResultComponent(tableName, attrName,
									bitSetToString(info->BitVector)));

				}
			}

		}
	}
	return NULL;
}

static char*
getUpdatedTable(QueryOperator *op) {

	FOREACH(QueryOperator, operator, op->inputs)
	{
		if (isA(operator, TableAccessOperator)) {
			return ((TableAccessOperator*) operator)->tableName;
		}
	}

	return NULL;

}

static psAttrInfo*
getUpdatedTablePSAttrInfo(psInfo *PSInfo, char *tableName) {

	char *tmp = tableName;

	while (*tmp) {
		*tmp = tolower(*tmp);
		tmp++;
	}

	if (hasMapStringKey(PSInfo->tablePSAttrInfos, tableName)) {
		return (psAttrInfo*) getMapString(PSInfo->tablePSAttrInfos, tableName);
	}

	return NULL;
}

static List*
getAllTables(psInfo *PSInfo) {
	return getKeys(PSInfo->tablePSAttrInfos);
}

static char*
createResultComponent(char *tableName, char *psAttr, char *ps) {

	StringInfo result = makeStringInfo();

	appendStringInfo(result, "(%s[%s:%s])", tableName, psAttr, ps);

	return result->data;
}

static ProjectionOperator*
createDummyProjTree(QueryOperator *updateQuery) {

	List *attrNames;
	List *attrValues;
	List *attrDefs;
	FOREACH(QueryOperator, o, updateQuery->inputs)
	{
		if (isA(o, ConstRelOperator)) {
			ConstRelOperator *cro = (ConstRelOperator*) o;
			attrNames = cro->op.schema->attrDefs;
			attrValues = cro->values;
			attrDefs = cro->op.schema->attrDefs;
			// TODO get attr data type
			break;
		}
	}
	TableAccessOperator *taOp = createTableAccessOp("DUAL", NULL, "DUAL", NIL,
			singleton("DUMMY"), singletonInt(DT_STRING));
	ProjectionOperator *projOp = createProjectionOp(attrValues,
			(QueryOperator*) taOp, NIL, attrNames);
	projOp->op.schema->attrDefs = attrDefs;
	return projOp;
}

static void reversePSInfo(psInfo *PSInfo, char *updatedTable) {
	HashMap *allAttrInfos = PSInfo->tablePSAttrInfos;

	List *keys = getKeys(allAttrInfos);

	for (int i = 0; i < LIST_LENGTH(keys); i++) {
		char *tableName = (char*) ((Constant*) getNthOfListP(keys, i))->value;

		if (!streq(tableName, updatedTable)) {
			List *psAttrInfoList = (List*) getMapString(allAttrInfos,
					tableName);

			for (int j = 0; j < LIST_LENGTH(psAttrInfoList); j++) {

				psAttrInfo *info = (psAttrInfo*) getNthOfListP(psAttrInfoList,
						j);

				char *bitset = bitSetToString(info->BitVector);
				for (int index = 0; index < strlen(bitset); index++) {
					if (bitset[index] == '0') {
						setBit(info->BitVector, index, 1);
					} else {
						setBit(info->BitVector, index, 0);
					}
				}
			}
		}
	}

}

static boolean getTableAccessOps(Node *op, List **l) {
	if (op == NULL)
		return TRUE;

	if (isA(op, TableAccessOperator)) {
		*l = appendToTailOfList(*l, op);
	}

	return visit((Node*) op, getTableAccessOps, l);
}

static QueryOperator*
rewriteTableAccessOperator(TableAccessOperator *op, psInfo *PSInfo) {

	List *attrInfos = (List*) getMapString(PSInfo->tablePSAttrInfos,
			op->tableName);

	List *selAndList = NIL;
	for (int i = 0; i < LIST_LENGTH(attrInfos); i++) {
		psAttrInfo *info = (psAttrInfo*) getNthOfListP(attrInfos, i);

		List *rangeList = info->rangeList;
		char *bitSet = bitSetToString(info->BitVector);

		AttributeReference *psAttrRef = createAttrsRefByName(
				(QueryOperator*) op, info->attrName);
		for (int j = 0; j < strlen(bitSet); j++) {
			if (bitSet[j] == '1') {
				Operator *lOp = createOpExpr(">=",
						LIST_MAKE(copyObject(psAttrRef),
								copyObject(getNthOfListP(rangeList, j))));
				Operator *rOp = createOpExpr("<",
						LIST_MAKE(copyObject(psAttrRef),
								copyObject(getNthOfListP(rangeList, j + 1))));
				Node *andExp = andExprList(LIST_MAKE(lOp, rOp));
				selAndList = appendToTailOfList(selAndList, andExp);
			}
		}

	}
	Node *selOrExp = orExprList(selAndList);
	SelectionOperator *selOp = createSelectionOp((Node*) selOrExp,
			(QueryOperator*) op, op->op.parents,
			getQueryOperatorAttrNames((QueryOperator*) op));
//	INFO_OP_LOG("selecction operator", selOp);
	op->op.parents = singleton(selOp);
	return (QueryOperator*) selOp;
}
