#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

/*
以上是一个使用C语言实现的线程池，可以安全地管理多个任务，
并在多线程环境下分配和处理任务。线程池支持添加任务、
关闭线程池和设置最大线程数等功能，使用互斥锁和条件变量保护共享资源，
以避免多线程竞争导致的数据不一致问题。在实现过程中，
需要注意线程安全和内存管理等问题，以确保程序的正确性和性能
*/
// 定义线程池结构体
typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t cond;
    int thread_count;    // 线程计数
    int task_count;     // 任务计数
    int active_count;  // 运行任务计数
    int max_threads;   // 最大线程数量
    int queue_size;   // 队列大小
    int shutdown;     // 停止
    int callouts;    // 标注
    void (*task_fn)(void*);
    void** tasks;
    pthread_t* threads;
} thread_pool_t;

// 初始化线程池
int thread_pool_init(thread_pool_t* pool, int max_threads, int queue_size, void (*task_fn)(void*))
{
    if (max_threads <= 0 || queue_size < 0 || !task_fn) {
        return -1;
    }

    pool->max_threads = max_threads;
    pool->queue_size = queue_size;
    pool->task_fn = task_fn;

    pool->tasks = calloc(queue_size + max_threads, sizeof(void*));
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

    for (int i = 0; i < max_threads; i++) {
        pthread_create(&pool->threads[i], NULL, thread_pool_worker, pool);
    }

    pool->thread_count = max_threads;
    pool->active_count = 0;
    pool->task_count = 0;
    pool->shutdown = 0;

    return 0;
}

// 添加任务到队列
int thread_pool_add_task(thread_pool_t* pool, void* task)
{
    pthread_mutex_lock(&pool->lock);

    if (pool->shutdown) {
        pthread_mutex_unlock(&pool->lock);
        return -1;
    }

    while (pool->task_count == pool->queue_size) {
        pthread_cond_wait(&pool->cond, &pool->lock);
    }

    int next = (pool->task_count + pool->callouts) % (pool->queue_size + pool->thread_count);
    pool->tasks[next] = task;
    pool->task_count++;

    if (pool->active_count < pool->thread_count) {
        pthread_cond_signal(&pool->cond);
    }

    pthread_mutex_unlock(&pool->lock);

    return 0;
}

//线程池工作线程函数
void* thread_pool_worker(void* arg)
{
    thread_pool_t* pool = (thread_pool_t*)arg;

    while (1) {
        pthread_mutex_lock(&pool->lock);

        while (!pool->task_count && !pool->shutdown) {
            pool->active_count--;
            pthread_cond_wait(&pool->cond, &pool->lock);
            pool->active_count++;
        }

        if (pool->shutdown) {
            pthread_mutex_unlock(&pool->lock);
            break;
        }

        void* task = pool->tasks[pool->callouts];
        pool->callouts = (pool->callouts + 1) % (pool->queue_size + pool->thread_count);
        pool->task_count--;

        if (pool->task_count == pool->queue_size - 1) {
            pthread_cond_signal(&pool->cond);
        }

        pthread_mutex_unlock(&pool->lock);

        if (task) {
            pool->task_fn(task);
        }
    }

    pthread_exit(NULL);
}
// 关闭线程池
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

