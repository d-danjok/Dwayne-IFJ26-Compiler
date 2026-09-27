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

    int weight;
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
    node->weight = 1;

    node->data = data;
    node->key = key;

    return node;
}

void update_weight_and_balance(bst_node *node)
{
    int weight = 1;
    int balance = 0;
    if (node->right_child != NULL)
    {
        weight += (node->right_child)->weight;
        balance -= (node->right_child)->weight;
    }
    if (node->left_child != NULL)
    {
        weight += (node->left_child)->weight;
        balance += (node->left_child)->weight;
    }
    // debug
    // printf("NODE : K:%d W:%d B:%d\n", node->key, weight, balance);

    node->weight = weight;
    node->balance = balance;
}

void check_and_restore_balance_recursively(bst_node *root)
{
    if (root->left_child != NULL)
    {
        check_and_restore_balance_recursively(root->left_child);
    }

    if (root->right_child != NULL)
    {
        check_and_restore_balance_recursively(root->right_child);
    }
    update_weight_and_balance(root);
    // TODO: add rebalancing HERE
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
        }
        else
        {
            insert_node(root->left_child, node_to_insert);
        }
    }

    if (root->key < node_to_insert->key)
    {
        if (root->right_child == NULL)
        {
            root->right_child = node_to_insert;
        }
        else
        {
            insert_node(root->right_child, node_to_insert);
        }
    }
    update_weight_and_balance(root);
    // TODO: add rebalancing HERE
}

bst_node *get_successor(bst_node *curr)
{
    curr = curr->right_child;
    while (curr != NULL && curr->left_child != NULL)
        curr = curr->left_child;
    return curr;
}

bst_node *remove_node(bst_node *root, int key_to_remove)
{
    if (root == NULL)
    {
        return root;
    }
    if (root->key == key_to_remove)
    {
        if (root->left_child == NULL)
        {
            bst_node *temp = root->right_child;
            free(root);
            return temp;
        }
        if (root->right_child == NULL)
        {
            bst_node *temp = root->left_child;
            free(root);
            return temp;
        }

        // for node with 2 children
        bst_node *successor = get_successor(root);
        root->key = successor->key;
        root->data = successor->data;
        root->right_child = remove_node(root->right_child, successor->key);

        check_and_restore_balance_recursively(root);
        return root;
    }

    if (root->key > key_to_remove)
    {
        root->left_child = remove_node(root->left_child, key_to_remove);
    }
    if (root->key < key_to_remove)
    {
        root->right_child = remove_node(root->right_child, key_to_remove);
    }

    return root;
}

// globals
bst_table new_table()
{
    bst_table table;

    table.root = NULL; // set to
    return table;
}

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

void bst_remove(bst_table *self, int key)
{
    self->root = remove_node(self->root, key);
}

bool bst_search(bst_table *self, int key)
{

    return false;
}

void *bst_getdata(bst_table *self, int key)
{

    return NULL;
}