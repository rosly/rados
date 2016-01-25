/*
 * This file is a part of RadOs project
 * Copyright (c) 2013, Radoslaw Biernacki <radoslaw.biernacki@gmail.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1) Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2) Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3) No personal names or organizations' names associated with the 'RadOs'
 *    project may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
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

#ifndef __SLIST_H_
#define __SLIST_H_ 1

/* --- public structures --- */

/**
 * @file Implementation of single linked list
 */

/**
 * Definition of list element and list head struct
 *
 * It is used for both the list head and elements.
 * To form a list of some other structures, those structures will have to embed
 * this element as a field. Also some publicly accessible list header need to be
 * created.
 *
 * Empty list has next pointer set to NULL.
 * Tail of the list also has next element set to NULL.
 */
typedef struct _slist_t {
   struct _slist_t *next;
} slist_t;

/**
 * Definition of priority list element and priority list head
 *
 * Extension of single linked list where each element has associated priority.
 * Elements are added in the position of the list which preserve the ascending
 * order of element priorities.
 */
typedef struct _slistprio_t {
   slist_t list;
   unsigned short prio;
} slistprio_t;

/* --- private functions --- */

/**
 * Insert a new element between the two adjacent elements 'prev' and 'next'.
 * Internal function.
 */
static inline void __slist_put_in_between(
   slist_t *elem,
   slist_t *left,
   slist_t *right)
{
   elem->next = right;
   left->next = elem;
}

/* --- public functions --- */

/**
 * Initialize an empty list, or an unlinked element.
 * In case of single linked list, empty list next must be set to NULL
 */
static inline void slist_init(slist_t *l)
{
   l->next = NULL;
}

/**
 * Insert an element at the begin of the slist. O(1) operation.
 */
static inline void slist_prepend(
   slist_t *l,
   slist_t *elem)
{
   __slist_put_in_between(elem, l, l->next);
}

/**
 * Insert an element at the end of the slist. O(n) operation.
 * /warning Putting element at the end of sinlge linked list is very inefficient
 *          operation. Make sure that you do not this function in performance
 *          critical path of the code
 */
static inline void slist_append(
   slist_t *l,
   slist_t *elem)
{
   slist_t *curr;

   /* iterate to the end of the list */
   curr = l;
   while (curr->next)
      curr = curr->next;

   __slist_put_in_between(elem, curr, NULL);
}

/**
 * Insert an @param ele element after @param irt iterator. O(1) operation.
 */
static inline void slist_put_after(
   slist_t *itr,
   slist_t *ele)
{
   __slist_put_in_between(ele, itr, itr->next);
}

/**
 * Remove an @param elem element from its slist
 */
static inline void slist_unlink_next(slist_t *prev)
{
   slist_t *elem = prev->next;

   prev->next = elem->next;
   slist_init(elem);
}

/**
 * Check if slist is empty.
 *
 * @return true in case slist is empty
 *         false in case slist is not empty
 */
static inline bool slist_is_empty(const slist_t *l)
{
   return !(l->next); /* NULL means empty list */
}


/**
 * Returns iterator, it should be used in conjunction to slist_itr_end().
 *
 * @note Do not use in other cases, it is important to not mistake this function
 *       with slist_peekfirst  */
static inline slist_t *slist_itr_begin(const slist_t *l)
{
   return l->next;
}

/**
 * Returns true if iterator is at the end of the slist, false otherwise
 */
static inline bool slist_itr_end(const slist_t *itr)
{
   return !itr; /* NULL means end of list */
}

/**
 * Peek the first slist item. The returned element is not removed from the slist.
 */
static inline slist_t *slist_peekfirst(const slist_t *l)
{
   return l->next;
}

/**
 * Get the first slist item. The returned element is removed from slist.
 */
static inline slist_t *slist_detachfirst(slist_t *l)
{
   slist_t *elem = l->next;

   if (!elem)
      return NULL;  /* means slist is empty */
   l->next = elem->next;
   slist_init(elem);
   return elem;
}

/**
 * Adds the element to the priority slist in proper place which will sustain the
 * sorting order. In case of multiple elements with the same priority, it will
 * be added at the end of the block of those elements.
 *
 * @note important this is O(n) operation
 */
static inline void slistprio_append(
   slistprio_t *list,
   slistprio_t *elem)
{
   slistprio_t *left = list;
   slistprio_t *right = (slistprio_t*)(list->list.next);

   while ((right) && (elem->prio <= right->prio)) {
      left = right;
      right = (slistprio_t*)(right->list.next);
   }
   __slist_put_in_between(&(elem->list), &(left->list), &(right->list));
}

/**
 * Get the first element from priority slist. The returned element is removed
 * from slist.
 */
static inline slistprio_t *slistprio_detachfirst(slistprio_t *list)
{
   slist_t *elem = list->list.next;

   if (!elem)
      return NULL;  /* means slist is empty */
   list->list.next = elem->next;
   slist_init(elem);
   return (slistprio_t*)elem; /* slist_t slist is at begin of slistprio_t */
}

#endif /* __SLIST_H_ */

