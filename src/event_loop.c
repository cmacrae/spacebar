#include "event_loop.h"

#ifdef STATS
struct cycle_counter
{
    uint64_t cycle_count;
    uint64_t hit_count;
};

static struct cycle_counter queue_counters[2];
static struct cycle_counter event_counters[EVENT_TYPE_COUNT];

static inline void cycle_counter_tick(const char *name, struct cycle_counter *counter, uint64_t elapsed_cycles)
{
    uint64_t cycle_count = __sync_add_and_fetch(&counter->cycle_count, elapsed_cycles);
    uint64_t hit_count = __sync_add_and_fetch(&counter->hit_count, 1);
    fprintf(stdout, "%30s: hits %'25lld | cur %'25lld | avg %'25lld\n",
            name, hit_count, elapsed_cycles, cycle_count / hit_count);
}
#endif

static bool queue_init(struct queue *queue)
{
    if (!memory_pool_init(&queue->pool, QUEUE_POOL_SIZE)) return false;
    queue->head = memory_pool_push(&queue->pool, struct queue_item);
    queue->head->data = NULL;
    queue->head->next = NULL;
    queue->tail = queue->head;
#ifdef DEBUG
    queue->count = 0;
#endif
    return true;
};

static void queue_push(struct queue *queue, struct event *event)
{
    bool success;
    struct queue_item *tail, *new_tail;

#ifdef STATS
    uint64_t begin_cycles = __rdtsc();
#endif

    new_tail = memory_pool_push(&queue->pool, struct queue_item);
    new_tail->data = event;
    new_tail->next = NULL;
    __asm__ __volatile__ ("" ::: "memory");

    do {
        tail = queue->tail;
        success = __sync_bool_compare_and_swap(&tail->next, NULL, new_tail);
        if (!success) __sync_bool_compare_and_swap(&queue->tail, tail, tail->next);
    } while (!success);
    __sync_bool_compare_and_swap(&queue->tail, tail, new_tail);

#ifdef DEBUG
    uint64_t count = __sync_add_and_fetch(&queue->count, 1);
    assert(count > 0 && count < QUEUE_MAX_COUNT);
#endif

#ifdef STATS
    cycle_counter_tick(__FUNCTION__, &queue_counters[0], __rdtsc() - begin_cycles);
#endif
}

static struct event *queue_pop(struct queue *queue)
{
    struct queue_item *head;

#ifdef STATS
    uint64_t begin_cycles = __rdtsc();
#endif

    do {
        head = queue->head;
        if (!head->next) return NULL;
    } while (!__sync_bool_compare_and_swap(&queue->head, head, head->next));

#ifdef DEBUG
    uint64_t count = __sync_sub_and_fetch(&queue->count, 1);
    assert(count >= 0 && count < QUEUE_MAX_COUNT);
#endif

#ifdef STATS
    cycle_counter_tick(__FUNCTION__, &queue_counters[1], __rdtsc() - begin_cycles);
#endif

    return head->next->data;
}

static void *event_loop_run(void *context)
{
    struct event_loop *event_loop = (struct event_loop *) context;
    struct queue *queue = (struct queue *) &event_loop->queue;

    while (event_loop->is_running) {
        struct event *event = queue_pop(queue);
        if (event) {
#ifdef STATS
            uint64_t begin_cycles = __rdtsc();
#endif
            uint32_t result = event_handler[event->type](event->context, event->param1);
#ifdef STATS
            cycle_counter_tick(event_type_str[event->type], &event_counters[event->type], __rdtsc() - begin_cycles);
#endif
            if (event->info) *event->info = (result << 0x1) | EVENT_PROCESSED;

            event_destroy(event_loop, event);
        } else {
            sem_wait(event_loop->semaphore);
        }
    }

    return NULL;
}

void event_loop_post(struct event_loop *event_loop, struct event *event)
{
    assert(event_loop->is_running);
    queue_push(&event_loop->queue, event);
    sem_post(event_loop->semaphore);
}

bool event_loop_init(struct event_loop *event_loop)
{
    if (!queue_init(&event_loop->queue)) return false;
    if (!memory_pool_init(&event_loop->pool, EVENT_POOL_SIZE)) return false;
    event_loop->is_running = false;
#ifdef DEBUG
    event_loop->count = 0;
#endif
#ifdef STATS
    setlocale(LC_ALL, ""); // For fprintf digit grouping
#endif
    event_loop->semaphore = sem_open("spacebar_event_loop_semaphore", O_CREAT, 0600, 0);
    sem_unlink("spacebar_event_loop_semaphore");
    return event_loop->semaphore != SEM_FAILED;
}

bool event_loop_begin(struct event_loop *event_loop)
{
    if (event_loop->is_running) return false;
    event_loop->is_running = true;
    pthread_create(&event_loop->thread, NULL, &event_loop_run, event_loop);
    return true;
}

bool event_loop_end(struct event_loop *event_loop)
{
    if (!event_loop->is_running) return false;
    event_loop->is_running = false;
    pthread_join(event_loop->thread, NULL);
    return true;
}
