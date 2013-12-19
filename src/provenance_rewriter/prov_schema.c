/*-----------------------------------------------------------------------------
 *
 * prov_schema.c
 *			  
 *		
 *		AUTHOR: lord_pretzel
 *
 *		
 *
 *-----------------------------------------------------------------------------
 */

#include "common.h"

#include "log/logger.h"

#include "uthash.h"

#include "mem_manager/mem_mgr.h"
#include "model/list/list.h"
#include "model/node/nodetype.h"
#include "model/query_operator/query_operator.h"
#include "provenance_rewriter/prov_schema.h"

/* consts */
#define PROV_ATTR_PREFIX "PROV_"

/* data types */
typedef struct ProvSchemaInfo
{
    List *provAttrs;
    RelCount *rels;
} ProvSchemaInfo;

/* function declarations */
static boolean findBaserelationsVisitor (Node *node, ProvSchemaInfo *status);
static int getRelCount(ProvSchemaInfo *info, char *tableName);
static boolean findTablerefVisitor (Node *node, ProvSchemaInfo *status);

/* definitions */
List *
getProvenanceAttributes(QueryOperator *q, ProvenanceType type)
{
    switch(type) {
        case PI_CS:
        {
            ProvSchemaInfo *pSchema= NEW(ProvSchemaInfo);

            findBaserelationsVisitor((Node *) q, pSchema);
            return pSchema->provAttrs;
        }
        case TRANSFORMATION:
            return singleton(strdup("tprov"));
    }
}

List *
getProvenanceAttrNames (char *table, List *attrs, int count)
{
     List *result = NIL;

     FOREACH(char,a,attrs)
         result = appendToTailOfList(result, getProvenanceAttrName(table, a, count));

     return result;
}

char *
getProvenanceAttrName (char *table, char *attr, int count)
{
    char *countStr = CALLOC(1,128);
    if (count > 0)
        sprintf(countStr,"_%u", count);
    return CONCAT_STRINGS(PROV_ATTR_PREFIX, strdup(table), countStr, "_",
            strdup(attr));
}

static boolean
findBaserelationsVisitor (Node *node, ProvSchemaInfo *status)
{
    if (node == NULL)
        return TRUE;

    if (isA(node, TableAccessOperator))
    {
        TableAccessOperator *t = (TableAccessOperator *) node;

        FOREACH(AttributeDef,a,t->op.schema->attrDefs)
            status->provAttrs = appendToTailOfList(status->provAttrs,
                    getProvenanceAttrName(t->tableName,a->attrName,
                            getRelCount(status, t->tableName)));
    }

    return visit(node, findBaserelationsVisitor, status);
}

static int
getRelCount(ProvSchemaInfo *info, char *tableName)
{
    return getRelNameCount(&(info->rels), tableName);
//
//
//
//    HASH_FIND_STR(info->rels,tableName,relCount);
//    if (relCount == NULL)
//    {
//        relCount = NEW(RelCount);
//        relCount->count = 0;
//        relCount->relName = strdup(tableName);
//        HASH_ADD_STR(info->rels,relName,relCount);
//    }
//    else
//        relCount->count++;
//
//    return relCount->count;
}

int
getRelNameCount(RelCount **relCount, char *tableName)
{
    RelCount *relC = NULL;

    HASH_FIND_STR((*relCount), tableName, relC);
    if (relC == NULL)
    {
        relC = NEW(RelCount);
        relC->count = 0;
        relC->relName = strdup(tableName);
        HASH_ADD_KEYPTR(hh, (*relCount), relC->relName, strlen(relC->relName),
                relC);
    }
    else
        relC->count++;

    return relC->count;
}

List *
getQBProvenanceAttrList (ProvenanceStmt *stmt)
{
    switch(stmt->provType)
    {
        case PI_CS:
            //TODO
        default:
        {
            ProvSchemaInfo *pSchema= NEW(ProvSchemaInfo);

            findTablerefVisitor((Node *) stmt->query, pSchema);
            return pSchema->provAttrs;
        }
    }

    return NIL;
}

static boolean
findTablerefVisitor (Node *node, ProvSchemaInfo *status)
{
    if (node == NULL)
        return TRUE;

    if (isFromItem(node))
    {
        FromItem *f = (FromItem *) node;

        // if user specified provenance attribute, then use them
        if (f->provInfo)
        {
            char *tableName = f->name;

            // is base relation provenance
            if (f->provInfo->baserel)
            {
                FOREACH(char,attrName,f->attrNames)
                {
                    status->provAttrs = appendToTailOfList(status->provAttrs,
                            getProvenanceAttrName(tableName, attrName,
                                    getRelCount(status, tableName)));
                }

            }
            // user provided provenance attributes
            else
            {
                FOREACH(char,attrName,f->provInfo->userProvAttrs)
                {
                    status->provAttrs = appendToTailOfList(status->provAttrs,
                            getProvenanceAttrName(tableName, attrName,
                                    getRelCount(status, tableName)));
                }
            }
            return TRUE;
        }
        else if (isA(node, FromTableRef))
        {
            FromTableRef *r = (FromTableRef *) node;
            char *tableName = r->tableId;

            FOREACH(char,a,r->from.attrNames)
            status->provAttrs = appendToTailOfList(status->provAttrs,
                    getProvenanceAttrName(tableName,a,
                            getRelCount(status, tableName)));
        }
    }

    return visit(node, findTablerefVisitor, status);
}
