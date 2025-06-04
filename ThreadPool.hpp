#include <pthread.h>
#include <iostream>
#include <stdlib.h>
#include <fcntl.h>
#include <error.h>
#include <stdbool.h>
#include <time.h>
#include <stdio.h>
#include <unistd.h>
#include <cstring>
#include <string>
#include <functional>
#include <memory>

void sys_err(int ret, std::string str){
    if (ret != 0){
        std::cout << str;
        fprintf(stderr, " error:%s\n", strerror(ret));
        exit(1);
    }
}

// 任务队列（链表）类
class task{
public:
    std::function<void()> function;     // 存储已绑定参数的函数
    task *next;         // 链表实现
    // 构造函数：使用完美转发和参数包存储任意函数和参数
    template<typename Func, typename... Args>
    task(Func&& func, Args&&... args) {
        // 直接绑定所有参数，不使用占位符
        function = [func = std::forward<Func>(func), 
                   args = std::make_tuple(std::forward<Args>(args)...)]() mutable {
            // 使用std::apply调用函数并传递参数
            std::apply(func, args);
        };
    }
};

// 线程池类
class pthread_pool{
private:
    pthread_mutex_t lock;       // 互斥锁
    pthread_cond_t cond;        // 条件变量
    task *task_queue;           // 任务队列
    pthread_t *pthreads;        // 线程数组
    int pth_cnt;                // 线程数量
    bool stop;                  // 是否运行

    //每个线程优先执行此函数
    static void* pthreadrun(void* arg){
        pthread_pool* pool = static_cast<pthread_pool*>(arg);
        pool->consumer(pool);
        return nullptr;
    }

public:
    // 消费者
    void *consumer(pthread_pool* pool);

    //构造函数，初始化线程池
    pthread_pool(int ThreadCount = 500);

    // 添加任务到线程池
    template<typename Func, typename... Args>

    //向线程池添加任务
    void PushTask(Func&& func, Args&&... args);

    //析构函数，释放线程池
    ~ pthread_pool();
};


// 消费者
void *pthread_pool::consumer(pthread_pool* pool){
    while (1){
        int key = 0;
        pthread_mutex_lock(&pool->lock); // 上锁

        while (key == 0 ||( pool->task_queue == NULL && pool->stop == false )){
            key = 1;
            pthread_cond_wait(&pool->cond, &pool->lock);
        }
        //printf("消费者被唤醒，走出循环\n");
        // 出循环，表示已经拿到了锁且有任务
        // 检查是否需要终止线程
        if (pool->stop){
            pthread_mutex_unlock(&pool->lock);
            pthread_exit(NULL);
        }

        // 拿取任务
        task *mytask;
        mytask = pool->task_queue;
        if(mytask == NULL) {
            pthread_mutex_unlock(&pool->lock);
            continue;
        }
        pool->task_queue = pool->task_queue->next;
        // 拿完任务直接解锁
        pthread_mutex_unlock(&pool->lock);
        printf("任务函数地址：%p\n", reinterpret_cast<void*>(mytask->function.target_type().hash_code()));
        printf("消费者成功拿到任务\n");
        // 执行任务
        mytask->function();
        delete mytask;
        mytask = NULL;
    }
    return NULL;
}

//构造函数，初始化线程池
pthread_pool::pthread_pool(int ThreadCount){
    int ret = pthread_mutex_init(&lock, NULL); // 互斥锁
    sys_err(ret, "mutex_init");

    ret = pthread_cond_init(&cond, NULL); // 条件变量
    sys_err(ret, "cond_init");

    task_queue = NULL;                                               // 任务队列
    pthreads = (pthread_t *)malloc(ThreadCount * sizeof(pthread_t)); // 线程数组
    pth_cnt = ThreadCount;                                           // 线程数量
    // 生成线程
    for (int i = 0; i < ThreadCount; i++){
        ret = pthread_create(&pthreads[i], NULL, pthreadrun, this);
        sys_err(ret, "pthread_create");
    }
    stop = false;
}

// 添加任务到线程池
template<typename Func, typename... Args>
void pthread_pool::PushTask(Func&& func, Args&&... args) {
    /*std::unique_ptr<task> NewTask = std::make_unique<task>(
        std::forward<Func>(func),
        std::forward<Args>(args)...
    );*/
    task* NewTask = new task(std::forward<Func>(func), std::forward<Args>(args)...);
    NewTask->next = NULL;
    pthread_mutex_lock(&this->lock); // 上锁，写入公共空间

    if (this->task_queue == NULL){
        this->task_queue = NewTask;       // 转移所有权到队列
    }
    else{
        task *p = this->task_queue;
        while (p->next != NULL)
            p = p->next;
        p->next = NewTask;        // 转移所有权到队列
    }
    printf("已成功将一个任务push到任务队列\n");
    pthread_cond_signal(&this->cond);  // 唤醒工作线程
    pthread_mutex_unlock(&this->lock); // 解锁
}

//析构函数，释放线程池
pthread_pool::~ pthread_pool(){
    pthread_mutex_lock(&this->lock);     // 上锁
    this->stop = true;                   // 准备令全部线程开始停止
    pthread_cond_broadcast(&this->cond); // 叫醒全部线程
    pthread_mutex_unlock(&this->lock);   // 解锁

    // 回收所有线程
    for (int i = 0; i < this->pth_cnt; i++){
        pthread_join(this->pthreads[i], NULL);
    }

    free(this->pthreads); // 释放线程数组

    // 释放任务队列
    task *pt;
    while (this->task_queue){
        pt = this->task_queue;
        this->task_queue = pt->next;
        delete pt;
    }

    // 销毁锁和条件变量
    pthread_mutex_destroy(&this->lock);
    pthread_cond_destroy(&this->cond);
}
