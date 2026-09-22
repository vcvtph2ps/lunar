#include <common/assert.h>
#include <common/sched/timer_wait.h>
#include <common/sync/spinlock.h>
#include <common/sync/wait_obj.h>
#include <common/time/time.h>
#include <lib/helpers.h>
#include <lib/list.h>
#include <memory/heap.h>

static spinlock_no_dw_t g_timer_wait_lock = SPINLOCK_NO_DW_INIT;
static list_t g_timer_waits = LIST_INIT;

timer_wait_t* timer_wait_create(uint64_t msec) {
    timer_wait_t* timer = heap_zalloc(sizeof(timer_wait_t));
    assert(timer != nullptr);

    timer->obj = WAIT_OBJ_INIT(WAIT_OBJ_NOTIFICATION);
    timer->deadline_ns = time_monotonic_ns() + (msec * 1000000ULL);
    ATOMIC_STORE(&timer->pending, true, ATOMIC_RELEASE);

    spinlock_nodw_lock(&g_timer_wait_lock);
    bool inserted = false;
    LIST_FOR_EACH(&g_timer_waits, node) {
        timer_wait_t* current = CONTAINER_OF(node, timer_wait_t, list_node);
        if(timer->deadline_ns < current->deadline_ns) {
            list_node_prepend(&g_timer_waits, node, &timer->list_node);
            inserted = true;
            break;
        }
    }
    if(!inserted) {
        list_push_back(&g_timer_waits, &timer->list_node);
    }
    spinlock_nodw_unlock(&g_timer_wait_lock);

    return timer;
}

void timer_wait_free(timer_wait_t* timer) {
    if(ATOMIC_XCHG(&timer->pending, false, ATOMIC_ACQ_REL)) {
        spinlock_nodw_lock(&g_timer_wait_lock);
        list_node_delete(&g_timer_waits, &timer->list_node);
        spinlock_nodw_unlock(&g_timer_wait_lock);
    }
    heap_free(timer, sizeof(timer_wait_t));
}

void timer_wait_check() {
    uint64_t now = time_monotonic_ns();

    while(true) {
        timer_wait_t* timer = nullptr;

        spinlock_nodw_lock(&g_timer_wait_lock);
        list_node_t* node = g_timer_waits.head;
        if(node != nullptr) {
            timer_wait_t* head = CONTAINER_OF(node, timer_wait_t, list_node);
            if(head->deadline_ns <= now) {
                list_pop_front(&g_timer_waits);
                ATOMIC_STORE(&head->pending, false, ATOMIC_RELEASE);
                timer = head;
            }
        }
        spinlock_nodw_unlock(&g_timer_wait_lock);

        if(timer == nullptr) {
            return;
        }

        wait_obj_signal(&timer->obj, 1);
    }
}
