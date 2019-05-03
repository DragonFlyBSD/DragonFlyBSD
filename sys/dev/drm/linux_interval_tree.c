/*
 * Copyright (c) 2019 Matthew Dillon <dillon@backplane.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <linux/init.h>
#include <linux/interval_tree.h>
#include <linux/module.h>

typedef struct interval_tree_node itnode_t;

void
interval_tree_insert(struct interval_tree_node *node, struct rb_root *root)
{
	itnode_t *scan;

	scan = (itnode_t *)root->rb_node;
	if (scan) {
		node->next = scan->next;
		node->atroot = 0;
		scan->next = node;
	} else {
		root->rb_node = (void *)node;
		node->atroot = 1;
		node->next = node;
	}
}

void
interval_tree_remove(struct interval_tree_node *node, struct rb_root *root)
{
	itnode_t *scan;

	scan = (itnode_t *)root->rb_node;
	KKASSERT(scan != NULL);
	while (scan->next != node) {
		scan = scan->next;
	}
	scan->next = node->next;
	if (scan == node) {
		/*
		 * Last element is being removed
		 */
		root->rb_node = NULL;
		node->atroot = 0;
	} else if ((itnode_t *)root->rb_node == node) {
		/*
		 * Root pointer is the node being removed, move the root
		 * pointer.
		 */
		node->atroot = 0;
		scan->atroot = 1;
		root->rb_node = (void *)scan;
	}
}

struct interval_tree_node *
interval_tree_iter_first(struct rb_root *root,
                         unsigned long start, unsigned long last)
{
	itnode_t *scan;

	scan = (itnode_t *)root->rb_node;
	if (scan) {
		do {
			if (start <= scan->last &&
			    last >= scan->start) {
				return scan;
			}
			scan = scan->next;
		} while (scan->atroot == 0);
		scan = NULL;
	}
	return scan;
}

struct interval_tree_node *
interval_tree_iter_next(struct interval_tree_node *node,
			unsigned long start, unsigned long last)
{
	itnode_t *scan;

	scan = node->next;
	while (scan->atroot == 0) {
		if (start <= scan->last &&
		    last >= scan->start) {
				return scan;
		}
		scan = scan->next;
	}
	return NULL;
}
