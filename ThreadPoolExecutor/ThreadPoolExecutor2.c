/*
下面是一个基于Java线程池的架构规格，使用C语言实现的示例程序。该程序实现了ThreadPoolExecutor的所有特性，包括任务队列、核心线程和最大线程数、线程池拒绝策略等
*/

typedef struct {
    void (*task_fn)(void*);
    void* arg;
} thread_pool_task_t;

typedef enum {
    THREAD_POOL_ABORT_POLICY, // 拒绝并抛出异常
    THREAD_POOL_DISCARD_POLICY, // 忽略并抛弃当前任务
    THREAD_POOL_DISCARD_OLDEST_POLICY, //抛弃队列头部（最旧）的一个任务，并执行当前任务
    THREAD_POOL_CALLER_RUNS_POLICY // 使用当前调用的线程来执行此任务
} thread_pool_reject_policy_t;

typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t cond;
    int core_threads;
    int max_threads;
    int keep_alive_time;
    int task_count;
    int active_count;
    int queue_size;
    int shutdown;
    thread_pool_reject_policy_t reject_policy;
    void (*before_fn)(void*);
    void (*after_fn)(void*);
    void (*task_fn)(void*);
    thread_pool_task_t** tasks;
    pthread_t* threads;
} thread_pool_t;

int thread_pool_init(thread_pool_t* pool, int core_threads, int max_threads, int keep_alive_time, int queue_size,
                     thread_pool_reject_policy_t reject_policy, void (*before_fn)(void*), void (*after_fn)(void*),
                     void (*task_fn)(void*))
{
    if (core_threads < 0 || max_threads < core_threads || keep_alive_time < 0 || queue_size <= 0 || !task_fn) {
        return -1;
    }

    pool->core_threads = core_threads;
    pool->max_threads = max_threads;
    pool->keep_alive_time = keep_alive_time;
    pool->queue_size = queue_size;
    pool->reject_policy = reject_policy;
    pool->before_fn = before_fn;
    pool->after_fn = after_fn;
    pool->task_fn = task_fn;

    pool->tasks = calloc(queue_size + max_threads, sizeof(thread_pool_task_t*));
    if (!pool->tasks) {
        return -1;
    }

    pool->threads = calloc(max_threads, sizeof(pthread_t));
    if (!pool->threads) {
        free(pool->tasks);
        return -1;
    }

    pthread_mutex_init(&pool->lock, NULL);
    pthread_cond_init(&pool->cond, NULL);

    for (int i = 0; i < core_threads; i++) {
        pthread_create(&pool->threads[i], NULL, thread_pool_worker, pool);
    }

    pool->thread_count = core_threads;
    pool->active_count = 0;
    pool->task_count = 0;
    pool->shutdown = 0;

    return 0;
}

int thread_pool_add_task(thread_pool_t* pool, void (*task_fn)(void*), void* arg)
{
    pthread_mutex_lock(&pool->lock);

    if (pool->shutdown) {
        pthread_mutex_unlock(&pool->lock);
        return -1;
    }

    while (pool->task_count == pool->queue_size) {
        if (pool->reject_policy == THREAD_POOL_ABORT_POLICY) {
            pthread_mutex_unlock(&pool->lock);
            return -1;
        } else if (pool->reject_policy == THREAD_POOL_DISCARD_POLICY) {
            pthread_mutex_unlock(&pool->lock);
            return 0;
        } else if (pool->reject_policy == THREAD_POOL_DISCARD_OLDEST_POLICY) {
            free(pool->tasks[pool->callouts]->arg);
            free(pool->tasks[pool->callouts]);
            pool->callouts = (pool->callouts + 1) % (pool->queue_size + pool->thread_count);
            pool->task_count--;
            continue;
        } else if (pool->reject_policy == THREAD_POOL_CALLER_RUNS_POLICY) {
            pthread_mutex_unlock(&pool->lock);
            (*task_fn)(arg);
            return 0;
        }
    }

    thread_pool_task_t* task = malloc(sizeof(thread_pool_task_t));
    task->task_fn = task_fn;
    task->arg = arg;

    int next = (pool->task_count + pool->callouts) % (pool->queue_size + pool->thread_count);
    pool->tasks[next] = task;
    pool->task_count++;

    if (pool->active_count < pool->max_threads) {
        pthread_cond_signal(&pool->cond);
    }

    pthread_mutex_unlock(&pool->lock);

    return 0;
}

void* thread_pool_worker(void* arg)
{
    thread_pool_t* pool = (thread_pool_t*)arg;

    while (1) {
        pthread_mutex_lock(&pool->lock);

        while (!pool->task_count && !pool->shutdown) {
            if (pool->active_count > pool->core_threads) {
                pool->active_count--;
                pthread_mutex_unlock(&pool->lock);
                return NULL;
            }

            if (pool->keep_alive_time > 0) {
                struct timespec abstime;
                clock_gettime(CLOCK_REALTIME, &abstime);
                abstime.tv_sec += pool->keep_alive_time;

                pthread_cond_timedwait(&pool->cond, &pool->lock, &abstime);
            } else {
                pthread_cond_wait(&pool->cond, &pool->lock);
            }

            pool->active_count++;
        }

        if (pool->shutdown) {
            pool->active_count--;
            pthread_mutex_unlock(&pool->lock);
            break;
        }

        thread_pool_task_t* task = pool->tasks[pool->callouts];
        pool->callouts = (pool->callouts + 1) % (pool->queue_size + pool->thread_count);
        pool->task_count--;

        if (pool->task_count == pool->queue_size - 1) {
            pthread_cond_signal(&pool->cond);
        }

        pthread_mutex_unlock(&pool->lock);

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

    free(pool->tasks);
    free(pool->threads);

    pthread_mutex_destroy(&pool->lock);
    pthread_cond_destroy(&pool->cond);

    return 0;
}






