#ifndef BLOCKQUEUE_H
#define BLOCKQUEUE_H

#include <mutex>
#include <deque>
#include <condition_variable>
#include <sys/time.h>

//基于std::deque的类模板,提供了许多用于在线程之间进行数据传输的函数
//它使用标准库mutex、condition_variable和sys/time.h来实现互斥和同步
template<class T>
class BlockDeque {
public:
    explicit BlockDeque(size_t MaxCapacity = 1000);

    ~BlockDeque();

    void clear();

    bool empty();

    bool full();

    void Close();

    size_t size();

    size_t capacity();

    T front();

    T back();

    void push_back(const T &item);

    void push_front(const T &item);

    bool pop(T &item);

    bool pop(T &item, int timeout);

    void flush();

private:
    std::deque <T> deq_;

    size_t capacity_;

    std::mutex mtx_;

    bool isClose_;

    std::condition_variable condConsumer_;

    std::condition_variable condProducer_;
};

/**
 * 构造函数,初始化BlockDeque类
 * @tparam T
 * @param MaxCapacity
 */
template<class T>
BlockDeque<T>::BlockDeque(size_t MaxCapacity) :capacity_(MaxCapacity) {
    assert(MaxCapacity > 0);
    isClose_ = false;
}

/**
 *
 * @tparam T
 */
template<class T>
BlockDeque<T>::~BlockDeque() {
    Close();
};

/**
 *
 * @tparam T
 */
template<class T>
void BlockDeque<T>::Close() {
    {
        std::lock_guard <std::mutex> locker(mtx_);
        deq_.clear();
        isClose_ = true;
    }
    condProducer_.notify_all();
    condConsumer_.notify_all();
};

/**
 *
 * @tparam T
 */
template<class T>
void BlockDeque<T>::flush() {
    condConsumer_.notify_one();
};

/**
 *
 * @tparam T
 */
template<class T>
void BlockDeque<T>::clear() {
    std::lock_guard <std::mutex> locker(mtx_);
    deq_.clear();
}

/**
 *
 * @tparam T
 * @return
 */
template<class T>
T BlockDeque<T>::front() {
    std::lock_guard <std::mutex> locker(mtx_);
    return deq_.front();
}

/**
 *
 * @tparam T
 * @return
 */
template<class T>
T BlockDeque<T>::back() {
    std::lock_guard <std::mutex> locker(mtx_);
    return deq_.back();
}

/**
 *
 * @tparam T
 * @return
 */
template<class T>
size_t BlockDeque<T>::size() {
    std::lock_guard <std::mutex> locker(mtx_);
    return deq_.size();
}

/**
 *
 * @tparam T
 * @return
 */
template<class T>
size_t BlockDeque<T>::capacity() {
    std::lock_guard <std::mutex> locker(mtx_);
    return capacity_;
}

/**
 * @brief 向BlockDeque容器中添加元素
 * @tparam T
 * @param item
 */
template<class T>
void BlockDeque<T>::push_back(const T &item) {
    //通过std::mutex锁定对象
    std::unique_lock <std::mutex> locker(mtx_);
    //检查当前容器的大小是否超过预定的容量
    while (deq_.size() >= capacity_) {  //超过
        //等待条件condProducer_满足
        condProducer_.wait(locker);
    }
    //如果没有超过，则将新的元素添加到容器中，最后唤醒条件condConsumer_。
    deq_.push_back(item);
    condConsumer_.notify_one();
}

/**
 *
 * @tparam T
 * @param item
 */
template<class T>
void BlockDeque<T>::push_front(const T &item) {
    std::unique_lock <std::mutex> locker(mtx_);
    while (deq_.size() >= capacity_) {
        condProducer_.wait(locker);
    }
    deq_.push_front(item);
    condConsumer_.notify_one();
}

/**
 *
 * @tparam T
 * @return
 */
template<class T>
bool BlockDeque<T>::empty() {
    std::lock_guard <std::mutex> locker(mtx_);
    return deq_.empty();
}

/**
 *
 * @tparam T
 * @return
 */
template<class T>
bool BlockDeque<T>::full() {
    std::lock_guard <std::mutex> locker(mtx_);
    return deq_.size() >= capacity_;
}

/**
 *
 * @tparam T
 * @param item
 * @return
 */
template<class T>
bool BlockDeque<T>::pop(T &item) {
    std::unique_lock <std::mutex> locker(mtx_);
    while (deq_.empty()) {
        condConsumer_.wait(locker);
        if (isClose_) {
            return false;
        }
    }
    item = deq_.front();
    deq_.pop_front();
    condProducer_.notify_one();
    return true;
}

/**
 * @brief
 * @tparam T
 * @param item
 * @param timeout
 * @return
 */
template<class T>
bool BlockDeque<T>::pop(T &item, int timeout) {
    std::unique_lock <std::mutex> locker(mtx_);
    while (deq_.empty()) {
        if (condConsumer_.wait_for(locker, std::chrono::seconds(timeout))
            == std::cv_status::timeout) {
            return false;
        }
        if (isClose_) {
            return false;
        }
    }
    item = deq_.front();
    deq_.pop_front();
    condProducer_.notify_one();
    return true;
}

#endif // BLOCKQUEUE_H