/*
以上是一个基于Java线程池的架构规格，
使用C语言实现对应所有接口，并包含线程工厂、无锁任务队列、
核心线程、最大线程数、线程池拒绝策略、动态创建销毁线程、最大任务数等特性。
在实现过程中，需要注意线程安全和内存管理等问题，以确保程序的正确性和可靠性。
同时，为了提高并发性能，使用无锁任务队列来支持任务管理

线程池是一种高级的多线程模式，其中包含了多个工作线程，以便在需要时执行任务。它主要由以下几个组成部分：

线程工厂：用于创建新线程。
无锁任务队列：用于存储待处理的任务。
核心线程：在没有任务时始终存在的线程数量。
最大线程数：线程池允许的最大线程数量。
线程池拒绝策略：当任务队列已满且线程池达到最大线程数时，如何处理新任务。
动态创建销毁线程：线程池根据任务量自动增加或减少线程数量。
最大任务数：线程池允许的最大任务数量。
*/
typedef struct thread_pool_task_t {
    void (*task_fn)(void*);
    void* arg;
    struct thread_pool_task_t* next;
} thread_pool_task_t;


typedef pthread_t* (*thread_factory_fn_t)(void (*start_routine)(void*), void* arg);
typedef enum {
    THREAD_POOL_ABORT_POLICY,
    THREAD_POOL_DISCARD_POLICY,
    THREAD_POOL_DISCARD_OLDEST_POLICY,
    THREAD_POOL_CALLER_RUNS_POLICY,
} thread_pool_reject_policy_t;


typedef struct {
    int core_threads;
    int max_threads;
    int max_tasks;
    int task_count;
    int active_count;
    int shutdown;
    void (*before_fn)(void*);
    void (*after_fn)(void*);
    void (*task_fn)(void*);
    thread_pool_reject_policy_t reject_policy;
    pthread_mutex_t lock;
    pthread_cond_t  cond;
    thread_factory_fn_t thread_factory_fn;
    lock_free_queue_t tasks_head;
    pthread_t* threads;
} thread_pool_t;

int thread_pool_init(thread_pool_t* pool, int core_threads, int max_threads, int max_tasks,
                     thread_pool_reject_policy_t reject_policy, void (*before_fn)(void*), void (*after_fn)(void*),
                     void (*task_fn)(void*), thread_factory_fn_t thread_factory_fn)
{
    if (core_threads <= 0 || max_threads < core_threads || max_tasks < 0 || !task_fn) {
        return -1;
    }

    pool->core_threads = core_threads;
    pool->max_threads = max_threads;
    pool->max_tasks = max_tasks;
    pool->task_count = 0;
    pool->active_count = 0;
    pool->shutdown = 0;
    pool->before_fn = before_fn;
    pool->after_fn = after_fn;
    pool->task_fn = task_fn;
    pool->reject_policy = reject_policy;
    pool->thread_factory_fn = thread_factory_fn;

    lock_free_queue_init(&pool->tasks_head);

    pool->threads = calloc(max_threads, sizeof(pthread_t));
    if (!pool->threads) {
        return -1;
    }

    pthread_mutex_init(&pool->lock, NULL);
    pthread_cond_init(&pool->cond, NULL);

    for (int i = 0; i < core_threads; i++) {
        pool->thread_factory_fn(thread_pool_worker, pool);
    }

    return 0;
}
int thread_pool_submit(thread_pool_t* pool, void* arg)
{
    if (pool->max_tasks > 0 && pool->task_count >= pool->max_tasks) {
        if (pool->reject_policy == THREAD_POOL_ABORT_POLICY) {
            return -1;
        } else if (pool->reject_policy == THREAD_POOL_DISCARD_POLICY) {
            return 0;
        } else if (pool->reject_policy == THREAD_POOL_DISCARD_OLDEST_POLICY) {
            thread_pool_task_t* oldest_task = lock_free_queue_pop(&pool->tasks_head);
            if (oldest_task) {
                free(oldest_task->arg);
                free(oldest_task);
                pool->task_count--;
            }
        } else if (pool->reject_policy == THREAD_POOL_CALLER_RUNS_POLICY) {
            (*pool->task_fn)(arg);
            return 0;
        }
    }

    thread_pool_task_t* task = malloc(sizeof(thread_pool_task_t));
    if (!task) {
        return -1;
    }

    task->task_fn = pool->task_fn;
    task->arg = arg;

    lock_free_queue_push(&pool->tasks_head, task);

    pthread_mutex_lock(&pool->lock);

    if ((pool->active_count < pool->thread_count) ||
        (pool->task_count > (pool->thread_count * THREAD_POOL_TASKS_PER_THREAD_RATIO)) ||(pool->active_count == 0 && pool->thread_count < pool->max_threads)) {
        thread_factory_fn_t thread_factory_fn = (thread_factory_fn_t)pthread_create;
        thread_factory_fn(thread_pool_worker, pool);
    }

    pthread_cond_signal(&pool->cond);

    pool->task_count++;

    pthread_mutex_unlock(&pool->lock);

    return 0;
}
void* thread_pool_worker(void* arg)
{
    thread_pool_t* pool = (thread_pool_t*)arg;

    while (1) {
        pthread_mutex_lock(&pool->lock);

        while (lock_free_queue_empty(&pool->tasks_head) && !pool->shutdown) {
            if (pool->active_count > pool->core_threads) {
                pool->active_count--;
                pthread_mutex_unlock(&pool->lock);
                return NULL;
            }

            pthread_cond_wait(&pool->cond, &pool->lock);

            pool->active_count++;
        }

        if (pool->shutdown) {
            pool->active_count--;
            pthread_mutex_unlock(&pool->lock);
            break;
        }

        thread_pool_task_t* task = lock_free_queue_pop(&pool->tasks_head);
        pool->task_count--;

        if (task) {
            if (pool->before_fn) {
                (*pool->before_fn)(task->arg);
            }

            (*task->task_fn)(task->arg);

            if (pool->after_fn) {
                (*pool->after_fn)(task->arg);
            }

            free(task->arg);
            free(task);
        }

        if (pool->task_count == 0 && pool->active_count > pool->core_threads &&
            pool->active_count > pool->thread_count * THREAD_POOL_IDLE_THREADS_PER_TASK_RATIO) {
            pool->active_count--;
            pthread_mutex_unlock(&pool->lock);
            break;
        }

        pthread_mutex_unlock(&pool->lock);
    }

    pthread_exit(NULL);
}
int thread_pool_shutdown(thread_pool_t* pool)
{
    pthread_mutex_lock(&pool->lock);

    if (pool->shutdown) {
        pthread_mutex_unlock(&pool->lock);
        return -1;
    }

    pool->shutdown = 1;

    pthread_cond_broadcast(&pool->cond);

    pthread_mutex_unlock(&pool->lock);

    for (int i = 0; i < pool->thread_count; i++) {
        pthread_join(pool->threads[i], NULL);
    }

    free(pool->threads);

    pthread_mutex_destroy(&pool->lock);
    pthread_cond_destroy(&pool->cond);

    return 0;
}
void thread_pool_destroy(thread_pool_t* pool)
{
    thread_pool_shutdown(pool);
    lock_free_queue_destroy(&pool->tasks_head);
}
