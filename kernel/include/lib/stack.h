#pragma once

#include <lib/helpers.h>
typedef struct stack_node stack_node_t;

struct stack_node {
    stack_node_t* next;
};

typedef struct {
    stack_node_t* head;
} stack_t;

#define STACK_FOR_EACH(LIST, VAR) for(stack_node_t* VAR = (LIST)->head; VAR != nullptr; VAR = VAR->next)

static inline void stack_push(stack_t* stack, stack_node_t* node) {
    stack_node_t* old = stack->head;
    node->next = old;
    stack->head = node;
}

static inline stack_node_t* stack_pop(stack_t* stack) {
    if(stack->head == nullptr)
        return nullptr;
    stack_node_t* old = stack->head;
    stack->head = stack->head->next;
    return old;
}
