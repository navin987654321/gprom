/*-----------------------------------------------------------------------------
 *
 * rewriter.h
 *		
 *
 *		AUTHOR: lord_pretzel
 *
 *-----------------------------------------------------------------------------
 */

#ifndef REWRITER_H_
#define REWRITER_H_

#include "common.h"

extern char *rewriteQuery(char *input);
extern char *rewriteQueryFromStream (FILE *stream);
extern char *rewriteQueryWithOptimization(char *input);

#endif /* REWRITER_H_ */
