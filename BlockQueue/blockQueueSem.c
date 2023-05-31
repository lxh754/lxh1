/*
以上代码中，我们使用了mutex、semaphore和pthread库，分别用于实现访问队列的原子性、阻塞队列的同步和线程处理。

我们首先定义了一个BlockingQueue结构体，用于保存队列中的元素信息和同步使用的信号量、锁的信息，
我们使用了一个存放元素的数组，一个指向头部的head指针和一个指向尾部的tail指针。

接下来，我们实现了createBlockingQueue和destroyBlockingQueue函数，用于创建和销毁阻塞队列。

然后是pushBlockingQueue和popBlockingQueue函数，用于阻塞添加元素和阻塞获取元素。
这两个函数都使用了sem_timedwait函数，在可用时间段内等待信号量的值，并将其减一。
如果信号量的值已经为零，则无法通过该函数进一步访问，直到信号量超时或其他线程或进程释放了信号量。

我们的tryPushBlockingQueue和tryPopBlockingQueue函数相似，不过这两个函数使用了sem_trywait函数，
当函数等待的信号量空间已经满或者队列中不存在元素时，会立即返回且不等待。

最后，我们实现的是notifyAllBlockingQueue函数，用于唤醒阻塞的push和pop操作，向信号量值加一表示队列中新增了元素或空闲空间。
*/
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <time.h>

#define QUEUE_SIZE 5

typedef struct {
    int data[QUEUE_SIZE];
    int head;
    int tail;
    sem_t spaces;   // 信号量，表示队列中可用的空闲空间数
    sem_t items;    // 信号量，表示队列中存在的元素数目
    pthread_mutex_t lock;   // 互斥锁，用于保证队列操作的原子性
} BlockingQueue;

BlockingQueue * createBlockingQueue() {
    BlockingQueue * queue = (BlockingQueue *) malloc(sizeof(BlockingQueue));
    queue->head = 0;
    queue->tail = 0;
    sem_init(&queue->spaces, 0, QUEUE_SIZE);   // 初始化spaces为QUEUE_SIZE，表示队列开始时为空闲空间数为QUEUE_SIZE
    sem_init(&queue->items, 0, 0);  // 初始化items为0，表示队列开始时不存在任何元素
    pthread_mutex_init(&queue->lock, NULL);
    return queue;
}

void destroyBlockingQueue(BlockingQueue * queue) {
    sem_destroy(&queue->spaces);
    sem_destroy(&queue->items);
    pthread_mutex_destroy(&queue->lock);
    free(queue);
}

int pushBlockingQueue(BlockingQueue * queue, int data, int timeout) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += timeout;

    // 等待可用的空闲空间
    if (sem_timedwait(&queue->spaces, &ts) == -1) {
        return -1;  // 超时，返回-1
    }

    pthread_mutex_lock(&queue->lock);
    queue->data[queue->tail] = data;
    queue->tail = (queue->tail + 1) % QUEUE_SIZE;
    pthread_mutex_unlock(&queue->lock);

    sem_post(&queue->items);    // 通知有新元素加入队列

    return 0;   // 成功添加元素，返回0
}

int popBlockingQueue(BlockingQueue * queue, int * data, int timeout) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += timeout;

    // 等待队列中存在的元素
    if (sem_timedwait(&queue->items, &ts) == -1) {
        return -1;  // 超时，返回-1
    }

    pthread_mutex_lock(&queue->lock);
    *data = queue->data[queue->head];
    queue->head = (queue->head + 1) % QUEUE_SIZE;
    pthread_mutex_unlock(&queue->lock);

    sem_post(&queue->spaces);   // 通知有空闲空间可以新增元素

    return 0;   // 成功获取元素，返回0
}

int tryPushBlockingQueue(BlockingQueue * queue, int data) {
    if (sem_trywait(&queue->spaces) == -1) {
        return -1;  // 空间不足，返回-1
    }

    pthread_mutex_lock(&queue->lock);
    queue->data[queue->tail] = data;
    queue->tail = (queue->tail + 1) % QUEUE_SIZE;
    pthread_mutex_unlock(&queue->lock);

    sem_post(&queue->items);    // 通知有新元素加入队列

    return 0;   // 成功添加元素，返回0
}

int tryPopBlockingQueue(BlockingQueue * queue, int * data) {
    if (sem_trywait(&queue->items) == -1) {
        return -1;  // 队列中不存在元素，返回-1
    }

    pthread_mutex_lock(&queue->lock);
    *data = queue->data[queue->head];
    queue->head = (queue->head + 1) % QUEUE_SIZE;
    pthread_mutex_unlock(&queue->lock);

    sem_post(&queue->spaces);   // 通知有空闲空间可以新增元素

    return 0;   // 成功获取元素，返回0
}

void notifyAllBlockingQueue(BlockingQueue * queue) {
    sem_post(&queue->spaces);
    sem_post(&queue->items);
}


