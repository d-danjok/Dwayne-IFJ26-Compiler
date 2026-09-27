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

    size_t height;
    int balance;
} bst_node;

struct bst_table_s
{
    bst_node *root;
};

// utilities
void free_node(bst_node *node)
{
    if (node->left_child != NULL)
    {
        free_node(node->left_child);
    }
    if (node->right_child != NULL)
    {
        free_node(node->right_child);
    }
    UTILS_SAFE_FREE(&node);
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
    node->height = 0;

    node->data = data;
    node->key = key;

    return node;
}

void insert_node(bst_node *root, bst_node *node_to_insert)
{
    if (root->key == node_to_insert->key)
    {
        // do not do nothing
        return;
    }

    if (root->key > node_to_insert->key)
    {
        if (root->left_child == NULL)
        {
            root->left_child = node_to_insert;
            return;
        }
        insert_node(root->left_child, node_to_insert);
    }

    if (root->key < node_to_insert->key)
    {
        if (root->right_child == NULL)
        {
            root->right_child = node_to_insert;
            return;
        }
        insert_node(root->right_child, node_to_insert);
    }
}

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

// globals
void bst_destruct(bst_table *self)
{
    if (self->root != NULL)
    {
        free_node(self->root);
        self->root = NULL;
    }
    UTILS_SAFE_FREE(&self);
}

void bst_insert(bst_table *self, error *err, int key, void *data)
{
    *err = NONE;
    bst_node *node_to_insert = create_node(err, key, data);
    if (*err != NONE)
    {
        return;
    }
    if (self->root == NULL)
    {
        self->root = node_to_insert;
        return;
    }
    insert_node(self->root, node_to_insert);
}

void bst_remove(bst_table *self, int key);

bool bst_search(bst_table *self, int key);

void *bst_getdata(bst_table *self, int key);