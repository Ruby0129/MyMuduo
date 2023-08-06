#pragma once

#include "noncopyable.h"
#include "Timestamp.h"

#include <functional>
#include <memory>


class EventLoop;


/**
 * EventLoop包含一个Poller, Poller(Epoll)可以包含多个Channel(fd和event) => 一个EventLoop包含多个Channel
 * Channel通道: 封装类sockfd和感兴趣的event，如EPOLLIN、EPOLLOUT事件
 * 还绑定了poller返回的事件
*/
class Channel : noncopyable
{
public:
    // typedef std::function<void()> EventCallback;
    using EventCallback = std::function<void()>;
    using ReadEventCallback = std::function<void(Timestamp)>;

    Channel(EventLoop* loop, int fd);
    ~Channel();

    // fd得到poller通知以后，处理事件，调用相应的回调方法。
    void handleEvent(Timestamp receiveTime);

    // 设置回调函数对象 move左值转右值
    void setReadCallback(ReadEventCallback cb) { readCallback_ = std::move(cb); }
    void setWriteCallback(EventCallback cb) { writeCallback_ = std::move(cb); }
    void setCloseCallback(EventCallback cb) { closeCallback_ = std::move(cb); }
    void setErrorCallbacK(EventCallback cb) { errorCallback_ = std::move(cb); }

    // 防止当Channel被remove掉，channel还在执行回调操作
    // Channel的tie方法什么时候调用过?
    void tie(const std::shared_ptr<void>&);

    int fd() const { return fd_; }
    int events() const { return events_; }
    void set_revents(int revt) { revents_ = revt; }

    //设置fd相应的事件状态
    void enableReading() { events_ |= kReadEvent; update(); }
    void disableReading() { events_ &= ~kReadEvent; update(); }
    void enableWriting() { events_ |= kWriteEvent; update(); }
    void disableWriting() { events_ &= ~kWriteEvent; update(); }
    void disableAll() { events_ = kNoneEvent; update(); }

    // 返回fd当前的事件状态
    bool isNoneEvent() const { return events_ == kNoneEvent; }
    bool isWriting() const { return events_ & kWriteEvent; }
    bool isReading() const { return events_ & kReadEvent; }

    int index() { return index_; }
    void set_index(int idx) { index_ = idx; }

    // one loop per thread
    EventLoop* ownerLoop() { return loop_; }
    void remove();

private:
    void update();
    void handleEventWithGuard(Timestamp receiveTime);

private:
    // 当前fd的状态
    static const int kNoneEvent;
    static const int kReadEvent;
    static const int kWriteEvent;

    EventLoop* loop_; // 事件循环
    const int fd_; // fd, Poller监听的对象
    int events_; // 注册fd感兴趣的事件
    int revents_; // Poller返回的具体发生的事件
    int index_;

    // weak_ptr多线程里监听所观察资源的生存状态，可以把weak_ptr提升成shared_ptr，提升成功可以访问
    std::weak_ptr<void> tie_;
    bool tied_;
    
    // 因为Channel里面能够获知fd最终发生的具体事件revents，所以它负责调用具体事件的回调操作
    ReadEventCallback readCallback_;
    EventCallback writeCallback_;
    EventCallback closeCallback_;
    EventCallback errorCallback_;
};