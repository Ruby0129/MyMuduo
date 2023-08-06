# MyMuduo

项目描述: 参考 Muduo 网络库使用 C++11 实现的基于 Reactor 模型的多线程网络库。
主要工作：
1.底层使用 Epoll 和 LT 模式下的非阻塞 IO 复用模型，并且结合非阻塞 IO 实现主从 Reactor 模型；
2.采用 one loop per thread 线程模型，并向上封装线程池避免线程创建和销毁带来的性能开销；
3.采用 eventfd 作为事件通知描述符，方便高效派发事件到其他线程执行异步任务；
4.使用智能指针管理内存，减小内存泄露风险；
5.利用信号量、互斥锁及条件变量实现的线程同步机制；
6.封装 HTTP 模块，利用主从状态机解析 HTTP 请求报文,支持解析 GET 和 POST 请求,实现处理静态资源的请求



编译生成libmymuduo.so库

```shell
# 进入项目文件夹运行脚本文件
./autobuild.sh
```

example文件夹下分别为HTTP服务器和回声服务器例程



**MyMuduo执行流程:**
⚪ 用户创建TcpServer 和 mainLoop:
	1.TcpServer绑定mainLoop, TcpServer拥有成员变量Acceptor。Acceptor同样是由mianLoop创建的，同时包含listenAddr。
	2.TcpServer执行start()函数，TcpServer的start()会调用EventLoopThreadPool的start()函数并给出线程创建回调函数threadInitCallback_。_

```c++
// in testserver.cc
EventLoop loop;
InetAddress addr(8000);
EchoServer server(&loop, addr, "EchoServer-01"); // Acceptor non-blocking listenfd  create bind 
server.start(); // listen  loopthread  listenfd => acceptChannel => mainLoop =>
loop.loop(); // 启动mainLoop的底层Poller

...
// in TcpServer.cc
void TcpServer::start()
{
    if (started_++ == 0) // 防止一个TcpServer对象被start多次
    {
        threadPool_->start(threadInitCallback_); // 启动底层的loop线程池
        loop_->runInLoop(std::bind(&Acceptor::listen, acceptor_.get()));
    }
}
```

​	  EventLoopThreadPool的start()函数会创建用户给出线程数数量的EventLoopThread。

```c++
// in EventLoopThreadPool.cc
// EventLoopThreadPool::start
void EventLoopThreadPool::start(const ThreadInitCallback &cb);

for (int i = 0; i < numThreads_; ++i)
{
	char buf[name_.size() + 32];
	snprintf(buf, sizeof buf, "%s%d", name_.c_str(), i);
	EventLoopThread *t = new EventLoopThread(cb, buf);
	...
}
```

​	  EventLoopThread被初始化时被给到参数threadInitCallback和name。每个EventLoopThread都包含一个Thread和EventLoop(subLoop)。

```c++
// in EventLoopThread.h
// an EventLoopThread include an EventLoop and a Thread.
EventLoop *loop_;
Thread thread_;
// in EventLoopThread.cc
// EventLoopThread constructor
EventLoopThread::EventLoopThread(const ThreadInitCallback &cb, 
        const std::string &name)
        : loop_(nullptr)
        , exiting_(false)
        , thread_(std::bind(&EventLoopThread::threadFunc, this), name)
        , mutex_()
        , cond_()
        , callback_(cb)
{
}
```

​	  所有EventLoopThread在被EventLoopThreadPool创建并加入threads数组的同时就开始运行自己的startloop函数。

```c++
// in EventLoopThreadPool.cc
// EventLoopThreadPool::start
threads_.push_back(std::unique_ptr<EventLoopThread>(t));
loops_.push_back(t->startLoop()); // 底层创建线程，绑定一个新的EventLoop，并返回该loop的地址
```

​	3.Thread是封装了C++线程类的类，防止创建线程时线程就开始执行了，EventLoopThread提供startLoop函数来启动线程。

```c++
// in Thread.cc
// Thread constructor
Thread::Thread(ThreadFunc func, const std::string &name)
    : started_(false)
    , joined_(false)
    , tid_(0)
    , func_(std::move(func))
    , name_(name)
{
    setDefaultName();
}

void Thread::start()  // 一个Thread对象，记录的就是一个新线程的详细信息
{
    started_ = true;
    sem_t sem;
    sem_init(&sem, false, 0);

    // 开启线程
    // start里才会真正创建线程
    thread_ = std::shared_ptr<std::thread>(new std::thread([&](){
        // 获取线程的tid值
        tid_ = CurrentThread::tid();
        sem_post(&sem);
        // 开启一个新线程，专门执行该线程函数
        func_(); 
    }));

    // 这里必须等待获取上面新创建的线程的tid值
    sem_wait(&sem);
}
```

​	  startLoop会调用Thread的start函数来真正的初始化线程，Thread创建时需要threadFunc函数作为线程执行函数。

```c++
// in EventLoopThread.cc
EventLoop* EventLoopThread::startLoop()
{
    thread_.start(); // 启动底层的新线程

    EventLoop *loop = nullptr; // 创建了subLoop
    {
        std::unique_lock<std::mutex> lock(mutex_);
        // 等待threadFunc创建EventLoop
        while ( loop_ == nullptr )
        {
            cond_.wait(lock);
        }
        loop = loop_;
    }
    return loop;
}
```

​	  threadFunc的作用就是创建一个独立的eventloop(这就是subLoop)，同时threadFunc将subLoop的指针赋给EventLoopThread从而和Thread一一对应,然后执行subLoop的loop()函数。

```c++
// 下面这个方法，是在单独的新线程里面运行的
void EventLoopThread::threadFunc()
{
    EventLoop loop; // 创建一个独立的eventloop，和上面的线程是一一对应的，one loop per thread

    if (callback_)
    {
        callback_(&loop);
    }

    {
        std::unique_lock<std::mutex> lock(mutex_);
        loop_ = &loop;
        // 唤醒阻塞的startLoop函数，等待loop_真正获取值之后
        cond_.notify_one();
    }

    loop.loop(); // EventLoop loop  => Poller.poll
    std::unique_lock<std::mutex> lock(mutex_);
    // loop.loop()函数执行完之后，将loop_置空 （实际上若loop的quit不为true，loop会一直执行loop.loop());
    loop_ = nullptr;
}
```

​	  Thread的start函数运行后，threadFunc也就开始运行，所以SubLoop在这时被创建并开始运行loop函数，同时在EventLoopThread的startLoop函数里赋值给EventLoopThread的成员变量loop。EventLoop通过默认构造函数构造，会初始化一个默认的Poller poller_，通过createEventfd()创建wakeupFd和wakeupChannel，通过wakeupChannel_->enableReading()，被注册到Poller，希望Poller监听读事件。

```c++
// in EventLoop.cc
EventLoop::EventLoop()
    : looping_(false)
    , quit_(false)
    , callingPendingFunctors_(false)
    , threadId_(CurrentThread::tid())
    , poller_(Poller::newDefaultPoller(this))
    , wakeupFd_(createEventfd())
    , wakeupChannel_(new Channel(this, wakeupFd_))
{
    LOG_DEBUG("EventLoop created %p in thread %d \n", this, threadId_);
    if (t_loopInThisThread)
    {
        LOG_FATAL("Another EventLoop %p exists in this thread %d \n", t_loopInThisThread, threadId_);
    }
    else
    {
        t_loopInThisThread = this;
    }

    // 设置wakeupfd的事件类型以及发生事件后的回调操作
    wakeupChannel_->setReadCallback(std::bind(&EventLoop::handleRead, this));
    // 每一个eventloop都将监听wakeupchannel的EPOLLIN读事件了
    wakeupChannel_->enableReading();
}
```

​	4.各个SubLoop的loop函数运行后，会运行一个循环，调用各自所持有Poller的poll函数，期望Poller返回所有发生事件的channels(activeChannels)。

```c++
// 开启事件循环
void EventLoop::loop()
{
    looping_ = true;
    quit_ = false;

    LOG_INFO("EventLoop %p start looping \n", this);

    while(!quit_)
    {
        activeChannels_.clear();
        // 监听两类fd   一种是client的fd，一种wakeupfd
        pollReturnTime_ = poller_->poll(kPollTimeMs, &activeChannels_);
        for (Channel *channel : activeChannels_)
        {
            // Poller监听哪些channel发生事件了，然后上报给EventLoop，通知channel处理相应的事件
            channel->handleEvent(pollReturnTime_);
        }
        // 执行当前EventLoop事件循环需要处理的回调操作
        /**
         * IO线程 mainLoop accept fd《=channel subloop
         * mainLoop 事先注册一个回调cb（需要subloop来执行）    wakeup subloop后，执行下面的方法，执行之前mainloop注册的cb操作
         */ 
        doPendingFunctors();
    }

    LOG_INFO("EventLoop %p stop looping. \n", this);
    looping_ = false;
}
```

​	  poll函数会调用epoll_wait函数来监听事件。

```c++
// in EPollPoller.cc
Timestamp EPollPoller::poll(int timeoutMs, ChannelList *activeChannels)
{
    LOG_INFO("func=%s => fd total count:%lu \n", __FUNCTION__, channels_.size());

    int numEvents = ::epoll_wait(epollfd_, &*events_.begin(), static_cast<int>(events_.size()), timeoutMs);
    int saveErrno = errno;
    Timestamp now(Timestamp::now());

    if (numEvents > 0)
    {
        LOG_INFO("%d events happened \n", numEvents);
        fillActiveChannels(numEvents, activeChannels);
        if (numEvents == events_.size())
        {
            events_.resize(events_.size() * 2);
        }
    }
    else if (numEvents == 0)
    {
        LOG_DEBUG("%s timeout! \n", __FUNCTION__);
    }
    else
    {
        if (saveErrno != EINTR)
        {
            errno = saveErrno;
            LOG_ERROR("EPollPoller::poll() err!");
        }
    }
    return now;
}

// 填写活跃的连接
void EPollPoller::fillActiveChannels(int numEvents, ChannelList *activeChannels) const
{
    for (int i=0; i < numEvents; ++i)
    {
        Channel *channel = static_cast<Channel*>(events_[i].data.ptr);
        channel->set_revents(events_[i].events);
        activeChannels->push_back(channel); // EventLoop就拿到了它的poller给它返回的所有发生事件的channel列表了
    }
}
```

​	  因为此时，还没有其他线程调用wake函数唤醒subLoop(即没有线程发送数据到wakefd)所以epoll_wait会阻塞，poll函数也就被阻塞。即各个SubLoop等待被唤醒。
​	5.此时TcpServer中的线程，即MainLoop所在的线程。MainLoop会执行自己的runInLoop函数，调用Acceptor中的listen函数。

```c++
// in TcpServer.cc
// void TcpServer::start();
loop_->runInLoop(std::bind(&Acceptor::listen, acceptor_.get()));
```

​	  listen函数会设置Acceptor中的acceptChannel_的读事件。_

```c++
// in Acceptor.cc
void Acceptor::listen()
{
    listenning_ = true;
    acceptSocket_.listen(); // listen
    acceptChannel_.enableReading(); // acceptChannel_ => Poller
}
```

​	  前面说到TcpServer构造函数里会初始化一个Acceptor acceptor,Acceptor初始化时包含mainLoop, listenAddr。同时会设置acceptor的新连接到来时的回调函数newConnectionCallback。

```c++
// in Acceptor.cc 
// Acceptor constructor
Acceptor::Acceptor(EventLoop *loop, const InetAddress &listenAddr, bool reuseport)
    : loop_(loop)
    , acceptSocket_(createNonblocking()) // socket
    , acceptChannel_(loop, acceptSocket_.fd())
    , listenning_(false)
{
    acceptSocket_.setReuseAddr(true);
    acceptSocket_.setReusePort(true);
    acceptSocket_.bindAddress(listenAddr); // bind
    // TcpServer::start() Acceptor.listen  有新用户的连接，要执行一个回调（connfd=》channel=》subloop）
    // 即acceptChannel有读事件时，执行Acceptor的handleRead函数
    acceptChannel_.setReadCallback(std::bind(&Acceptor::handleRead, this));
 }
```

 Acceptor的构造函数里会先调用自身的成员函数createNonblocking创建非阻塞sockfd用来初始化acceptSocket, 同时把这个sockfd打包成acceptChannel交由MainLoop管理。acceptSocket会进行相关的setReuseAddr、setReusePort、bindAddress(listenAddr)等操作，然后设置acceptChannel发生读事件的回调函数。 这个回调函数就是Accepor::handleRead。即当mainLoop的Poller监听到acceptSocket中的sockfd有读事件发生时，就会调用Accepor::handleRead。handleRead通过acceptSocket的accept函数生成connfd，然后调用新连接到来时的回调函数newConnectionCallback_。

```c++
// in Acceptor.cc
void Acceptor::handleRead()
{
    InetAddress peerAddr;
    int connfd = acceptSocket_.accept(&peerAddr);
    if (connfd >= 0)
    {
        if (newConnectionCallback_)
        {
            newConnectionCallback_(connfd, peerAddr);
        }
        else
        {
            ::close(connfd);
        }
    }
    else
    {
        LOG_ERROR("%s:%s:%d accept err:%d \n", __FILE__, __FUNCTION__, __LINE__, errno);
        if (errno == EMFILE)
        {
            LOG_ERROR("%s:%s:%d sockfd reached limit! \n", __FILE__, __FUNCTION__, __LINE__);
        }
    }
}
```

newConnectionCallback是由TcpServer设置给Acceptor的，即函数newConnection。

```cpp
// in TcpServer.constructor
// 当有先用户连接时，会执行TcpServer::newConnection回调
acceptor_->setNewConnectionCallback(std::bind(&TcpServer::newConnection, this, std::placeholders::_1, std::placeholders::_2));
```

newConnection需要两个参数(一个是上面accept函数得到的返回值connfd， 一个是accept函数的传出参数peerAddr远端地址)。它的作用就是通过轮询算法从threadPool中取出一个ioLoop。

```c++
// 有一个新的客户端的连接，acceptor会执行这个回调操作
void TcpServer::newConnection(int sockfd, const InetAddress &peerAddr)
{
    // 轮询算法，选择一个subLoop，来管理channel
    EventLoop *ioLoop = threadPool_->getNextLoop(); 
    char buf[64] = {0};
    snprintf(buf, sizeof buf, "-%s#%d", ipPort_.c_str(), nextConnId_);
    ++nextConnId_;
    std::string connName = name_ + buf;

    LOG_INFO("TcpServer::newConnection [%s] - new connection [%s] from %s \n",
        name_.c_str(), connName.c_str(), peerAddr.toIpPort().c_str());

    // 通过sockfd获取其绑定的本机的ip地址和端口信息
    sockaddr_in local;
    ::bzero(&local, sizeof local);
    socklen_t addrlen = sizeof local;
    if (::getsockname(sockfd, (sockaddr*)&local, &addrlen) < 0)
    {
        LOG_ERROR("sockets::getLocalAddr");
    }
    InetAddress localAddr(local);

    // 根据连接成功的sockfd，创建TcpConnection连接对象
    TcpConnectionPtr conn(new TcpConnection(
                            ioLoop,
                            connName,
                            sockfd,   // Socket Channel
                            localAddr,
                            peerAddr));
    connections_[connName] = conn;
    // 下面的回调都是用户设置给TcpServer=>TcpConnection=>Channel=>Poller=>notify channel调用回调
    conn->setConnectionCallback(connectionCallback_);
    conn->setMessageCallback(messageCallback_);
    conn->setWriteCompleteCallback(writeCompleteCallback_);

    // 设置了如何关闭连接的回调   conn->shutDown()
    conn->setCloseCallback(
        std::bind(&TcpServer::removeConnection, this, std::placeholders::_1)
    );

    // 直接调用TcpConnection::connectEstablished
    ioLoop->runInLoop(std::bind(&TcpConnection::connectEstablished, conn));
}
```

先通过connfd获取其绑定的本机ip地址和端口信息。生成localAddr。
然后初始化一个新的TcpConnection(由智能指针TcpConnectionPtr conn管理)。然后给conn设置各种回调函数(connectionCallback, messageCallback, writeCompleteCallback, TcpServer::removeConnection(conn->setCloseCallback))。TcpConnection初始化时需要一个EventLoop(即ioLoop)，同时将state设置为kConnecting，表示状态正在连接中，初始化一个新的Channel(打包connfd和ioLoop),设置高水位标志。

```c++
// in TcpConnection.c
// TcpConnection constructor
TcpConnection::TcpConnection(EventLoop *loop, 
                const std::string &nameArg, 
                int sockfd,
                const InetAddress& localAddr,
                const InetAddress& peerAddr)
    : loop_(CheckLoopNotNull(loop))
    , name_(nameArg)
    , state_(kConnecting)
    , reading_(true)
    , socket_(new Socket(sockfd))
    , channel_(new Channel(loop, sockfd))
    , localAddr_(localAddr)
    , peerAddr_(peerAddr)
    , highWaterMark_(64*1024*1024) // 64M
{
    // 下面给channel设置相应的回调函数，poller给channel通知感兴趣的事件发生了，channel会回调相应的操作函数
    channel_->setReadCallback(
        std::bind(&TcpConnection::handleRead, this, std::placeholders::_1)
    );
    channel_->setWriteCallback(
        std::bind(&TcpConnection::handleWrite, this)
    );
    channel_->setCloseCallback(
        std::bind(&TcpConnection::handleClose, this)
    );
    channel_->setErrorCallback(
        std::bind(&TcpConnection::handleError, this)
    );

    LOG_INFO("TcpConnection::ctor[%s] at fd=%d\n", name_.c_str(), sockfd);
    socket_->setKeepAlive(true);
}
```

同时给该Channel设置相应的各个事件回调函数，设置socket为KeepAlive，这些回调函数均封装在TcpConnection中(handleRead， handleWrite, handleClose, handleError)。可以看出TcpConnection给SubLoop上的Channel设置了各个事件相应的处理函数。

```c++
// handleRead: 读取套接字中的数据到inputBuffer, 然后调用messageCallback(给用户传入的有可读事件的回调函数)。
void TcpConnection::handleRead(Timestamp receiveTime)
{
    int savedErrno = 0;
    ssize_t n = inputBuffer_.readFd(channel_->fd(), &savedErrno);
    if (n > 0)
    {
        // 已建立连接的用户，有可读事件发生了，调用用户传入的回调操作onMessage
        messageCallback_(shared_from_this(), &inputBuffer_, receiveTime);
    }
    else if (n == 0)
    {
        handleClose();
    }
    else
    {
        errno = savedErrno;
        LOG_ERROR("TcpConnection::handleRead");
        handleError();
    }
}

// handleWrite: 将outputBuffer中的数据往套接字写，写完后，调用TcpConnection的成员变量ioLoop的queueInLoop函数执行回调操作writeCompleteCallback。
void TcpConnection::handleWrite()
{
    if (channel_->isWriting())
    {
        int savedErrno = 0;
        ssize_t n = outputBuffer_.writeFd(channel_->fd(), &savedErrno);
        if (n > 0)
        {
            outputBuffer_.retrieve(n);
            if (outputBuffer_.readableBytes() == 0)
            {
                channel_->disableWriting();
                if (writeCompleteCallback_)
                {
                    // 唤醒loop_对应的thread线程，执行回调
                    loop_->queueInLoop(
                        std::bind(writeCompleteCallback_, shared_from_this())
                    );
                }
                if (state_ == kDisconnecting)
                {
                    shutdownInLoop();
                }
            }
        }
        else
        {
            LOG_ERROR("TcpConnection::handleWrite");
        }
    }
    else
    {
        LOG_ERROR("TcpConnection fd=%d is down, no more writing \n", channel_->fd());
    }
}

//handleClose: 将channel所关注的事件设置为None (channel->disableAll), 设置state，执行connectionCallback和closeCallback_.
// poller => channel::closeCallback => TcpConnection::handleClose
void TcpConnection::handleClose()
{
    LOG_INFO("TcpConnection::handleClose fd=%d state=%d \n", channel_->fd(), (int)state_);
    setState(kDisconnected);
    channel_->disableAll();

    TcpConnectionPtr connPtr(shared_from_this());
    connectionCallback_(connPtr); // 执行连接关闭的回调
    closeCallback_(connPtr); // 关闭连接的回调  执行的是TcpServer::removeConnection回调方法
}

void TcpConnection::handleError()
{
    int optval;
    socklen_t optlen = sizeof optval;
    int err = 0;
    if (::getsockopt(channel_->fd(), SOL_SOCKET, SO_ERROR, &optval, &optlen) < 0)
    {
        err = errno;
    }
    else
    {
        err = optval;
    }
    LOG_ERROR("TcpConnection::handleError name:%s - SO_ERROR:%d \n", name_.c_str(), err);
}
```

新的TcpConnection conn创建后，其上的Channel还没有和ioLoop所绑定，且对任何事件还不感兴趣(events = 0)。之后调用ioLoop->runInLoop()，要知道我们此时是通过TcpConnection所在线程调用ioLoop的runInLoop函数的，此时ioLoop并不在其所在线程上。所以runInLoop会先调用queueInLoop把TcpConnection::connectEstablished回调函数加入ioLoop的pendingFunctors中，然后wakeup唤醒ioLoop。

```c++
// in EventLoop.cc
// 在当前loop中执行cb
void EventLoop::runInLoop(Functor cb)
{
    if (isInLoopThread()) // 在当前的loop线程中，执行cb
    {
        cb();
    }
    else // 在非当前loop线程中执行cb , 就需要唤醒loop所在线程，执行cb
    {
        queueInLoop(cb);
    }
}
// 把cb放入队列中，唤醒loop所在的线程，执行cb
void EventLoop::queueInLoop(Functor cb)
{
    {
        std::unique_lock<std::mutex> lock(mutex_);
        pendingFunctors_.emplace_back(cb);
    }

    // 唤醒相应的，需要执行上面回调操作的loop的线程了
    // || callingPendingFunctors_的意思是：当前loop正在执行回调，但是loop又有了新的回调
    if (!isInLoopThread() || callingPendingFunctors_) 
    {
        wakeup(); // 唤醒loop所在线程
    }
}
```

 此时ioLoop从poll中解除阻塞获得了wakeupChannel的读事件，然后又会调用doPendingFunctors函数执行pendingFunctors里的回调。
  connectEstablished会设置conn的状态为kConnected,同时调用channel->enableReading。

```cpp
// in TcpConnection.cc
// 连接建立
void TcpConnection::connectEstablished()
{
    setState(kConnected);
    channel_->tie(shared_from_this());
    channel_->enableReading(); // 向poller注册channel的epollin事件

    // 新连接建立，执行回调
    connectionCallback_(shared_from_this());
}
```

  此时代表conn上的channel对读事件感兴趣，并已经通过ioLoop注册给Poller了，Poller便会开始监听connfd的读事件(有没有数据传过来)。同时调用新连接建立的回调connectionCallback_。

6.所以在TcpServer的start函数中，当mainLoop执行runInLoop时，mainLoop会执行Acceptor::listen函数，这时mainLoop上的Poller可以监听客户端的连接了。
此时自行mainLoop.loop函数让Poller开始监听连接，返回活跃事件给mainLoop来进行处理。有新连接便会调用new Connection创建新连接。
此时当各个ioLoop上Poller监听到各种事件便会返回activeChannels_给ioLoop, ioLoop在调用各个channel->handleEvent() 处理各种事件。

⚪ 回调函数传递:
		用户读取数据


```c++
server_.setMessageCallback(
	std::bind(&EchoServer::onMessage, this,
	std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)
	);
	
	conn->setMessageCallback(messageCallback_);

	void TcpConnection::handleRead(Timestamp receiveTime)
	{
		...
		messageCallback_(shared_from_this(), &inputBuffer_, receiveTime);
	}

	channel_->setReadCallback(
		std::bind(&TcpConnection::handleRead, this, std::placeholders::_1)
	);
```

所以 用户定义信息到达时的函数（有数据可读) -> TcpServer Bind -> TcpConnection Bind -> TcpConnection::handleRead -> channel Bind handleRead
所以当connfd上有数据可读时，poller返回给subLoop通知channel处理，channel调用的时TcpConnecction里的handleRead先将数据读到应用缓冲区Buffer，然后TcpConnecction里的handleRead又会调用用户的OnMessage函数（用户自定义 如将Buf里的数据取出)。



用户想要发送数据:

调用TcpConnection的Send()方法。

```cpp
conn->send(msg);
// TcpConnection.cc send(msg)
void TcpConnection::send(const std::string &buf)
{
    if (state_ == kConnected)
    {
        if (loop_->isInLoopThread())
        {
            sendInLoop(buf.c_str(), buf.size());
        }
        else
        {
            loop_->runInLoop(std::bind(
                &TcpConnection::sendInLoop,
                this,
                buf.c_str(),
                buf.size()
            ));
        }
    }
}
```

如果是在当前线程则直接调用sendInLoop, 否则唤醒线程调用sendInLoop(loop_->runInLoop(std::bind(&TcpConnection::sendInLoop...)))
sendInLoop() 会先直接往connfd写数据，如果写不完，则把数据写入应用层缓冲区，且设置了水位回调，写时要保证连接没有shutdown。_

*Poller监听写事件时，只要可以往channel->fd()写数据会一直通知。
先直接向channel->fd() 写数据，更新还剩下的数据remaining，如果已经写完就不需要在设置EPOLLOUT事件了。*
*如果没写完就先写入应用层缓冲区outBuffer(先判断高水位)，然后设置channel EPOLLOUT事件(channel-enableWriting)。*
*这样下次Poller返回写事件时，channel会调用handleWrite函数(最终调用TcpConnection里的handleWrite函数从outBuffer向conn_fd写数据)。应用缓冲区数据写完，则会调用writeCompleteCallback*

```cpp
/**
 * 发送数据  应用写的快， 而内核发送数据慢， 需要把待发送数据写入缓冲区， 而且设置了水位回调
 */ 
void TcpConnection::sendInLoop(const void* data, size_t len)
{
    ssize_t nwrote = 0;
    size_t remaining = len;
    bool faultError = false;

    // 之前调用过该connection的shutdown，不能再进行发送了
    if (state_ == kDisconnected)
    {
        LOG_ERROR("disconnected, give up writing!");
        return;
    }

    // 表示channel_第一次开始写数据，而且缓冲区没有待发送数据
    if (!channel_->isWriting() && outputBuffer_.readableBytes() == 0)
    {
        nwrote = ::write(channel_->fd(), data, len);
        if (nwrote >= 0)
        {
            remaining = len - nwrote;
            if (remaining == 0 && writeCompleteCallback_)
            {
                // 既然在这里数据全部发送完成，就不用再给channel设置epollout事件了
                loop_->queueInLoop(
                    std::bind(writeCompleteCallback_, shared_from_this())
                );
            }
        }
        else // nwrote < 0
        {
            nwrote = 0;
            if (errno != EWOULDBLOCK)
            {
                LOG_ERROR("TcpConnection::sendInLoop");
                if (errno == EPIPE || errno == ECONNRESET) // SIGPIPE  RESET
                {
                    faultError = true;
                }
            }
        }
    }

    // 说明当前这一次write，并没有把数据全部发送出去，剩余的数据需要保存到缓冲区当中，然后给channel
    // 注册epollout事件，poller发现tcp的发送缓冲区有空间，会通知相应的sock-channel，调用writeCallback_回调方法
    // 也就是调用TcpConnection::handleWrite方法，把发送缓冲区中的数据全部发送完成
    if (!faultError && remaining > 0) 
    {
        // 目前发送缓冲区剩余的待发送数据的长度
        size_t oldLen = outputBuffer_.readableBytes();
        if (oldLen + remaining >= highWaterMark_
            && oldLen < highWaterMark_
            && highWaterMarkCallback_)
        {
            loop_->queueInLoop(
                std::bind(highWaterMarkCallback_, shared_from_this(), oldLen+remaining)
            );
        }
        outputBuffer_.append((char*)data + nwrote, remaining);
        if (!channel_->isWriting())
        {
            channel_->enableWriting(); // 这里一定要注册channel的写事件，否则poller不会给channel通知epollout
        }
    }
}
```

MyMuduo各个模块

TcpServer:
	提供给用户的接口，
