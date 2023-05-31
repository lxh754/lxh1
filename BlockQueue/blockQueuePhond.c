/*
实现一个基于C语言的阻塞队列（BlockQueue）可以使用线程锁和条件变量来解决并发问题。实现一个阻塞队列需要以下基本功能：

1.入队操作

2.出队操作

3.队列长度获取

4.阻塞操作：当队列为空时出队操作和队列长度操作会被阻塞，当队列满时入队操作会被阻塞。

下面是一个简单的基于C语言的阻塞队列实现代码：

在上面的代码中，我们定义了一个QUEUE结构体，其中包含了队列数据及其容量大小，头部偏移量、尾部偏移量和队列大小、互斥量和两个条件变量。

我们的程序中实现了三个基本操作：

1.队列push操作：插入新的元素到队列尾部

2.队列pop操作：弹出队列头部元素并返回

3.队列length操作：获取队列长度

我们的代码中使用了线程锁和条件变量来实现阻塞操作。
在push操作中，当队列已满时，当前线程blocked，等待not_full条件变量的通知，
当队列不满时通知push的线程。在pop操作中，当队列为空时，当前线程blocked，等待not_empty条件变量的通知，当队列非空时通知pop的线程。

使用方式和普通队列一样，可以以函数库的模式实现，或者直接在程序中使用。

阻塞队列的优势在于可以轻松实现不同线程之间的数据共享和传递，避免数据同步问题，
也能很好的解决生产者消费者模型中数据缓存和传递的问题。阻塞队列同时也会造成性能的损失，需要根据应用场景权衡利弊。
*/
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#define QUEUE_SIZE 10

typedef struct {
    int data[QUEUE_SIZE];
    int head;
    int tail;
    int count;
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
}QUEUE;

QUEUE queue;

static void queue_init(QUEUE *queue) {
    queue->head = 0;
    queue->tail = 0;
    queue->count = 0;
    pthread_mutex_init(&queue->mutex, NULL);
    pthread_cond_init(&queue->not_empty, NULL);
    pthread_cond_init(&queue->not_full, NULL);
}

static void queue_push(QUEUE *queue, int data) {
    pthread_mutex_lock(&queue->mutex);
    while(queue->count >= QUEUE_SIZE) {
        pthread_cond_wait(&queue->not_full, &queue->mutex);
    }
    queue->data[queue->tail] = data;
    queue->tail = (queue->tail + 1) % QUEUE_SIZE;
    queue->count++;
    pthread_cond_signal(&queue->not_empty);
    pthread_mutex_unlock(&queue->mutex);
}

static int queue_pop(QUEUE *queue) {
    pthread_mutex_lock(&queue->mutex);
    while(queue->count <= 0) {
        pthread_cond_wait(&queue->not_empty, &queue->mutex);
    }
    int data = queue->data[queue->head];
    queue->head = (queue->head + 1) % QUEUE_SIZE;
    queue->count--;
    pthread_cond_signal(&queue->not_full);
    pthread_mutex_unlock(&queue->mutex);
    return data;
}

static int queue_length(QUEUE *queue) {
    int count;
    pthread_mutex_lock(&queue->mutex);
    count = queue->count;
    pthread_mutex_unlock(&queue->mutex);
    return count;
}

void* producer(void *arg) {
    int i;
    for(i=0; i<50; i++) {
        printf("producer push %d\n", i);
        queue_push(&queue, i);
        sleep(1);
    }
    return NULL;
}

void* consumer(void *arg) {
    int i, data;
    for(i=0; i<25; i++) {
        data = queue_pop(&queue);
        printf("consumer pop %d\n", data);
        sleep(2);
    }
    return NULL;
}

int main() {
    pthread_t producer_id, consumer_id;
    queue_init(&queue);
    pthread_create(&producer_id, NULL, producer, NULL);
    pthread_create(&consumer_id, NULL, consumer, NULL);
    pthread_join(producer_id, NULL);
    pthread_join(consumer_id, NULL);
    return 0;
}

