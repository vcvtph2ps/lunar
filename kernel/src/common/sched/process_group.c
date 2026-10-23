#include <common/assert.h>
#include <common/sched/process.h>
#include <common/sched/process_group.h>
#include <common/sync/spinlock.h>
#include <lib/helpers.h>
#include <lib/list.h>
#include <lib/rb.h>
#include <memory/heap.h>
#include <stddef.h>
#include <stdint.h>

static size_t process_group_value_of_node(rb_node_t* node) {
    return CONTAINER_OF(node, process_group_t, rb_node)->pgid;
}

static spinlock_no_dpc_t g_process_group_lock = SPINLOCK_NO_DPC_INIT;
static rb_tree_t g_process_group_tree = {
    .value_of_node = process_group_value_of_node,
    .root = nullptr,
};

process_group_t* process_group_lookup(uint32_t pgid) {
    spinlock_no_dpc_lock(&g_process_group_lock);

    rb_node_t* node = rb_find(&g_process_group_tree, pgid, RB_SEARCH_TYPE_EXACT);
    process_group_t* group = node != nullptr ? CONTAINER_OF(node, process_group_t, rb_node) : nullptr;
    if(group != nullptr) {
        ATOMIC_LOAD_ADD(&group->ref_count, 1, ATOMIC_ACQ_REL);
    }

    spinlock_no_dpc_unlock(&g_process_group_lock);
    return group;
}

process_group_t* process_group_get_or_create(uint32_t pgid) {
    process_group_t* group = process_group_lookup(pgid);
    if(group != nullptr) {
        return group;
    }

    process_group_t* new_group = heap_zalloc(sizeof(process_group_t));
    if(new_group == nullptr) {
        return nullptr;
    }
    new_group->pgid = pgid;
    ATOMIC_STORE(&new_group->ref_count, 1, ATOMIC_RELAXED);
    new_group->members = LIST_INIT;

    spinlock_no_dpc_lock(&g_process_group_lock);

    /// Another core may have created the group while we were allocating
    rb_node_t* existing = rb_find(&g_process_group_tree, pgid, RB_SEARCH_TYPE_EXACT);
    if(existing != nullptr) {
        process_group_t* other = CONTAINER_OF(existing, process_group_t, rb_node);
        ATOMIC_LOAD_ADD(&other->ref_count, 1, ATOMIC_ACQ_REL);
        spinlock_no_dpc_unlock(&g_process_group_lock);
        heap_free(new_group, sizeof(process_group_t));
        return other;
    }

    rb_insert(&g_process_group_tree, &new_group->rb_node);
    spinlock_no_dpc_unlock(&g_process_group_lock);
    return new_group;
}

void process_group_deref(process_group_t* group) {
    assert(group != nullptr);

    if(ATOMIC_LOAD_SUB(&group->ref_count, 1, ATOMIC_ACQ_REL) != 1) {
        return;
    }

    assert(group->members.count == 0);
    heap_free(group, sizeof(process_group_t));
}

bool process_group_join(process_t* process, uint32_t pgid) {
    assert(process != nullptr);

    process_group_t* group = process_group_get_or_create(pgid);
    if(group == nullptr) {
        return false;
    }

    spinlock_no_dpc_lock(&g_process_group_lock);

    process_group_t* old_group = process->group;
    if(old_group == group) {
        spinlock_no_dpc_unlock(&g_process_group_lock);
        process_group_deref(group);
        return true;
    }

    if(old_group != nullptr) {
        list_node_delete(&old_group->members, &process->list_node_group);
        if(old_group->members.count == 0) {
            rb_remove(&g_process_group_tree, &old_group->rb_node);
        }
    }

    process->group = group;
    list_push_back(&group->members, &process->list_node_group);
    process_ref(process);

    spinlock_no_dpc_unlock(&g_process_group_lock);

    if(old_group != nullptr) {
        /// Drop the old group's reference on the process and the process' reference on the old group
        process_deref(process);
        process_group_deref(old_group);
    }

    return true;
}

void process_group_leave(process_t* process) {
    assert(process != nullptr);

    spinlock_no_dpc_lock(&g_process_group_lock);

    process_group_t* group = process->group;
    if(group == nullptr) {
        spinlock_no_dpc_unlock(&g_process_group_lock);
        return;
    }

    list_node_delete(&group->members, &process->list_node_group);
    process->group = nullptr;
    if(group->members.count == 0) {
        rb_remove(&g_process_group_tree, &group->rb_node);
    }

    spinlock_no_dpc_unlock(&g_process_group_lock);

    /// Drop the process' reference on the group and the group's reference on the process
    process_group_deref(group);
    process_deref(process);
}

uint32_t process_group_id(process_t* process) {
    assert(process != nullptr);

    spinlock_no_dpc_lock(&g_process_group_lock);
    uint32_t pgid = process->group != nullptr ? process->group->pgid : 0;
    spinlock_no_dpc_unlock(&g_process_group_lock);
    return pgid;
}

process_t** process_group_find_processes(uint32_t pgid, size_t* out_count) {
    assert(out_count != nullptr);
    *out_count = 0;

    spinlock_no_dpc_lock(&g_process_group_lock);

    rb_node_t* node = rb_find(&g_process_group_tree, pgid, RB_SEARCH_TYPE_EXACT);
    if(node == nullptr) {
        spinlock_no_dpc_unlock(&g_process_group_lock);
        return nullptr;
    }

    process_group_t* group = CONTAINER_OF(node, process_group_t, rb_node);
    size_t count = group->members.count;
    if(count == 0) {
        spinlock_no_dpc_unlock(&g_process_group_lock);
        return nullptr;
    }

    process_t** processes = heap_alloc(sizeof(process_t*) * count);
    if(processes == nullptr) {
        spinlock_no_dpc_unlock(&g_process_group_lock);
        return nullptr;
    }

    size_t index = 0;
    LIST_FOR_EACH(&group->members, member_node) {
        processes[index++] = process_ref(CONTAINER_OF(member_node, process_t, list_node_group));
    }

    spinlock_no_dpc_unlock(&g_process_group_lock);

    *out_count = count;
    return processes;
}
