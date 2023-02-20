#include "epoller.h"

/**
 * 构造函数,创建一个Epoller对象
 * @param maxEvent 最多管理的事件数
 */
Epoller::Epoller(int maxEvent) : epollFd_(epoll_create(512)), events_(maxEvent) {
    //确保eppollFd_的值大于0,event_的大小大于0
    assert(epollFd_ >= 0 && events_.size() > 0);
}

/**
 * 析构函数,关闭epollFd_
 */
Epoller::~Epoller() {
    close(epollFd_);
}

/**
 * 添加一个新的文件描述符到epoll实例中
 * @param fd
 * @param events
 * @return
 */
bool Epoller::AddFd(int fd, uint32_t events) {
    //检查fd的有效性
    if (fd < 0) return false;
    epoll_event ev = {0};   //声明一个新的epoll_event结构实例
    //设置ev.data.fd和ev.events属性来确定服务的文件描述符和要监听的事件类型
    ev.data.fd = fd;        
    ev.events = events;
    return 0 == epoll_ctl(epollFd_, EPOLL_CTL_ADD, fd, &ev);
}

/**
 * 修改epoll监听的文件描述符fd
 * @param fd
 * @param events
 * @return
 */
bool Epoller::ModFd(int fd, uint32_t events) {
    if (fd < 0) 
    return false;
    epoll_event ev = {0};
    ev.data.fd = fd;
    ev.events = events;
    return 0 == epoll_ctl(epollFd_, EPOLL_CTL_MOD, fd, &ev);
}

/**
 * 从epoll中删除指定的文件描述符
 * @param fd
 * @return
 */
bool Epoller::DelFd(int fd) {
    if (fd < 0) 
    return false;
    epoll_event ev = {0};
    return 0 == epoll_ctl(epollFd_, EPOLL_CTL_DEL, fd, &ev);
}

/**
 * 等待与监听的socket上有事件发生,返回发生事件的数量
 * @param timeoutMs
 * @return
 */
int Epoller::Wait(int timeoutMs) {
    return epoll_wait(epollFd_, &events_[0], static_cast<int>(events_.size()), timeoutMs);
}

/**
 * 从events_中获取索引i指定的事件的文件描述符
 * @param i
 * @return
 */
int Epoller::GetEventFd(size_t i) const {
    assert(i < events_.size() && i >= 0);
    return events_[i].data.fd;
}

/**
 * 获取索引i指定的事件集合
 * @param i 必须大于等于0且小于events_size()
 * @return
 */
uint32_t Epoller::GetEvents(size_t i) const {
    assert(i < events_.size() && i >= 0);
    return events_[i].events;
}