%{
#include "common.h"
#include "mem_manager/mem_mgr.h"
#include "model/expression/expression.h"
#include "model/list/list.h"
#include "model/node/nodetype.h"
#include "model/datalog/datalog_model.h"
#include "model/rpq/rpq_model.h"
#include "model/query_block/query_block.h"
#include "parser/parse_internal_dl.h"
#include "log/logger.h"
#include "model/query_operator/operator_property.h"

#define RULELOG(grule) \
    { \
        TRACE_LOG("Parsing grammer rule <%s>", #grule); \
    }
    
#undef free
#undef malloc

Node *dlParseResult = NULL;
%}

%name-prefix "dl"

%union {
    /* 
     * Declare some C structure those will be used as data type
     * for various tokens used in grammar rules.
     */
     Node *node;
     List *list;
     char *stringVal;
     int intVal;
     double floatVal;
}

/*
 * Declare tokens for name and literal values
 * Declare tokens for user variables
 */
%token <stringVal> IDENT
%token <stringVal> VARIDENT
%token <stringVal> FORMAT

/* 
 * Functions and operators 
 */ 
%token <stringVal> AMMSC
%token <stringVal> '+' '-' '*' '/' '%' '^' '&' '|' '!' ')' '(' ':'
%token <stringVal> STRCONCAT 

/*
 * Tokens for in-built keywords
 *        Currently keywords related to basic query are considered.
 *        Later on other keywords will be added.
 */
%token <stringVal> NEGATION RULE_IMPLICATION ANS WHYPROV WHYNOTPROV GP RPQ USERDOMAIN OF IS 
%token <stringVal> SCORE AS THRESHOLDS TOP FOR FAILURE SUMMARIZED BY WITH SAMPLE PRECISION RECALL INFORMATIVENESS

/* tokens for constant and idents */
%token <intVal> intConst
%token <floatVal> floatConst
%token <stringVal> stringConst

/* comparison and arithmetic operators */
%token <stringVal> comparisonOp

/*
 * Declare token for operators specify their associativity and precedence
 *
%left NEGATION

/* Arithmetic operators : FOR TESTING */
%left '+' '-'
%left '*' '/' '%'
%left STRCONCAT
%nonassoc '(' ')'

/*
 * Types of non-terminal symbols
 */
/* statements and their parts */ 
%type <stringVal> name
%type <list> stmtList
%type <node> statement program

%type <node> rule fact rulehead headatom relAtom bodyAtom arg comparison ansrelation provStatement rpqStatement associateDomain
%type <node> variable constant expression functionCall binaryOperatorExpression optionalTopK optionalSumSample optionalSumType
%type <node> optionalFPattern 
%type <list> bodyAtomList argList exprList rulebody summarizationStatement intConstList optionalScore optionalThresholds
%type <stringVal> optProvFormat 

/* start symbol */
%start program

/*************************************************************/
/* RULE SECTION												 */
/*************************************************************/
%%

program:
		stmtList summarizationStatement
			{ 
				RULELOG("program::stmtList");
				$$ = (Node *) createDLProgram ($1, NULL, NULL, NULL, NULL, $2);
				dlParseResult = (Node *) $$;
				DEBUG_LOG("parsed %s", nodeToString($$));
			}
		;

/* Rule for all types of statements */
stmtList: 
		statement
			{ 
				RULELOG("stmtList::statement"); 
				$$ = singleton($1);
			}
		| stmtList statement 
			{
				RULELOG("stmtList::stmtList::statement");
				$$ = appendToTailOfList($1, $2); 
			}
	;

/*
 * Statements can be:
 *
 * 	- rules, e.g., Q(X) :- R(X,Y); DQ(X) :- R(X,Y); DQ(X) :- R(Y,X);
 *  - facts, e.g., R(1,2);
 * 	- answer relation declarations, e.g., ANS : Q;
 * 	- associated domain declarations, e.g., USERDOMAIN OF rel.attr IS DQ;
 * 	- provenance requests, e.g., WHY(Q(1));
 *	- summarization, e.g., TOP k SUMMARIZED BY type WITH SAMPLE(p);
 *  - RPQ requests, e.g., RPQ('a*.b', typeOfResult, edge, result)
 */
statement:
		rule { RULELOG("statement::rule"); $$ = $1; }
		| fact { RULELOG("statement::fact"); $$ = $1; }
		| ansrelation { RULELOG("statement::ansrelation"); $$ = $1; }
		| associateDomain { RULELOG("statement::associateDomain"); $$ = $1; }		
		| provStatement { RULELOG("statement::prov"); $$ = $1; }
		| rpqStatement { RULELOG("statement::rpq"); $$ = $1; }
	;

rpqStatement:
		RPQ '(' stringConst ',' IDENT ',' IDENT ',' IDENT ')' '.'
		{
			RULELOG("rpqStatement");
			$$ = (Node *) makeRPQQuery($3, $5, $7, $9);
		}
	;
	
provStatement:
		WHYPROV '(' relAtom ')' optProvFormat '.' 
		{
			RULELOG("provStatement::WHY");
			char *str = $5 ? CONCAT_STRINGS("WHY_PROV-", $5) : "WHY_PROV";
			$$ = (Node *) createNodeKeyValue((Node *) createConstString(str), (Node *) $3);
		}
		| WHYNOTPROV '(' relAtom ')' optProvFormat '.'
		{
			RULELOG("provStatement::WHYNOT");
			char *str = $5 ? CONCAT_STRINGS("WHYNOT_PROV-", $5) : "WHYNOT_PROV";
			$$ = (Node *) createNodeKeyValue((Node *) createConstString(str), (Node *) $3);
		}
		| GP optProvFormat '.'
		{
			RULELOG("provStatement::GP");
			char *str = $2 ? CONCAT_STRINGS("FULL_GP_PROV-", $2) : "GP";
			$$ = (Node *) createNodeKeyValue((Node *) createConstString(str), NULL);
		}
	;

/* optProv:
		optProvFormat optProvSummarize
	;

optProvSummarize:
		/* EMPTY  { $$ = NULL; }
		| SUMMARIZE name { $$ = $2; }
	;
*/

	
summarizationStatement:
		/* EMPTY */ { $$ = NIL; }
		| optionalTopK optionalSumType optionalSumSample { $$ = LIST_MAKE($1,$2,$3); }
		| optionalTopK optionalFPattern optionalSumType optionalSumSample { $$ = LIST_MAKE($1,$2,$3,$4); }
		| optionalScore optionalThresholds optionalTopK optionalSumType optionalSumSample
		{ 
			$$ = CONCAT_LISTS($1,$2,LIST_MAKE($3,$4,$5)); 
		}
		| optionalScore optionalThresholds optionalTopK optionalFPattern optionalSumType optionalSumSample 
		{ 
			$$ = CONCAT_LISTS($1,$2,LIST_MAKE($3,$4,$5,$6)); 
		}
/*
		| FOR TOP intConst SUMMARIZED BY name WITH SAMPLE '(' intConst ')' '.'
		{
			RULELOG("summarizationStatement::sumOpts");
			Node *topk = (Node *) createNodeKeyValue((Node *) createConstString("topk"),(Node *) createConstInt($3));
			Node *type = (Node *) createStringKeyValue(strdup("sumtype"),strdup($6));
			Node *samp = (Node *) createNodeKeyValue((Node *) createConstString("sumsamp"),(Node *) createConstInt($10));
			$$ = LIST_MAKE(topk, type, samp);
		}
*/		
	;	


optionalScore:
		SCORE AS '(' floatConst '*' RECALL '+' floatConst '*' INFORMATIVENESS ')' 
		{ 
			RULELOG("optionalScore::score");
			Node *recall = (Node *) createNodeKeyValue((Node *) createConstString("srecall"),(Node *) createConstFloat($4));
			Node *info = (Node *) createNodeKeyValue((Node *) createConstString("sinformativeness"),(Node *) createConstFloat($8));
			$$ = LIST_MAKE(recall,info);
		}
		| SCORE AS '(' floatConst '*' PRECISION '+' floatConst '*' RECALL ')' 
		{ 
			RULELOG("optionalScore::score");
			Node *precision = (Node *) createNodeKeyValue((Node *) createConstString("sprecision"),(Node *) createConstFloat($4));
			Node *recall = (Node *) createNodeKeyValue((Node *) createConstString("srecall"),(Node *) createConstFloat($8));
			$$ = LIST_MAKE(precision,recall);
		}
		| SCORE AS '(' floatConst '*' PRECISION '+' floatConst '*' INFORMATIVENESS ')' 
		{ 
			RULELOG("optionalScore::score");
			Node *precision = (Node *) createNodeKeyValue((Node *) createConstString("sprecision"),(Node *) createConstFloat($4));
			Node *info = (Node *) createNodeKeyValue((Node *) createConstString("sinformativeness"),(Node *) createConstFloat($8));
			$$ = LIST_MAKE(precision,info);
		}
		| SCORE AS '(' floatConst '*' PRECISION '+' floatConst '*' RECALL '+' floatConst '*' INFORMATIVENESS ')' 
		{ 
			RULELOG("optionalScore::score");
			Node *precision = (Node *) createNodeKeyValue((Node *) createConstString("sprecision"),(Node *) createConstFloat($4));
			Node *recall = (Node *) createNodeKeyValue((Node *) createConstString("srecall"),(Node *) createConstFloat($8));
			Node *info = (Node *) createNodeKeyValue((Node *) createConstString("sinformativeness"),(Node *) createConstFloat($12));
			$$ = LIST_MAKE(precision,recall,info);
		}
    ;


optionalThresholds:
		THRESHOLDS '(' RECALL ':' floatConst ',' INFORMATIVENESS ':' floatConst ')' 
		{ 
			RULELOG("optionalScore::score");
			Node *recall = (Node *) createNodeKeyValue((Node *) createConstString("trecall"),(Node *) createConstFloat($5));
			Node *info = (Node *) createNodeKeyValue((Node *) createConstString("tinformativeness"),(Node *) createConstFloat($9));
			$$ = LIST_MAKE(recall,info);
		}
		| THRESHOLDS '(' PRECISION '=' floatConst ',' RECALL '=' floatConst ')' 
		{ 
			RULELOG("optionalScore::score");
			Node *precision = (Node *) createNodeKeyValue((Node *) createConstString("tprecision"),(Node *) createConstFloat($5));
			Node *recall = (Node *) createNodeKeyValue((Node *) createConstString("trecall"),(Node *) createConstFloat($9));
			$$ = LIST_MAKE(precision,recall);
		}
		| THRESHOLDS '(' PRECISION '=' floatConst ',' INFORMATIVENESS '=' floatConst ')' 
		{ 
			RULELOG("optionalScore::score");
			Node *precision = (Node *) createNodeKeyValue((Node *) createConstString("tprecision"),(Node *) createConstFloat($5));
			Node *info = (Node *) createNodeKeyValue((Node *) createConstString("tinformativeness"),(Node *) createConstFloat($9));
			$$ = LIST_MAKE(precision,info);
		}
		| THRESHOLDS '(' PRECISION '=' floatConst ',' RECALL '=' floatConst ',' INFORMATIVENESS '=' floatConst ')'  
		{ 
			RULELOG("optionalScore::score");
			Node *precision = (Node *) createNodeKeyValue((Node *) createConstString("tprecision"),(Node *) createConstFloat($5));
			Node *recall = (Node *) createNodeKeyValue((Node *) createConstString("trecall"),(Node *) createConstFloat($9));
			Node *info = (Node *) createNodeKeyValue((Node *) createConstString("tinformativeness"),(Node *) createConstFloat($13));
			$$ = LIST_MAKE(precision,recall,info);
		}
    ;


optionalTopK:
		TOP intConst 
		{ 
			RULELOG("optionalTopK::topk");
			$$ = (Node *) createNodeKeyValue((Node *) createConstString("topk"),(Node *) createConstInt($2));
		}
    ;


optionalFPattern:
		FOR FAILURE OF '(' intConstList ')' 
		{ 
			RULELOG("optionalFPattern::intConstList");
			$$ = (Node *) createNodeKeyValue((Node *) createConstString("fpattern"),(Node *) $5);
		}
    ;
    
    
intConstList:
		intConst
		{
			RULELOG("intConst");
			$$ = singleton(createConstInt($1));
		}
		| intConstList ',' intConst
		{
			RULELOG("intConstList::intConst");
			$$ = appendToTailOfList($1,createConstInt($3));
		}
	;  
    

optionalSumType:
		SUMMARIZED BY name 
		{ 
			RULELOG("optionalSumType::sumType");
			$$ = (Node *) createStringKeyValue(strdup("sumtype"),strdup($3));
		} 
	;
	
	
optionalSumSample:
		WITH SAMPLE '(' intConst ')' '.'
		{ 
			RULELOG("optionalSumSample::sumSamp");
			$$ = (Node *) createNodeKeyValue((Node *) createConstString("sumsamp"),(Node *) createConstInt($4));	
		}
 	;
 	

optProvFormat:
		/* EMPTY */ { $$ = NULL; }
		| FORMAT name { $$ = $2; }
	;	
	
rule:
		rulehead RULE_IMPLICATION rulebody '.' 
		{ 
			RULELOG("rule::head::body"); 
			$$ = (Node *) createDLRule((DLAtom *) $1,$3); 
		}
	;
	
fact:
		rulehead '.' { RULELOG("fact::rulehead"); $$ = $1; }
	;

ansrelation:
		ANS ':' name '.'
		{
			RULELOG("ansrelation");
			$$ = (Node *) createConstString($3);
		}
	;

/*
domainSet:
 		domainList 
 			{ 
 				RULELOG("domainSet::domainList"); 
 				$$ = (Node *) $1; 
 			}		
	;
			
domainList:
		associateDomain		
			{
				RULELOG("domainList::associateDomain");
				$$ = singleton($1); 
			}   	
 		| domainList ',' associateDomain 
			{
				RULELOG("domainList::associateDomain");
				$$ = appendToTailOfList($1,$3); 
			}
	;
*/
		
associateDomain:
		USERDOMAIN OF name '.' name IS name '.'
		{
			RULELOG("associateDomain");
			$$ = (Node *) createDLDomain($3,$5,$7);
		}
	;

rulehead:
		headatom  { RULELOG("rulehead::atom"); $$ = $1; }
	;
	
rulebody:
		bodyAtomList { RULELOG("rulebody::atomList"); $$ = $1; }
	;
	
/* we allow for expressions in the head atom, e.g., Q(X + 1) :- R(X,Y); */
headatom:
		name '(' exprList ')'
			{
				RULELOG("");
				$$ = (Node *) createDLAtom($1, $3, FALSE);
			}
	;
	
bodyAtomList:
		bodyAtomList ',' bodyAtom
			{
				RULELOG("atomList::bodyAtom");
				$$ = appendToTailOfList($1,$3); 
			}
		| bodyAtom
			{
				RULELOG("atomList::atom");
				$$ = singleton($1); 
			}
	;

bodyAtom:
 		relAtom { RULELOG("bodyAtom::relAtom"); $$ = $1; }
		| comparison
			{
				RULELOG("atom::comparison");
				$$ = (Node *) $1;
			}
 	;

relAtom:
 		NEGATION name '(' argList ')' 
 			{ 
 				RULELOG("relAtom::negative");
 				$$ = (Node *) createDLAtom($2, $4, TRUE); 
			}
 		| name '(' argList ')' 
 			{
 				RULELOG("relAtom::positive");
 				$$ = (Node *) createDLAtom($1, $3, FALSE); 
			}		
	;
	
/*
constAtom:
		IDENT '(' constList ')' 
			{
				RULELOG("constAtom");
				$$ = createDLAtom($1,$3,FALSE); 
			}
	;
*/

argList:
 		argList ',' arg 
 			{
 				RULELOG("argList::argList::arg");
 				$$ = appendToTailOfList($1,$3); 
			}
 		| arg		
 			{
 				RULELOG("argList::arg");
 				$$ = singleton($1); 
			}
 	;

/* 	
constList:
		constList ',' constant { $$ = appendToTailOfList($1,$2); }
		| constant { $$ = singleton($1); }
	;
*/

comparison:
		arg comparisonOp arg
			{
				RULELOG("comparison::arg::op::arg");
				$$ = (Node *) createDLComparison($2, $1, $3);
			}
	;

/* args */ 	
arg:
 		variable 
 			{
 				RULELOG("arg:variable");
 		 		$$ = $1; 
	 		}
 		| constant 
 			{ 
 				RULELOG("arg:constant");
 				$$ = $1; 
			}
	;

/*
 * Rule to parse an expression list
 */
exprList: 
        expression        { RULELOG("exprList::SINGLETON"); $$ = singleton($1); }
        | exprList ',' expression
             {
                  RULELOG("exprList::exprList::expression");
                  $$ = appendToTailOfList($1, $3);
             }
    ;

	
/*
 * Rule to parse expressions used in various lists
 */
expression:
		'(' expression ')'				{ RULELOG("expression::bracked"); $$ = $2; } 
		| constant     				   	{ RULELOG("expression::constant"); }
        | variable 	        		  	{ RULELOG("expression::variable"); }
        | binaryOperatorExpression		{ RULELOG("expression::binaryOperatorExpression"); } 
        | functionCall	        		{ RULELOG("expression::functionCall"); }
    ;

/*
 * Parse operator expression
 */
 
binaryOperatorExpression: 

    /* Arithmatic Operations */
        expression '+' expression
            {
                RULELOG("binaryOperatorExpression:: '+' ");
                List *expr = singleton($1);
                expr = appendToTailOfList(expr, $3);
                $$ = (Node *) createOpExpr($2, expr);
            }
        | expression '-' expression
            {
                RULELOG("binaryOperatorExpression:: '-' ");
                List *expr = singleton($1);
                expr = appendToTailOfList(expr, $3);
                $$ = (Node *) createOpExpr($2, expr);
            }
        | expression '*' expression
            {
                RULELOG("binaryOperatorExpression:: '*' ");
                List *expr = singleton($1);
                expr = appendToTailOfList(expr, $3);
                $$ = (Node *) createOpExpr($2, expr);
            }
        | expression '/' expression
            {
                RULELOG("binaryOperatorExpression:: '/' ");
                List *expr = singleton($1);
                expr = appendToTailOfList(expr, $3);
                $$ = (Node *) createOpExpr($2, expr);
            }
        | expression '%' expression
            {
                RULELOG("binaryOperatorExpression:: '%' ");
                List *expr = singleton($1);
                expr = appendToTailOfList(expr, $3);
                $$ = (Node *) createOpExpr($2, expr);
            }
    /* String Operators */
        | expression STRCONCAT expression
            {
                RULELOG("binaryOperatorExpression::comparisonOps");
                List *expr = singleton($1);
                expr = appendToTailOfList(expr, $3);
                $$ = (Node *) createOpExpr("||", expr);
            }
    ;


/*
 * Rule to parse function calls (provision for aggregation)
 */
functionCall: 
        name '(' exprList ')'          
            {
                RULELOG("functionCall::IDENTIFIER::exprList");
				FunctionCall *f = createFunctionCall($1, $3);
				f->isAgg = TRUE;
				$$ = (Node *) f;
            }
		| AMMSC '(' exprList ')'          
            {
                RULELOG("functionCall::AMMSC::exprList");
				FunctionCall *f = createFunctionCall($1, $3);
				f->isAgg = TRUE;
				$$ = (Node *) f;
            }
    ;

	
variable:
		IDENT 
			{
				RULELOG("variable"); 
				$$ = (Node *) createDLVar($1, DT_STRING); 
			}
	;
	
constant: 
		intConst 
			{
				RULELOG("constant::intConst"); 
				$$ = (Node *) createConstInt($1); 
			}
		| floatConst
			{
				RULELOG("constant::floatConst"); 
				$$ = (Node *) createConstFloat($1); 
			}
		| stringConst 
			{
				RULELOG("constant::stringConst");
				$$ = (Node *) createConstString($1); 
			}
	;
	
name:
		IDENT { RULELOG("name::IDENT"); $$ = $1; }
		| VARIDENT { RULELOG("name::VARIDENT"); $$ = $1; }
	;	