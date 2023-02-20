#include "heaptimer.h"

/**
 * @brief 向上调整堆,heap_为存储堆节点的容器,
 * heap_[i]表示第i个节点,i为当前节点,j为当前节点的父节点,
 * SwapNode_为交换两个节点的函数
 * 该函数首先计算当前节点的父节点位置,
 * 然后比较当前节点和父节点的值,
 * 如果父节点值小于当前节点值,
 * 则结束循环，否则交换当前节点和父节点的值,
 * 并将当前节点的位置更新为父节点的位置,
 * 继续循环,直至当前节点的父节点不存在
 * @param i
 */
void HeapTimer::siftup_(size_t i) {
    assert(i >= 0 && i < heap_.size());
    size_t j = (i - 1) / 2;
    while (j >= 0) {
        if (heap_[j] < heap_[i]) { break; }
        SwapNode_(i, j);
        i = j;
        j = (i - 1) / 2;
    }
}

/**
 * @brief 交换堆中两个节点的位置
 * @param i
 * @param j
 */
void HeapTimer::SwapNode_(size_t i, size_t j) {
    //首先利用断言验证i和j的有效性,即两个索引必须都在堆的范围内
    assert(i >= 0 && i < heap_.size());
    assert(j >= 0 && j < heap_.size());
    //调用标准库函数swap交换堆中的两个节点
    std::swap(heap_[i], heap_[j]);
    //更新ref_的映射关系
    ref_[heap_[i].id] = i;
    ref_[heap_[j].id] = j;
}

/**
 * @brief 实现了下滤操作，用于在堆中插入一个新元素时使堆有序
 * @param index 要下滤的结点的索引
 * @param n 堆中元素的数量
 * @return 用于判断是否发生了交换,以此来确定是否需要继续下滤
 */
bool HeapTimer::siftdown_(size_t index, size_t n) {
    //通过断言检查参数的有效性
    assert(index >= 0 && index < heap_.size());
    assert(n >= 0 && n <= heap_.size());
    //将i设置为index，j设置为i的左孩子索引
    size_t i = index;
    size_t j = i * 2 + 1;
    while (j < n) {
        //判断j是否越界
        if (j + 1 < n && heap_[j + 1] < heap_[j])
            j++;    //若是则将j加1，使j指向更小的孩子
        //若未越界，则判断右孩子是否比左孩子小
        if (heap_[i] < heap_[j])
            break;  //若是则退出循环
        //若不是，则将当前结点和更小的孩子交换，
        //再把i和j分别设置为交换后的孩子的索引，重复循环
        SwapNode_(i, j);
        i = j;
        j = i * 2 + 1;
    }
    return i > index;
}

/**
 * @brief 向堆中添加新节点或更新已有节点
 * @param id
 * @param timeout
 * @param cb
 */
void HeapTimer::add(int id, int timeout, const TimeoutCallBack &cb) {
    assert(id >= 0);
    size_t i;
    //若id不存在，则新建一个节点并插入到堆的尾部，然后调用siftup_函数对堆进行调整
    if (ref_.count(id) == 0) {
        /* 新节点：堆尾插入，调整堆 */
        i = heap_.size();
        ref_[id] = i;
        heap_.push_back({id, Clock::now() + MS(timeout), cb});
        siftup_(i);
    } else {
        //若id存在，则更新节点的超时时间，调用siftdown_函数对堆进行调整
        /* 已有结点：调整堆 */
        i = ref_[id];
        heap_[i].expires = Clock::now() + MS(timeout);
        heap_[i].cb = cb;
        //若调整失败则调用siftup_函数进行调整
        if (!siftdown_(i, heap_.size())) {
            siftup_(i);
        }
    }
}

/**
 * @brief 堆定时器的核心功能，
 * 即根据指定的id从堆定时器中删除结点并触发回调函数
 * 其中heap_表示定时器堆，ref_表示id到定时器堆索引的映射，
 * TimerNode表示定时器结点，cb表示回调函数，
 * del_表示定时器结点删除函数
 * @param id
 */
void HeapTimer::doWork(int id) {
    /* 删除指定id结点，并触发回调函数 */
    //检查定时器堆是否为空
    if (heap_.empty() || ref_.count(id) == 0) {
        return;
    }
    size_t i = ref_[id];        //从ref_中获取该id对应的定时器堆索引
    TimerNode node = heap_[i];  //从heap_中取出对应的定时器结点
    node.cb();  //触发回调函数
    del_(i);    //删除该定时器结点
}

/**
 * @brief 删除堆中指定位置的结点的功能
 * @param index
 */
void HeapTimer::del_(size_t index) {
    /* 删除指定位置的结点 */
    //通过断言语句确保堆不为空并且指定位置有效
    assert(!heap_.empty() && index >= 0 && index < heap_.size());
    /* 将要删除的结点换到队尾，然后调整堆 */
    size_t i = index;
    size_t n = heap_.size() - 1;
    assert(i <= n);
    if (i < n) {
        SwapNode_(i, n);
        if (!siftdown_(i, n)) {
            siftup_(i);
        }
    }
    /* 队尾元素删除 */
    //从ref_和heap_中删除最后一个结点
    ref_.erase(heap_.back().id);
    heap_.pop_back();
}

/**
 * @brief 调整指定id的结点
 * @param id 要调整的结点的id
 * @param timeout 调整后的超时时间
 */
void HeapTimer::adjust(int id, int timeout) {
    /* 调整指定id的结点 */
    //通过断言检查heap_数组和ref_map是否为空
    assert(!heap_.empty() && ref_.count(id) > 0);
    //用参数timeout来设定超时时间
    heap_[ref_[id]].expires = Clock::now() + MS(timeout);;
    //调用siftdown_函数来调整堆，以确保堆的性质依然成立
    siftdown_(ref_[id], heap_.size());
}

/**
 * @brief 每次调用tick()函数时检查堆中的节点
 * 如果有节点的过期时间比当前时间要早，
 * 就将这个节点的回调函数调用，
 * 然后将这个节点从堆中弹出
 */
void HeapTimer::tick() {
    /* 清除超时结点 */
    if (heap_.empty()) {
        return;
    }
    while (!heap_.empty()) {
        TimerNode node = heap_.front();
        if (std::chrono::duration_cast<MS>(node.expires - Clock::now()).count() > 0) {
            break;
        }
        node.cb();
        pop();
    }
}

/**
 * @brief 从堆中删除第一个元素，即堆顶元素
 */
void HeapTimer::pop() {
    //使用assert断言函数，确保堆不为空
    assert(!heap_.empty());
    //删除堆顶元素
    del_(0);
}

/**
 * @brief 清空ref_和heap_两个成员变量
 */
void HeapTimer::clear() {
    ref_.clear();
    heap_.clear();
}

/**
 * @brief 获取下一个定时器到期时间
 * @return
 */
int HeapTimer::GetNextTick() {
    tick(); //更新堆中的定时器
    size_t res = -1;
    //判断堆是否为空
    if (!heap_.empty()) {
        //将最早到期定时器的到期时间与当前时间相减转换为毫秒
        res = std::chrono::duration_cast<MS>(heap_.front().expires - Clock::now()).count();
        if (res < 0) { res = 0; }
    }
    //返回计算结果
    return res;
}