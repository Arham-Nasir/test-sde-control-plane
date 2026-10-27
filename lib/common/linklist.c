/*
 * Doubly Linked List Implementation
 */

#include "linklist.h"
#include "memory.h"

#include <stdlib.h>

struct list *list_new(void)
{
    struct list *list;

    list = XCALLOC(MTYPE_TMP, sizeof(struct list));
    return list;
}

static void list_free_internal(struct list *list, bool free_data)
{
    struct listnode *node, *next;

    if (list == NULL)
        return;

    for (node = list->head; node; node = next) {
        next = node->next;
        if (free_data && list->del && node->data)
            list->del(node->data);
        XFREE(MTYPE_TMP, node);
    }

    list->head = list->tail = NULL;
    list->count = 0;
}

void list_free(struct list *list)
{
    if (list == NULL)
        return;

    list_free_internal(list, true);
    XFREE(MTYPE_TMP, list);
}

void list_delete(struct list **plist)
{
    if (plist == NULL || *plist == NULL)
        return;

    list_free(*plist);
    *plist = NULL;
}

void list_delete_all_node(struct list *list)
{
    list_free_internal(list, true);
}

struct listnode *listnode_add(struct list *list, void *data)
{
    struct listnode *node;

    if (list == NULL)
        return NULL;

    node = XCALLOC(MTYPE_TMP, sizeof(struct listnode));
    node->data = data;
    node->prev = list->tail;

    if (list->tail)
        list->tail->next = node;
    else
        list->head = node;

    list->tail = node;
    list->count++;

    return node;
}

struct listnode *listnode_add_head(struct list *list, void *data)
{
    struct listnode *node;

    if (list == NULL)
        return NULL;

    node = XCALLOC(MTYPE_TMP, sizeof(struct listnode));
    node->data = data;
    node->next = list->head;

    if (list->head)
        list->head->prev = node;
    else
        list->tail = node;

    list->head = node;
    list->count++;

    return node;
}

struct listnode *listnode_add_sort(struct list *list, void *data)
{
    struct listnode *curr;

    if (list == NULL)
        return NULL;

    /* If no comparison function, add to tail */
    if (list->cmp == NULL)
        return listnode_add(list, data);

    /* Find insertion point */
    for (curr = list->head; curr; curr = curr->next) {
        if (list->cmp(data, curr->data) < 0)
            return listnode_add_before(list, curr, data);
    }

    /* Add to tail */
    return listnode_add(list, data);
}

struct listnode *listnode_add_after(struct list *list, struct listnode *after,
                                    void *data)
{
    struct listnode *node;

    if (list == NULL)
        return NULL;

    if (after == NULL)
        return listnode_add_head(list, data);

    node = XCALLOC(MTYPE_TMP, sizeof(struct listnode));
    node->data = data;
    node->prev = after;
    node->next = after->next;

    if (after->next)
        after->next->prev = node;
    else
        list->tail = node;

    after->next = node;
    list->count++;

    return node;
}

struct listnode *listnode_add_before(struct list *list, struct listnode *before,
                                     void *data)
{
    struct listnode *node;

    if (list == NULL)
        return NULL;

    if (before == NULL)
        return listnode_add(list, data);

    node = XCALLOC(MTYPE_TMP, sizeof(struct listnode));
    node->data = data;
    node->next = before;
    node->prev = before->prev;

    if (before->prev)
        before->prev->next = node;
    else
        list->head = node;

    before->prev = node;
    list->count++;

    return node;
}

void listnode_delete(struct list *list, struct listnode *node)
{
    if (list == NULL || node == NULL)
        return;

    if (node->prev)
        node->prev->next = node->next;
    else
        list->head = node->next;

    if (node->next)
        node->next->prev = node->prev;
    else
        list->tail = node->prev;

    list->count--;
    XFREE(MTYPE_TMP, node);
}

void *listnode_remove(struct list *list, void *data)
{
    struct listnode *node;

    node = listnode_lookup(list, data);
    if (node) {
        listnode_delete(list, node);
        return data;
    }
    return NULL;
}

struct listnode *listnode_lookup(struct list *list, void *data)
{
    struct listnode *node;

    if (list == NULL)
        return NULL;

    for_each_list_node(list, node) {
        if (node->data == data)
            return node;
    }

    return NULL;
}

bool listnode_exists(struct list *list, void *data)
{
    return listnode_lookup(list, data) != NULL;
}

void listnode_move_to_head(struct list *list, struct listnode *node)
{
    if (list == NULL || node == NULL)
        return;

    if (node == list->head)
        return;

    /* Remove from current position */
    if (node->prev)
        node->prev->next = node->next;
    if (node->next)
        node->next->prev = node->prev;
    else
        list->tail = node->prev;

    /* Insert at head */
    node->prev = NULL;
    node->next = list->head;
    if (list->head)
        list->head->prev = node;
    list->head = node;
}

void listnode_move_to_tail(struct list *list, struct listnode *node)
{
    if (list == NULL || node == NULL)
        return;

    if (node == list->tail)
        return;

    /* Remove from current position */
    if (node->prev)
        node->prev->next = node->next;
    else
        list->head = node->next;
    if (node->next)
        node->next->prev = node->prev;

    /* Insert at tail */
    node->next = NULL;
    node->prev = list->tail;
    if (list->tail)
        list->tail->next = node;
    list->tail = node;
}
