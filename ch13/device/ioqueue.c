#include "ioqueue.h"
#include "interrupt.h"
#include "global.h"
#include "debug.h"

void ioqueue_init(struct ioqueue* ioq) {
    lock_init(&ioq->lock);
    ioq->producer = ioq->consumer = NULL;
    ioq->head = ioq->tail = 0;
}

static int32_t next_pos(int32_t pos) {
    return (pos + 1)% bufsize;
}

bool ioq_full(struct ioqueue* ioq) {
    ASSERT(intr_get_status() == INTR_OFF);
    return next_pos(ioq->head) == ioq->tail;
}

bool ioq_empty(struct ioqueue* ioq) {
    ASSERT(intr_get_status() == INTR_OFF);
    return ioq->head == ioq->tail;
}

// 让当前生产者or消费者在缓冲区上等待
static void ioq_wait(struct task_struct** waiter) {
    ASSERT(*waiter == NULL && waiter != NULL); // 第一个NULL意思是: waiter队列是空, 就不用wait了
    *waiter = running_thread();
    thread_block(TASK_BLOCKED);
}

static void wakeup(struct task_struct** waiter) {
    ASSERT(*waiter != NULL);
    thread_unblock(*waiter);
    *waiter = NULL;
}

char ioq_getchar(struct ioqueue* ioq) {
    ASSERT(intr_get_status() == INTR_OFF);

    while (ioq_empty(ioq)) {
        // 队列空, 没法get. 把当前thread标记为consumer. 并加入等待队列
        lock_acquire(&ioq->lock);
        ioq_wait(&ioq->consumer);
        lock_release(&ioq->lock);
    }

    char byte = ioq->buf[ioq->tail];
    ioq->tail = next_pos(ioq->tail);

    if (ioq->producer != NULL) {
        wakeup(&ioq->producer);
    }
    return byte;
}

void ioq_putchar(struct ioqueue* ioq, char byte) {
    ASSERT(intr_get_status() == INTR_OFF);
    
    while (ioq_full(ioq)) {
        // 队列满, 没法put. 把当前thread标记为producer. 并加入等待队列
        lock_acquire(&ioq->lock);
        ioq_wait(&ioq->producer);
        lock_release(&ioq->lock);
    }

    ioq->buf[ioq->head] = byte;
    ioq->head = next_pos(ioq->head);

    if (ioq->consumer != NULL) {
        wakeup(&ioq->consumer);
    }
}
