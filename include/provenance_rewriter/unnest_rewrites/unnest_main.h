/*
 * unnest_main.h
 *
 *  Created on: Nov. 2, 2019
 *      Author: Xing
 */

#include "model/query_operator/query_operator.h"

extern Node *unnestTranslateQBModel (Node *qbModel);
extern QueryOperator *unnestRewriteQuery(QueryOperator *input);


