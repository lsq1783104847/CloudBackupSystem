#ifndef CLOUD_BACKUP_THREAD_POOL_HPP
#define CLOUD_BACKUP_THREAD_POOL_HPP

#include <semaphore.h>
#include "config.hpp"

namespace cloud_backup
{
    template <class Task>
    class ThreadPool
    {
    public:
        using ptr = std::shared_ptr<ThreadPool<Task>>;
        ~ThreadPool()
        {
            sem_destroy(&_free_slots);
            sem_destroy(&_ready_tasks);
            for (auto &worker_thread : _worker_threads)
                worker_thread.join();
        }
        void push(const Task &task)
        {
            if (!task)
                return;
            sem_wait(&_free_slots);
            {
                std::unique_lock<std::mutex> productor_lock(_productor_mutex);
                _task_queue[_productor_pos++] = task;
                _productor_pos %= _task_queue_capacity;
            }
            sem_post(&_ready_tasks);
        }
        bool try_push(const Task &task)
        {
            if (!task)
                return false;
            if (sem_trywait(&_free_slots) == 0)
            {
                {
                    std::unique_lock<std::mutex> productor_lock(_productor_mutex);
                    _task_queue[_productor_pos++] = task;
                    _productor_pos %= _task_queue_capacity;
                }
                sem_post(&_ready_tasks);
                return true;
            }
            return false;
        }

    private:
        ThreadPool(int threads_size, int task_pool_capacity) : _task_queue_capacity(task_pool_capacity),
                                                               _task_queue(task_pool_capacity)
        {
            sem_init(&_free_slots, 0, task_pool_capacity);
            sem_init(&_ready_tasks, 0, 0);
            _worker_threads.reserve(threads_size);
            for (int i = 0; i < threads_size; i++)
                _worker_threads.push_back(std::thread(&ThreadPool<Task>::ThreadRUN, this));
        }
        ThreadPool(const ThreadPool<Task> &tp) = delete;
        ThreadPool<Task> &operator=(const ThreadPool<Task> &tp) = delete;

        void ThreadRUN()
        {
            Task task;
            while (1)
            {
                sem_wait(&_ready_tasks);
                {
                    std::unique_lock<std::mutex> consumer_lock(_consumer_mutex);
                    task = _task_queue[_consumer_pos];
                    _task_queue[_consumer_pos++] = Task();
                    _consumer_pos %= _task_queue_capacity;
                }
                sem_post(&_free_slots);
                task();
            }
        }

    private:
        std::vector<Task> _task_queue;
        std::vector<std::thread> _worker_threads;
        std::mutex _consumer_mutex;
        std::mutex _productor_mutex;
        int _task_queue_capacity;
        int _consumer_pos = 0;
        int _productor_pos = 0;
        sem_t _free_slots;
        sem_t _ready_tasks;

    public:
        static ThreadPool<Task>::ptr GetInstance()
        {
            static ThreadPool<Task>::ptr thread_pool(new ThreadPool<Task>(Config::GetInstance()->GetThreadPoolThreadsSize(),
                                                                          Config::GetInstance()->GetThreadPoolQueueCapacity()));
            if (thread_pool == nullptr)
                LOG_FATAL("create ThreadPool object fail");
            return thread_pool;
        }
    };
}

#endif
