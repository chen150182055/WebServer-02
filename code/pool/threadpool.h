#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <mutex>
#include <condition_variable>
#include <queue>
#include <thread>
#include <functional>

//实现简单的线程池
class ThreadPool {
public:
    /**
     * @brief 构造函数
     * @param threadCount 指定线程池的线程数量,默认是8个线程
     */
    explicit ThreadPool(size_t threadCount = 8) : pool_(std::make_shared<Pool>()) {
        //使用了std::make_shared<Pool>()来创建一个共享指针pool_
        //Pool结构体中包含一个互斥量mtx、
        //一个条件变量cond、
        //一个bool型标志isClosed
        //和一个std::queue<std::function<void()>>型的tasks队列
        assert(threadCount > 0);
        //创建threadCount个线程
        for (size_t i = 0; i < threadCount; i++) {
            //每个线程执行一个Lambda表达式，
            //Lambda表达式中创建一个std::unique_lockstd::mutex类型的locker对象来锁定互斥量mtx
            std::thread([pool = pool_] {
                std::unique_lock <std::mutex> locker(pool->mtx);
                while (true) {
                    //如果tasks队列不为空
                    if (!pool->tasks.empty()) {
                        auto task = std::move(pool->tasks.front());
                        pool->tasks.pop();
                        locker.unlock();
                        task();
                        locker.lock();
                    //如果tasks队列为空且线程池已关闭
                    } else if (pool->isClosed) break;   //退出循环
                    //进入条件变量cond的等待状态，等待其他线程的通知
                    else pool->cond.wait(locker);
                }
            }).detach();    //将线程设置为分离状态
        }
    }

    ThreadPool() = default;

    ThreadPool(ThreadPool &&) = default;

    /**
     * 析构函数，在销毁对象时关闭线程池
     */
    ~ThreadPool() {
        //检查线程池的智能指针是否指向了一个有效的 Pool 对象
        if (static_cast<bool>(pool_)) {
            {
                //先用锁保护线程池的互斥锁
                std::lock_guard <std::mutex> locker(pool_->mtx);
                //将线程池的 isClosed 标志设置为 true
                pool_->isClosed = true;
            }
            //唤醒所有被等待在条件变量上的线程
            pool_->cond.notify_all();
        }
        //这样，线程池就能够被安全地销毁，不会有任何线程在后台运行
    }

    /**
     * @brief 用于向线程池中添加一个任务
     * @tparam F 可以接受任意可调用对象
     * @param task
     */
    template<class F>
    void AddTask(F &&task) {
        {
            //获取了 ThreadPool 类中的共享指针 pool_ 中的互斥锁 mtx
            //并在其作用域内创建了一个 std::lock_guard 对象 locker
            std::lock_guard <std::mutex> locker(pool_->mtx);
            //使用 std::forward 将传递进来的任务转发到线程池的任务队列
            pool_->tasks.emplace(std::forward<F>(task));
        }
        //唤醒其中一个等待线程，使其从等待中醒来并取出任务执行
        pool_->cond.notify_one();
    }

private:
    struct Pool {
        std::mutex mtx;
        std::condition_variable cond;
        bool isClosed;
        std::queue <std::function<void()>> tasks;
    };
    std::shared_ptr <Pool> pool_;
};


#endif //THREADPOOL_H