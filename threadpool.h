#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <vector>
#include <deque>
#include <functional>
#include <stop_token>
#include <condition_variable>
#include <mutex>
#include <thread>


class ThreadPool {
    std::deque<std::function<void()>> m_tasks;
    std::vector<std::jthread> m_threads;
    std::mutex m_tasksMtx;
    std::condition_variable m_tasksCv;
    size_t m_numThreads;
    std::stop_source m_stopSource;

    void createThreadPool();

public:
    ThreadPool(size_t numThreads=8);
    ~ThreadPool();
    void stop();

    template <typename T>
    inline void addTask(T&& task) {
        std::unique_lock<std::mutex> lock(m_tasksMtx);
        m_tasks.push_back(std::forward<T>(task));
        m_tasksCv.notify_one();
    }
};
#endif // THREADPOOL_H