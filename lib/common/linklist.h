/*
 * Doubly Linked List - FRR Style
 *
 * Generic doubly linked list implementation used throughout FRR for
 * maintaining ordered collections of routing information.
 *
 * Pattern Reference: lib/linklist.h in FRRouting
 */

#ifndef _COMMON_LINKLIST_H
#define _COMMON_LINKLIST_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
struct list;
struct listnode;

/* List node */
struct listnode {
    struct listnode *next;
    struct listnode *prev;
    void *data;
};

/* Comparison function for sorted lists */
typedef int (*list_cmp_func)(void *val1, void *val2);

/* Deletion callback for cleaning up node data */
typedef void (*list_del_func)(void *val);

/* List head */
struct list {
    struct listnode *head;
    struct listnode *tail;
    unsigned int count;
    list_cmp_func cmp;      /* Comparison function for sorted insert */
    list_del_func del;      /* Deletion callback */
};

/* Macros for iteration */
#define listhead(L) ((L) ? (L)->head : NULL)
#define listtail(L) ((L) ? (L)->tail : NULL)
#define listcount(L) ((L) ? (L)->count : 0)
#define listnextnode(N) ((N) ? (N)->next : NULL)
#define listprevnode(N) ((N) ? (N)->prev : NULL)
#define listgetdata(N) ((N) ? (N)->data : NULL)

/* Safe iteration - allows deletion during iteration */
#define for_each_list_node(list, node) \
    for ((node) = listhead(list); (node); (node) = listnextnode(node))

#define for_each_list_node_safe(list, node, next) \
    for ((node) = listhead(list), \
         (next) = (node) ? listnextnode(node) : NULL; \
         (node); \
         (node) = (next), \
         (next) = (node) ? listnextnode(node) : NULL)

/* Create a new list */
struct list *list_new(void);

/* Delete a list and optionally free all data */
void list_free(struct list *list);
void list_delete(struct list **plist);
void list_delete_all_node(struct list *list);

/* Add nodes */
struct listnode *listnode_add(struct list *list, void *data);
struct listnode *listnode_add_head(struct list *list, void *data);
struct listnode *listnode_add_sort(struct list *list, void *data);
struct listnode *listnode_add_after(struct list *list, struct listnode *after,
                                    void *data);
struct listnode *listnode_add_before(struct list *list, struct listnode *before,
                                     void *data);

/* Remove nodes */
void listnode_delete(struct list *list, struct listnode *node);
void *listnode_remove(struct list *list, void *data);

/* Lookup */
struct listnode *listnode_lookup(struct list *list, void *data);
bool listnode_exists(struct list *list, void *data);

/* Move operations */
void listnode_move_to_head(struct list *list, struct listnode *node);
void listnode_move_to_tail(struct list *list, struct listnode *node);

#ifdef __cplusplus
}
#endif

#endif /* _COMMON_LINKLIST_H */
