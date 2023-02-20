#ifndef HEAP_TIMER_H
#define HEAP_TIMER_H

#include <queue>
#include <unordered_map>
#include <time.h>
#include <algorithm>
#include <arpa/inet.h>
#include <functional>
#include <assert.h>
#include <chrono>
#include "../log/log.h"

typedef std::function<void()> TimeoutCallBack;
typedef std::chrono::high_resolution_clock Clock;
typedef std::chrono::milliseconds MS;
typedef Clock::time_point TimeStamp;

//用于存储定时任务的ID、到期时间、回调函数
struct TimerNode {
    int id;
    TimeStamp expires;
    TimeoutCallBack cb;

    bool operator<(const TimerNode &t) {
        return expires < t.expires;
    }
};

//堆定时器
//实现事实任务的管理
//可以添加和移除任务、调整任务的到期时间
class HeapTimer {
public:
    HeapTimer() { heap_.reserve(64); }

    ~HeapTimer() { clear(); }

    void adjust(int id, int newExpires);

    void add(int id, int timeOut, const TimeoutCallBack &cb);

    void doWork(int id);

    void clear();

    void tick();

    void pop();

    int GetNextTick();

private:
    void del_(size_t i);

    void siftup_(size_t i);

    bool siftdown_(size_t index, size_t n);

    void SwapNode_(size_t i, size_t j);

    std::vector <TimerNode> heap_;

    std::unordered_map<int, size_t> ref_;
};

#endif //HEAP_TIMER_H