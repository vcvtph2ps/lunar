#pragma once

#include <lib/helpers.h>
#include <lib/stack.h>

typedef struct atomic_stack_node atomic_stack_node_t;

struct atomic_stack_node {
    atomic_stack_node_t* next;
};

typedef struct {
    ATOMIC atomic_stack_node_t* head;
} atomic_stack_t;

static inline void atomic_stack_push(atomic_stack_t* stack, atomic_stack_node_t* node) {
    atomic_stack_node_t* old_head = ATOMIC_LOAD(&stack->head, ATOMIC_RELAXED);
    while(true) {
        node->next = old_head;
        if(ATOMIC_COMPARE_EXCHANGE_STRONG(&stack->head, &old_head, node, ATOMIC_RELEASE, ATOMIC_RELAXED)) {
            break;
        }
    }
}

static inline stack_t atomic_stack_take_all(atomic_stack_t* stack) {
    return (stack_t) { .head = (stack_node_t*) ATOMIC_XCHG(&stack->head, nullptr, ATOMIC_ACQUIRE) };
}
