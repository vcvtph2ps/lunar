#pragma once
#include <stddef.h>

typedef enum {
    RB_COLOR_BLACK,
    RB_COLOR_RED,
} rb_color_t;

typedef enum {
    RB_DIRECTION_LEFT,
    RB_DIRECTION_RIGHT,
} rb_direction_t;

typedef struct rb_node {
    rb_color_t color;
    struct rb_node* parent;
    struct rb_node* left;
    struct rb_node* right;
} rb_node_t;

typedef struct {
    size_t (*value_of_node)(rb_node_t* node);
    rb_node_t* root;
} rb_tree_t;

typedef enum {
    RB_SEARCH_TYPE_EXACT, /* Find an exact match */
    RB_SEARCH_TYPE_NEAREST, /* Find the nearest match */
    RB_SEARCH_TYPE_NEAREST_LT, /* Find the nearest match that is less than the search value */
    RB_SEARCH_TYPE_NEAREST_LTE, /* Find the nearest match that is less than or equals to the search value */
    RB_SEARCH_TYPE_NEAREST_GT, /* Find the nearest match that is greater than the search value */
    RB_SEARCH_TYPE_NEAREST_GTE, /* Find the nearest match that is greater than or equals to the search value */
} rb_search_type_t;

/**
 * @brief Initializes a red-black tree with the specified value and length functions.
 * @param tree The red-black tree to initialize.
 * @param value_of_node A function that returns the value of a node, used for comparisons during tree operations.
 */
void rb_insert(rb_tree_t* tree, rb_node_t* node);

/**
 * @brief Removes a node from the red-black tree and rebalances the tree to maintain its properties.
 * @param tree The red-black tree to modify.
 * @param node The node to remove from the tree.
 */
void rb_remove(rb_tree_t* tree, rb_node_t* node);

/**
 * @brief Finds the first node in the red-black tree (the node with the smallest value).
 * @param tree The red-black tree to search.
 * @return A pointer to the first node in the tree, or NULL if the tree is empty.
 */
rb_node_t* rb_find_first(rb_tree_t* tree);

/**
 * @brief Finds the first node in the red-black tree (the node with the smallest value).
 * @param tree The red-black tree to search.
 * @return A pointer to the first node in the tree, or NULL if the tree is empty.
 */
rb_node_t* rb_find_next(rb_tree_t* tree, rb_node_t* current);

/**
 * @brief Finds the target node in the red-black tree based on the specified search type.
 * @param tree The red-black tree to search.
 * @param needle The value to search for in the tree.
 * @param search_type The type of search to perform
 */
rb_node_t* rb_find(rb_tree_t* tree, size_t needle, rb_search_type_t search_type);
