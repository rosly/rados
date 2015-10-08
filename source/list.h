/*
 * This file is a part of RadOs project
 * Copyright (c) 2013, Radoslaw Biernacki <radoslaw.biernacki@gmail.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1) Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2) Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3) No personal names or organizations' names associated with the 'RadOs' project
 *    may be used to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE RADOS PROJECT AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __LIST_H_
#define __LIST_H_ 1

/* --- public structures --- */

/**
 * @file Implementation of doubly linked list
 * Some ideas coming from Linux's source code.
 */

/**
 * Definition of list element and list head struct
 *
 * It is used for both the list head and elements.
 * To form a list of some other structures, those structures will have to embed
 * this element as a field. Also some publicly accessible list header need to be
 * created.
 *
 * Empty list does not contain elements and its next and prev pointer are set the
 * head of the list (the same element).
 * List with one element will contain list head linked with contained element
 * (two elements at total)
 */
typedef struct _list_t {
   struct _list_t *next;
   struct _list_t *prev;
} list_t;

/**
 * Definition of priority list element and priority list head
 *
 * Extension of doubly linked list where each element has associated priority.
 * Elements are added in the position of the list which preserve the ascending
 * order of element priorities.
 */
typedef struct _listprio_t {
   list_t list;
   unsigned short prio;
} listprio_t;

/* --- private functions --- */

/**
 * Insert a new element between the two adjacent elements 'prev' and 'next'.
 * Internal function.
 */
static inline void __list_put_in_between(
   list_t *elem, list_t *left, list_t *right)
{
   right->prev = elem;
   elem->next = right;
   elem->prev = left;
   left->next = elem;
}

/**
 * Connect two elements together, thus removing all elements in between.
 * Internal function.
 */
static inline void __list_connect_together(list_t *left, list_t *right)
{
   right->prev = left;
   left->next = right;
}

/* --- public functions --- */

/**
 * Initialize an empty list, or an unlinked element.
 * Both pointers are linked to the list header itself.
 */
static inline void list_init(list_t *l)
{
   l->next = l;
   l->prev = l;
}

/**
 * Insert an element at the begin of the list. O(1) operation.
 */
static inline void list_prepend(list_t *l, list_t *elem)
{
   __list_put_in_between(elem, l, l->next);
}

/**
 * Insert an element at the end of the list. O(1) operation/
 */
static inline void list_append(list_t *l, list_t *elem)
{
   __list_put_in_between(elem, l->prev, l);
}

/**
 * Insert an @param ele element after @param irt iterator. O(1) operation.
 */
static inline void list_put_after(list_t *itr, list_t *ele)
{
   __list_put_in_between(ele, itr, itr->next);
}

/**
 * Insert an @param ele element before @param irt iterator. O(1) operation.
 */
static inline void list_put_before(list_t *itr, list_t *ele)
{
   __list_put_in_between(ele, itr->prev, itr);
}

/**
 * Remove an @param elem element from its list
 */
static inline void list_unlink(list_t *elem)
{
   __list_connect_together(elem->prev, elem->next);
   list_init(elem);
}

/**
 * Check if list is empty.
 *
 * @return true in case list is empty
 *         false in case list is not empty
 */
static inline bool list_is_empty(const list_t *l)
{
   return (l->next == l) ? true : false;
}


/**
 * Returns iterator, it should be used in conjunction to list_itr_end().
 *
 * @note Do not use in other cases, it is important to not mistake this function
 *       with list_peekfirst  */
static inline list_t *list_itr_begin(const list_t *l)
{
   return l->next;
}

/**
 * Returns true if iterator is at the end of the list, false otherwise
 */
static inline bool list_itr_end(const list_t *l, const list_t *itr)
{
   return l == itr;
}

/**
 * Peek the first list item. The returned element is not removed from the list.
 */
static inline list_t *list_peekfirst(const list_t *l)
{
   if (l->next == l)
   {
      return NULL;
   } else {
      return l->next;
   }
}

/*
 * Get the first list item. The returned element is removed from list.
 */
static inline list_t *list_detachfirst(const list_t *l)
{
   list_t *elem = l->next;
   if (elem == l)
   {
      return NULL; /* means list is empty */
   }
   __list_connect_together(elem->prev, elem->next);
   list_init(elem);
   return elem;
}

/**
 * Peek the last element from list. The returned element is not removed from
 * list.
 */
static inline list_t *list_peeklast(const list_t *l)
{
   if (l->prev == l)
   {
      return NULL;
   } else {
      return l->prev;
   }
}

/**
 * Adds the element to the priority list in proper place which will sustain the
 * sorting order. In case of multiple elements with the same priority, it will be
 * added at the end of the block of those elements.
 *
 * @note important this is O(n) operation
 */
static inline void listprio_append(listprio_t *h, listprio_t *elem)
{
   listprio_t *l = h;
   listprio_t *r = (listprio_t*)(l->list.next);
   while((r != h) && (elem->prio <= r->prio))
   {
      l = r;
      r = (listprio_t*)(r->list.next);
   }
   __list_put_in_between(&(elem->list), &(l->list), &(r->list));
}

/**
 * Get the first element from priority list. The returned element is removed
 * from list.
 */
static inline listprio_t *listprio_detachfirst(const listprio_t *l)
{
   list_t *elem = l->list.next;
   if (elem == (list_t*)l)
   {
      return NULL; /* means list is empty */
   }
   __list_connect_together(elem->prev, elem->next);
   list_init(elem);
   return (listprio_t*)elem; /* list_t list is at begin of listprio_t */
}

#endif /* __LIST_H_ */

