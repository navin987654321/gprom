/*************************************************************************
	> File Name: list.h
	> Author: Shukun Xie
    > Descriptions: Head file for the list.c
 ************************************************************************/

#ifndef LIST_H
#define LIST_H

#include "common.h"
#include "model/node/nodetype.h"

typedef struct ListCell
{
    union
    {
        void *ptr_value;
        int  int_value;
    } data;
    struct ListCell *next;
} ListCell;

typedef struct List
{
    NodeTag type;
    int     length;
    ListCell *head;
    ListCell *tail;
} List;

#define NIL ((List *)NULL)
#define LIST_LENGTH(l) ((l == NULL) ? 0 : (l)->length)

/*
 * Loop through list _list_ and access each element of type _type_ using name
 * _node_. _cell_ has to be an existing variable of type ListCell *.
 */
#define DUMMY_INT_FOR_COND(_name_) _name_##_stupid_int_
#define DUMMY_LC(_name_) _name_##_his_cell
#define INJECT_VAR(type,name) \
	for(int DUMMY_INT_FOR_COND(name) = 0; DUMMY_INT_FOR_COND(name) == 0;) \
		for(type name = NULL; DUMMY_INT_FOR_COND(name) == 0; DUMMY_INT_FOR_COND(name)++) \

#define FOREACH_LC(lc,list) \
	for(ListCell *lc = getHeadOfList(list); lc != NULL; lc = lc->next)

#define FOREACH(_type_,_node_,_list_) \
    INJECT_VAR(ListCell*,DUMMY_LC(_node_)) \
    for(_type_ *_node_ = (_type_ *)(((DUMMY_LC(_node_) = \
    		getHeadOfList(_list_)) != NULL) ? \
                    DUMMY_LC(_node_)->data.ptr_value : NULL); \
            DUMMY_LC(_node_) != NULL; \
           _node_ = (_type_ *)(((DUMMY_LC(_node_) = \
                    DUMMY_LC(_node_)->next) != NULL) ? \
                    DUMMY_LC(_node_)->data.ptr_value : NULL))

/*
 * Loop through integer list _list_ and access each element using name _ival_.
 * _cell_ has to be an existing variable of type ListCell *.
 */
#define FOREACH_INT(_ival_,_list_) \
    INJECT_VAR(ListCell*,DUMMY_LC(_ival_)) \
	for(int _ival_ = (((DUMMY_LC(_ival_) = getHeadOfList(_list_)) != NULL)  ? \
                        DUMMY_LC(_ival_)->data.int_value : -1); \
                DUMMY_LC(_ival_) != NULL; \
                _ival_ = (((DUMMY_LC(_ival_) = DUMMY_LC(_ival_)->next) != NULL) ? \
                        DUMMY_LC(_ival_)->data.int_value: -1))

/*
 * Loop through the cells of two lists simultaneously
 */
#define FORBOTH_LC(lc1,lc2,l1,l2) \
    for(ListCell *lc1 = getHeadOfList(l1), *lc2 = getHeadOfList(l2); lc1 != NULL && lc2 != NULL; \
            lc1 = lc1->next, lc2 = lc2->next)

/*
 * Loop through lists of elements with the same type simultaneously
 */
#define FORBOTH(_type_,_node1_,_node2_,_list1_,_list2_) \
	INJECT_VAR(ListCell*,DUMMY_LC(_node1_)) \
	INJECT_VAR(ListCell*,DUMMY_LC(_node2_)) \
    for(_type_ *_node1_ = (_type_ *)(((DUMMY_LC(_node1_) = \
    		getHeadOfList(_list1_)) != NULL) ? \
                    DUMMY_LC(_node1_)->data.ptr_value : NULL), \
        *_node2_ = (_type_ *)(((DUMMY_LC(_node2_) = \
        	getHeadOfList(_list1_)) != NULL) ? \
            		DUMMY_LC(_node2_)->data.ptr_value : NULL) \
        ; \
            DUMMY_LC(_node1_) != NULL && DUMMY_LC(_node2_) != NULL; \
           _node1_ = (_type_ *)(((DUMMY_LC(_node1_) = \
                    DUMMY_LC(_node1_)->next) != NULL) ? \
                    DUMMY_LC(_node1_)->data.ptr_value : NULL), \
           _node2_ = (_type_ *)(((DUMMY_LC(_node2_) = \
                    DUMMY_LC(_node2_)->next) != NULL) ? \
                    DUMMY_LC(_node2_)->data.ptr_value : NULL))

/*
 * Loop through two integer lists simultaneously
 */
#define FORBOTH_INT(_ival1_,_ival2_,_list1_,_list2_) \
    INJECT_VAR(ListCell*,DUMMY_LC(_ival1_)) \
    INJECT_VAR(ListCell*,DUMMY_LC(_ival2_)) \
    for(int _ival1_ = (((DUMMY_LC(_ival1_) = \
            (_list1_)->head) != NULL) ? \
                    DUMMY_LC(_ival1_)->data.int_value : -1), \
        _ival2_ = (((DUMMY_LC(_ival2_) = \
            (_list1_)->head) != NULL) ? \
                    DUMMY_LC(_ival2_)->data.int_value : -1) \
        ; \
            DUMMY_LC(_ival1_) != NULL && DUMMY_LC(_ival2_) != NULL; \
           _ival1_ = (((DUMMY_LC(_ival1_) = \
                    DUMMY_LC(_ival1_)->next) != NULL) ? \
                    DUMMY_LC(_ival1_)->data.int_value : -1), \
           _ival2_ = (((DUMMY_LC(_ival2_) = \
                    DUMMY_LC(_ival2_)->next) != NULL) ? \
                    DUMMY_LC(_ival2_)->data.int_value : -1))

/*
 * Get pointer of integer value of a list cell
 */
#define LC_P_VAL(lc) (((ListCell *) lc)->data.ptr_value)
#define LC_INT_VAL(lc) (((ListCell *) lc)->data.int_value)

extern boolean checkList(const List *list);

extern List *newList(NodeTag type);

extern List *singletonInt(int value);
extern List *singleton(void *value);
#define LIST_MAKE(...) listMake(__VA_ARGS__, NULL)
extern List *listMake(void *elem, ...);

extern int getListLength(List *list);

extern ListCell *getHeadOfList(List *list);
extern int getHeadOfListInt (List *list);
extern void *getHeadOfListP (List *list);

extern ListCell *getTailOfList(List *list);
extern int getTailOfListInt(List *list);

extern void *getNthOfListP(List *list, int n);
extern int getNthOfListInt(List *list, int n);
extern ListCell *getNthOfList(List *list, int n);

extern void newListTail(List *list);
extern List *appendToTailOfList(List *list, void *value);
extern List *appendToTailOfListInt(List *list, int value);

extern void newListHead(List *list);
extern List *appendToHeadOfList(List *list, void *value);
extern List *appendToHeadOfListInt (List *list, int value);

extern void reverseList(List *list);

extern void sortList(List *list);

extern List *copyList(List *list);

extern void freeList(List *list);
extern void deepFreeList(List *list);

extern boolean searchList(List *list, void *value);
extern boolean searchListInt(List *list, int value);

extern List *concatTwoLists (List *listA, List *listB);

#endif /* LIST_H */
