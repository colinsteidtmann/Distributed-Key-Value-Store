#include "threadpool.h"
#include <iostream>
#include <format>
void ThreadPool::createThreadPool() {
    for (size_t i = 0; i < m_numThreads; i++) {
        m_threads.emplace_back([this](std::stop_token stoken){
            while (!stoken.stop_requested()) {
                std::unique_lock<std::mutex> lock(m_tasksMtx);
                m_tasksCv.wait(lock, [this, &stoken](){
                    return stoken.stop_requested() || !m_tasks.empty();
                });
                if (m_tasks.empty()) break; // stop must have been requested otherwise we wouldn't exit cv loop
                auto task = std::move(m_tasks.front());
                m_tasks.pop_front();
                lock.unlock();
                try {
                    task();
                } catch (std::exception& e) {
                    std::cerr << std::format("ThreadPool failed to execute task: {}", e.what()) << std::endl;
                }
                
            }
        }, m_stopSource.get_token());
    }
}

ThreadPool::ThreadPool(size_t numThreads) : 
m_numThreads{std::max(numThreads, static_cast<size_t>(1))}
{
    createThreadPool();
}

ThreadPool::~ThreadPool() {
    m_stopSource.request_stop();
    m_tasksCv.notify_all();
}

void ThreadPool::stop() {
    m_stopSource.request_stop();
    m_tasksCv.notify_all();
}