// @file bst.c
//
// @author Daniil Didenko (xdidend00)

#include "bst.h"
#include "utils.h"

typedef struct bst_node_s
{
    int key; // in case of identifier it will be it's index in identifier list

    void *data;
    struct bst_node_s *left_child;
    struct bst_node_s *right_child;

    int height;
    int balance;
} bst_node;

struct bst_table_s
{
    bst_node *root;
};

// utilities
void free_node(bst_node *node)
{
    if (node->data != NULL)
    {
        UTILS_SAFE_FREE(&node->data);
    }
    UTILS_SAFE_FREE(&node);
}

void free_node_with_children(bst_node *node)
{
    if (node->left_child != NULL)
    {
        free_node_with_children(node->left_child);
    }
    if (node->right_child != NULL)
    {
        free_node_with_children(node->right_child);
    }
    free_node(node);
}

bst_node *create_node(error *err, int key, void *data)
{
    *err = NONE;
    bst_node *node = (bst_node *)utils_safe_malloc(err, sizeof(bst_node));
    if (*err != NONE)
    {
        return NULL;
    }

    node->left_child = NULL;
    node->right_child = NULL;
    node->balance = 0;
    node->height = 1;

    node->data = data;
    node->key = key;

    return node;
}

void update_height_and_balance(bst_node *node)
{
    int height = 1;

    int left_height = (node->left_child == NULL) ? 0 : node->left_child->height;
    int right_height = (node->right_child == NULL) ? 0 : node->right_child->height;

    int balance = left_height - right_height;

    if (left_height > right_height)
    {
        height += left_height;
    }
    else
    {
        height += right_height;
    }

    node->height = height;
    node->balance = balance;
}

// bst_node create_node_copy(bst_node* node)
//{
//	bst_node node_copy = { node->key, node->data, node->left_child, node->right_child, 1, 0 };
//	return node_copy;
// }
//
// void copy_node_contents(bst_node* destination, bst_node* source)
//{
//	destination->key = source->key;
//	destination->data = source->data;
//	destination->right_child = source->right_child;
//	destination->left_child = source->left_child;
// }

bst_node *rotate_right(bst_node *root)
{
    bst_node *tmp_node = root->left_child;
    root->left_child = tmp_node->right_child;
    tmp_node->right_child = root;

    update_height_and_balance(root);
    update_height_and_balance(tmp_node);
    return tmp_node;
}

bst_node *rotate_left(bst_node *root)
{
    bst_node *tmp_node = root->right_child;
    root->right_child = tmp_node->left_child;
    tmp_node->left_child = root;

    update_height_and_balance(root);
    update_height_and_balance(tmp_node);
    return tmp_node;
}

void check_and_restore_balance(bst_node **node)
{
    if (*node == NULL)
    {
        return;
    }
    update_height_and_balance(*node);

    int balance = (*node)->balance;
    if (balance > 1)
    {
        if ((*node)->left_child->balance < 0)
        {
            (*node)->left_child = rotate_left((*node)->left_child);
        }
        *node = rotate_right(*node);
    }
    else if (balance < -1)
    {
        if ((*node)->right_child->balance > 0)
        {
            (*node)->right_child = rotate_right((*node)->right_child);
        }
        *node = rotate_left(*node);
    }
}

void insert_node(error *err, bst_node **root, bst_node *node_to_insert)
{
    *err = NONE;
    if ((*root)->key == node_to_insert->key)
    {
        *err = INTERNAL_ITEM_ALREADY_EXISTS;
        free_node(node_to_insert);
        return;
    }
    else if ((*root)->key > node_to_insert->key)
    {
        if ((*root)->left_child == NULL)
        {
            (*root)->left_child = node_to_insert;
        }
        else
        {
            insert_node(err, &(*root)->left_child, node_to_insert);
        }
    }
    else
    {
        if ((*root)->right_child == NULL)
        {
            (*root)->right_child = node_to_insert;
        }
        else
        {
            insert_node(err, &(*root)->right_child, node_to_insert);
        }
    }
    check_and_restore_balance(root);
}

bst_node *get_successor(bst_node *curr)
{
    curr = curr->right_child;
    while (curr != NULL && curr->left_child != NULL)
        curr = curr->left_child;
    return curr;
}

bst_node *remove_node(bst_node **root, int key_to_remove)
{
    if ((*root) == NULL)
    {
        return (*root);
    }
    if ((*root)->key == key_to_remove)
    {
        if ((*root)->left_child == NULL)
        {
            bst_node *temp = (*root)->right_child;
            free_node((*root));
            return temp;
        }
        if ((*root)->right_child == NULL)
        {
            bst_node *temp = (*root)->left_child;
            free_node((*root));
            return temp;
        }

        // for node with 2 children
        bst_node *successor = get_successor((*root));

        UTILS_SAFE_FREE(&(*root)->data);

        (*root)->key = successor->key;
        (*root)->data = successor->data;
        successor->data = NULL; // ownership transition
        (*root)->right_child = remove_node(&(*root)->right_child, successor->key);
    }
    else if ((*root)->key > key_to_remove)
    {
        (*root)->left_child = remove_node(&(*root)->left_child, key_to_remove);
    }
    else
    {
        (*root)->right_child = remove_node(&(*root)->right_child, key_to_remove);
    }

    check_and_restore_balance(root);
    return (*root);
}

bst_node *find_node(bst_node *root, int key_to_find)
{
    if (root == NULL)
    {
        return NULL;
    }
    if (root->key == key_to_find)
    {
        return root;
    }
    if (key_to_find < root->key)
    {
        return find_node(root->left_child, key_to_find);
    }
    else
    {
        return find_node(root->right_child, key_to_find);
    }
}

// globals -------------------------------------------------------------------------------------------------------------
bst_table *new_table(error *err)
{
    *err = NONE;
    bst_table *table = (bst_table *)utils_safe_malloc(err, sizeof(bst_table));
    if (*err != NONE)
    {
        return NULL;
    }

    table->root = NULL; // set to
    return table;
}

void bst_destruct(bst_table **self)
{
    if ((*self)->root != NULL)
    {
        free_node_with_children((*self)->root);
        (*self)->root = NULL;
    }
    UTILS_SAFE_FREE(self);
}

void bst_insert(bst_table *self, error *err, int key, void **data)
{
    *err = NONE;
    bst_node *node_to_insert = create_node(err, key, *data);
    if (*err != NONE)
    {
        return;
    }
    if (self->root == NULL)
    {
        self->root = node_to_insert;
    }
    else
    {
        insert_node(err, &self->root, node_to_insert);
    }
    *data = NULL; // ensure value can not be used or free anywhere else
}

void bst_remove(bst_table *self, int key)
{
    self->root = remove_node(&self->root, key);
}

bool bst_search(bst_table *self, int key)
{
    if (find_node(self->root, key) != NULL)
    {
        return true;
    }
    return false;
}

void *bst_getdata(bst_table *self, int key)
{
    bst_node *found_node = find_node(self->root, key);
    if (found_node != NULL)
    {
        return found_node->data;
    }
    return NULL;
}