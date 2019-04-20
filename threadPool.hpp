//
// Created by wangyubo on 4/19/19.
//

#ifndef THREADPOOLTEST_THREADPOOL_HPP
#define THREADPOOLTEST_THREADPOOL_HPP

#include <vector>
#include <functional>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <future>

namespace std {
#define MAX_THREAD_NUM 256

    class ThreadPool {
        using Task = std::function<void()>;

    public:
        inline ThreadPool(unsigned short size = 4) : m_stopped(false) {
            m_idleThreadNum = size < 1 ? 1 : size;
            for (size = 0; size < m_idleThreadNum; ++size) {
                //初始化线程数量
                m_pool.emplace_back(
                        [this] {
                            //工作线程函数
                            while (!this->m_stopped) {
                                std::function<void()> task;
                                {
                                    //获取一个待执行的task
                                    std::unique_lock<std::mutex> lock(
                                            this->m_lock);    //unique_lock相比lock_guard的好处是: 可以随时unlock()和lock()
                                    this->cv_task.wait(lock,
                                                       [this] {
                                                           return this->m_stopped.load() || !this->m_tasks.empty();
                                                       });  //wait直到有task
                                    if (this->m_stopped && this->m_tasks.empty())
                                        return;
                                    task = std::move(this->m_tasks.front());
                                    this->m_tasks.pop();
                                }
                                m_idleThreadNum--;
                                task();
                                m_idleThreadNum++;
                            }
                        }
                );
            }
        }

        inline ~ThreadPool() {
            m_stopped.store(true);
            cv_task.notify_all();   //唤醒所有线程执行
            for (auto &thread : m_pool) {
                if (thread.joinable()) {
                    thread.join();  //等待任务结束，前提：线程一定会执行完
                }
            }
        }

        //提交一个任务
        //调用.get()获取返回值会等待任务执行完，获取返回值
        //有两种方法可以实现调用类成员
        //一种是使用 bind: .commit(std::bind(&Dog::sayHello, &dog));
        //一种是用 mem_fn: .commit(std::mem_fn(&Dog::sayHello), &dog);
        template<class F, class... Args>
        auto commit(F &&f, Args &&... args) -> std::future<decltype(f(args...))> {
            if (m_stopped.load())
                throw std::runtime_error("Commit on ThreadPool is stopped.");
            using RetType = decltype(f(args...));   //typename std::result_of<F(Arg...)>::type, 函数f的返回值类型
            auto task = std::make_shared<std::packaged_task<RetType()>>(
                    std::bind(std::forward<F>(f), std::forward<Args>(args)...)
            );
            std::future<RetType> future = task->get_future();
            {
                std::lock_guard<std::mutex> lock(
                        m_lock);   //对当前块的语句加锁 lock_guard是mutex的stack封装类，构造的时候lock()，析构的时候unlock()
                m_tasks.emplace(
                        [task]() {
                            (*task)();
                        }
                );
            }
            cv_task.notify_one();   //唤醒一个线程执行

            return future;
        }

        //空闲线程数量
        int idleCount() {
            return m_idleThreadNum;
        }

    private:
        //线程池
        std::vector<std::thread> m_pool;
        //任务队列
        std::queue<Task> m_tasks;
        //同步
        std::mutex m_lock;
        //条件阻塞
        std::condition_variable cv_task;
        //是否关闭提交
        std::atomic<bool> m_stopped;
        //空闲线程数量
        std::atomic<int> m_idleThreadNum;
    };
}

#endif //THREADPOOLTEST_THREADPOOL_HPP
