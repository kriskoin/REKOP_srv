//*********************************************************
//
//	Mike's generic binary tree class.
//
// 
//
//*********************************************************

#define DISP 0

#ifdef WIN32
  #define WIN32_LEAN_AND_MEAN		// Exclude rarely-used stuff from Windows headers
  #include <windows.h>			// Needed for CritSec stuff
#endif

#include <string.h>	// needed for memset()
#include <stdio.h>	// needed for sprintf()
#include "pplib.h"

//****************************************************************
// https://github.com/kriskoin//
// BinaryTree constructor/destructor
//
BinaryTree::BinaryTree(void)
{
	tree_root = NULL;
	PPInitializeCriticalSection(&BinaryTreeCritSec, CRITSECPRI_BINARY_TREE, "BinaryTree");
}

BinaryTree::~BinaryTree(void)
{
	EnterCriticalSection(&BinaryTreeCritSec);
	if (tree_root)  {
		Error(ERR_INTERNAL_ERROR, "%s(%d) Binary Tree getting destroyed while it still has nodes.",_FL);
		tree_root = NULL;
	}
	LeaveCriticalSection(&BinaryTreeCritSec);
	PPDeleteCriticalSection(&BinaryTreeCritSec);
	zstruct(BinaryTreeCritSec);
}

//****************************************************************
// https://github.com/kriskoin//
// BinaryTree constructor/destructor
//
// Add a node to a tree.  Node must already be initialized, including
// the object_ptr and sort_key.
//
void BinaryTree::AddNode(struct BinaryTreeNode *new_node)
{
	EnterCriticalSection(&BinaryTreeCritSec);
	AddNode(&tree_root, new_node);
	LeaveCriticalSection(&BinaryTreeCritSec);
}

void BinaryTree::AddNode(struct BinaryTreeNode **root, struct BinaryTreeNode *new_node)
{
	// Move down the left or right until we reach the end.  That's
	// where we put this new node.
	if (*root) {	// we're not at the end yet...
		pr(("%s(%d) Traversing tree to find insert location...\n", _FL));
		if ((*root)->sort_key > new_node->sort_key) {	// compare sort keys
			// put on left of root.
			AddNode(&((*root)->left), new_node);
		} else {
			// put on right of root.
			AddNode(&((*root)->right), new_node);
		}
	} else {
		// New node is at the bottom.
		*root = new_node;	// stick us in.
		if (new_node->left || new_node->right) {
			kp(("%s(%d) Warning: new node's left/right pointers weren't NULL ($%08lx/%08lx)\n", _FL, new_node->left, new_node->right));
		}
	}
}

//****************************************************************
// https://github.com/kriskoin//
// Remove an arbitrary node from a tree.
// Does not free/delete memory.
//
ErrorType BinaryTree::RemoveNode(WORD32 sort_key)
{
	EnterCriticalSection(&BinaryTreeCritSec);
	struct BinaryTreeNode **t = &tree_root;
	// Find the parent's ptr to the node we're looking for.
	while (t && *t && (*t)->sort_key != sort_key)  {
		if ((*t)->sort_key > sort_key) {	// go left?
			t = &(*t)->left;
		} else {							// go right.
			t = &(*t)->right;
		}
	}
	if (!t || !*t) {	// did we find it?
		LeaveCriticalSection(&BinaryTreeCritSec);
		return ERR_ERROR;	// could not find sort key.
	}

	ErrorType err = RemoveNode(t);
	LeaveCriticalSection(&BinaryTreeCritSec);
	return err;
}

ErrorType BinaryTree::RemoveNode(struct BinaryTreeNode **node_to_remove)
{
	struct BinaryTreeNode *us = *node_to_remove;
	struct BinaryTreeNode **parents_ptr = node_to_remove;
	EnterCriticalSection(&BinaryTreeCritSec);
	if (us->left==NULL && us->right==NULL) {
		// There's nothing below us.  This is the simplest case,
		// we can simply nuke the pointer to us and we're done.
		*parents_ptr = NULL;	// zero the pointer to us.
		zstruct(*us);			// always zero the memory we used.
	} else if (us->left && us->right) {
		// This is the most complex case... we have two subtrees below us.
		// We must graft one to the bottom of the other and then
		// put the new big sub-tree in our current place in the tree.
		struct BinaryTreeNode *left_tree = us->left;
		while (left_tree->right) {	// follow right side of left tree to bottom
			left_tree = left_tree->right;
		}
		left_tree->right = us->right;	// put our right tree there.
		*parents_ptr = us->left;
		zstruct(*us);
	} else if (us->left) {
		// Only the left exists.
		*parents_ptr = us->left;
		zstruct(*us);
	} else {
		// Only the right exists.
		*parents_ptr = us->right;
		zstruct(*us);
	}
	LeaveCriticalSection(&BinaryTreeCritSec);
	return ERR_NONE;
}
#if 0
treeNode *DeleteFromSubscriptionTable(treeNode *root, char *sym)
{
	treeNode *t1, *t2;
	if (!strcmpi(sym, root->symbol)) {              // found it -- delete root
		if (root->left == root->right) {        // empty tree
			strcpy(root->symbol, " ");              // blank it, only important for root of tree
			treeRoot = NULL;
			free(root);
			return NULL;
		} else if (root->left == NULL) {        // we've got a blank left subtree
			t1 = root->right;
			free(root);
			return t1;
		} else if (root->right == NULL) {       // we've got a blank right subtree
			t1 = root->left;
			free(root);
			return t1;
		} else {        // both have subtrees
			t1 = t2 = root->right;
			while (t1->left) t1 = t1->left;
			t1->left = root->left;
			free(root);
			return t2;
		}
	}
	if (strcmpi(root->symbol, sym) < 0) {   // keep looking for it
		root->right = DeleteFromSubscriptionTable(root->right, sym);
	} else {
		root->left = DeleteFromSubscriptionTable(root->left, sym);
	}
	return root;
}
#endif

//****************************************************************
// https://github.com/kriskoin//
// Search for a node in a tree using the sort_key.  This is very fast
// if the tree is balanced.  Returns NULL if not found.
//
struct BinaryTreeNode * BinaryTree::FindNode(WORD32 sort_key)
{
	EnterCriticalSection(&BinaryTreeCritSec);
	struct BinaryTreeNode *t = tree_root;
	while (t && t->sort_key != sort_key)  {
		if (t->sort_key > sort_key) {	// go left?
			t = t->left;
		} else {						// go right.
			t = t->right;
		}
	}
	LeaveCriticalSection(&BinaryTreeCritSec);
	return t;
}

//****************************************************************
// https://github.com/kriskoin//
// Rebalance a tree.  This can be slow, so try not to do it too often.
//
// For lack of a better name, I'll call this the Benna
// tree balancing algorithm.  I have no idea if someone else
// invented it first.
// note: this is a recursive function.
//
void BinaryTree::BalanceTree(void)
{
	EnterCriticalSection(&BinaryTreeCritSec);
	BalanceTree(&tree_root);
	LeaveCriticalSection(&BinaryTreeCritSec);
}
void BinaryTree::BalanceTree(struct BinaryTreeNode **tree)
{
	struct BinaryTreeNode **end, *t;

	if (*tree==NULL)  {
		return;	// no balancing to do.
	}

	int diff = CountNodes((*tree)->right) - CountNodes((*tree)->left);
	while (diff > 1) {                      // Shift the tree to the left by 1.
		// Find the leftmost node on the right side and place it
		// at the root.
		end = &(*tree)->right;  // start on right side
		while ((*end)->left)  {
			end = &(*end)->left;       // move down left.
		}
		// trim this node from old branch but leave lower branches
		// connected in its place.
		t = *end;
		*end = (*end)->right;
		// move to root and connect right side.
		t->right = (*tree)->right;
		t->left  = (*tree);
		(*tree)->right = NULL;  // disconnect old right side.
		*tree = t;
		diff = CountNodes((*tree)->right) - CountNodes((*tree)->left);
	}
	while (diff < -1) {     // Shift the tree to the right by 1
		// Find the rightmost node on the left side and place it
		// at the root.
		end = &(*tree)->left;   // start on left side
		while ((*end)->right) {
			end = &(*end)->right;     // move down right.
		}
		// trim this node from old branch but leave lower branches
		// connected in its place.
		t = *end;
		*end = (*end)->left;
		// move to root and connect left side.
		t->left  = (*tree)->left;
		t->right = (*tree);
		(*tree)->left = NULL;   // disconnect old left side.
		*tree = t;
		diff = CountNodes((*tree)->right) - CountNodes((*tree)->left);
	}
	// balance the two subtrees
	BalanceTree(&(*tree)->left);
	BalanceTree(&(*tree)->right);
}

//****************************************************************
// https://github.com/kriskoin//
// Count the number of nodes in a tree (including the root node).
//
int BinaryTree::CountNodes(void)
{
	EnterCriticalSection(&BinaryTreeCritSec);
	int result = CountNodes(tree_root);
	LeaveCriticalSection(&BinaryTreeCritSec);
	return result;
}

int BinaryTree::CountNodes(struct BinaryTreeNode *root)
{
	if (root==NULL)
		return 0;       // no nodes in this tree.
	return CountNodes(root->left) + CountNodes(root->right) + 1;
}

//****************************************************************
// https://github.com/kriskoin//
// Display a tree to debwin for debugging purposes only.
//
#define MAX_TREE_LEVELS (120/8-1)        // as much as we can fit in 120 columns

void BinaryTree::DisplayTree(void)
{
	EnterCriticalSection(&BinaryTreeCritSec);
	char bar_flags[MAX_TREE_LEVELS];        // flags for whether to draw a vertical bar
	DisplaySubTree(tree_root, 0, bar_flags);
	LeaveCriticalSection(&BinaryTreeCritSec);
}

//****************************************************************
// https://github.com/kriskoin//
// Display a subtree (internal to the DisplayTree function).
//
void BinaryTree::DisplaySubTree(struct BinaryTreeNode *tree, int level, char *bar_flags)
{
		if (level >= MAX_TREE_LEVELS) {
			kp(("***"));
			return; // can't display this deep.
		}
		if (tree) {
			if (tree->left || tree->right) {
				char str[30];
				sprintf(str, "%d", tree->sort_key);
				strcat(str, "-------");
				kp(("-%-5.5s-+", str));
				bar_flags[level] = TRUE;
				DisplaySubTree(tree->left, level+1, bar_flags);
				for (int i=0 ; i<level ; i++) {
					if (bar_flags[i])
						kp(("       |"));
					else
						kp(("        "));
				}
				kp(("       +"));
				bar_flags[level] = FALSE;
				DisplaySubTree(tree->right, level+1, bar_flags);
			} else {
				// We're the last node on this line.
				kp(("-%d\n", tree->sort_key));
			}
		} else {
			kp(("-|\n"));
		}
}
