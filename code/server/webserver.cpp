#include "webserver.h"

using namespace std;

/**
 * @brief 构造函数,初始化服务器
 * 
 * @param port 服务器监听端口号
 * @param trigMode 触发模式
 * @param timeoutMS 连接超时时间
 * @param OptLinger 是否使用linger选项
 * @param sqlPort 数据库端口号
 * @param sqlUser 数据库用户名
 * @param sqlPwd 数据库密码
 * @param dbName 数据库名称
 * @param connPoolNum 连接池大小
 * @param threadNum 线程池大小
 * @param openLog 是否打开日志系统
 * @param logLevel 日志等级
 * @param logQueSize 日志缓存长度
 */
WebServer::WebServer(
        int port, int trigMode, int timeoutMS, bool OptLinger,
        int sqlPort, const char *sqlUser, const char *sqlPwd,
        const char *dbName, int connPoolNum, int threadNum,
        bool openLog, int logLevel, int logQueSize) :
        port_(port), openLinger_(OptLinger), timeoutMS_(timeoutMS), isClose_(false),
        timer_(new HeapTimer()), threadpool_(new ThreadPool(threadNum)), epoller_(new Epoller()) {
    srcDir_ = getcwd(nullptr, 256);
    //srcDir_保存资源文件的路径,使用getcwd()函数获取当前工作目录
    assert(srcDir_);
    strncat(srcDir_, "/resources/", 16);
    HttpConn::userCount = 0;
    HttpConn::srcDir = srcDir_;
    SqlConnPool::Instance()->Init("localhost", sqlPort, sqlUser, sqlPwd, dbName, connPoolNum);

    InitEventMode_(trigMode);               //初始化触发模式
    if (!InitSocket_()) { isClose_ = true; }//初始化套接字连接

    if (openLog) {
        Log::Instance()->init(logLevel, "./log", ".log", logQueSize);
        if (isClose_) { LOG_ERROR("========== Server init error!=========="); }
        else {
            LOG_INFO("========== Server init ==========");
            LOG_INFO("Port:%d, OpenLinger: %s", port_, OptLinger ? "true" : "false");
            LOG_INFO("Listen Mode: %s, OpenConn Mode: %s",
                     (listenEvent_ & EPOLLET ? "ET" : "LT"),
                     (connEvent_ & EPOLLET ? "ET" : "LT"));
            LOG_INFO("LogSys level: %d", logLevel);
            LOG_INFO("srcDir: %s", HttpConn::srcDir);
            LOG_INFO("SqlConnPool num: %d, ThreadPool num: %d", connPoolNum, threadNum);
        }
    }
}

/**
 * @brief 析构函数,关闭服务器
 * 
 */
WebServer::~WebServer() {
    close(listenFd_);       //关闭服务器监听文件描述符
    isClose_ = true;        //标记服务器已经关闭
    free(srcDir_);    //释放资源文件路径
    SqlConnPool::Instance()->ClosePool();   //关闭数据库连接池
}

/**
 * @brief 初始化监听和连接的使事件设置,根据trigMode的值来设置listenEvent_和connEvent_的值
 * 
 * @param trigMode 用于指定触发模式
 */
void WebServer::InitEventMode_(int trigMode) {
    listenEvent_ = EPOLLRDHUP;              //监听事件设置
    connEvent_ = EPOLLONESHOT | EPOLLRDHUP; //连接事件设置
    switch (trigMode) {
        case 0:
            break;
        case 1:
            connEvent_ |= EPOLLET;  //使能连接事件设置
            break;
        case 2:
            listenEvent_ |= EPOLLET;//使能监听事件设置
            break;
        case 3:
            listenEvent_ |= EPOLLET;//使能监听事件设置
            connEvent_ |= EPOLLET;  //使能连接事件设置
            break;
        default:
            listenEvent_ |= EPOLLET;//使能监听事件设置
            connEvent_ |= EPOLLET;  //使能连接事件设置
            break;
    }
    HttpConn::isET = (connEvent_ & EPOLLET);    //检测以EPOLLET的形式使能的连接事件
}

/**
 * @brief 服务器启动函数
 * 循环检测是否关闭服务器
 * 设置Epoll的超时时间
 * 调用Epoll的Wait函数等待事件
 * 根据事件类型，分别处理不同的事件
 *
 */
void WebServer::Start() {
    int timeMS = -1;  /* epoll wait timeout == -1 无事件将阻塞 */
    //循环检测是否关闭服务器
    if (!isClose_) {
        LOG_INFO("========== Server start ==========");
    }
    while (!isClose_) {
        if (timeoutMS_ > 0) {
            //设置Epoll的超时时间
            timeMS = timer_->GetNextTick();
        }
        //调用Epoll的Wait函数等待事件
        int eventCnt = epoller_->Wait(timeMS);
        for (int i = 0; i < eventCnt; i++) {
            /* 处理事件 */
            int fd = epoller_->GetEventFd(i);
            uint32_t events = epoller_->GetEvents(i);
            if (fd == listenFd_) {      //处理监听事件
                DealListen_();
            } else if (events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {   //处理关闭事件
                assert(users_.count(fd) > 0);
                CloseConn_(&users_[fd]);
            } else if (events & EPOLLIN) {  //处理读取请求
                assert(users_.count(fd) > 0);
                DealRead_(&users_[fd]);
            } else if (events & EPOLLOUT) { //处理写入请求
                assert(users_.count(fd) > 0);
                DealWrite_(&users_[fd]);
            } else {
                LOG_ERROR("Unexpected event");
            }
        }
    }
}

/**
 * @brief 将指定的错误信息info发送给客户端
 * 
 * @param fd 
 * @param info 
 */
void WebServer::SendError_(int fd, const char *info) {
    assert(fd > 0);
    //使用send函数发送错误信息info到客户端
    int ret = send(fd, info, strlen(info), 0);
    if (ret < 0) {  //发送失败
        //则记录日志
        LOG_WARN("send error to client[%d] error!", fd);
    }
    //关闭套接字
    close(fd);
}

/**
 * @brief 关闭每一个客户端的连接
 * 
 * @param client 
 */
void WebServer::CloseConn_(HttpConn *client) {
    assert(client);
    LOG_INFO("Client[%d] quit!", client->GetFd());  //记录日志
    epoller_->DelFd(client->GetFd());               //使用epoller类删除文件描述符
    client->Close();
}

/**
 * @brief 向服务器添加一个新的客户端连接
 * 将新的客户端连接添加到Web服务器的事件循环中，
 * 以便Web服务器能够及时响应该客户端的请求
 * 
 * @param fd 客户端连接的文件描述符
 * @param addr 客户端连接的地址信息addr
 */
void WebServer::AddClient_(int fd, sockaddr_in addr) {
    assert(fd > 0);
    users_[fd].init(fd, addr);  //初始化客户端连接
    if (timeoutMS_ > 0) {     
        //添加一个定时器，定时器会在指定的超时时间后关闭该客户端连接  
        //使用std::bind绑定WebServer对象和HttpConn对象的引用，以便在CloseConn_函数中可以访问HttpConn对象的成员
        timer_->add(fd, timeoutMS_, std::bind(&WebServer::CloseConn_, this, &users_[fd]));
    }
    //添加到epoll实例中，注册EPOLLIN事件，即可读事件，并将事件类型(connEvent_)加入到epoll事件表中
    epoller_->AddFd(fd, EPOLLIN | connEvent_);  
    SetFdNonblock(fd);  //设置为非阻塞模式，以便异步IO操作
    LOG_INFO("Client[%d] in!", users_[fd].GetFd());
}

/**
 * @brief 用于处理监听socket的事件
 * 
 */
void WebServer::DealListen_() {
    struct sockaddr_in addr;     //存储新连接的地址信息
    socklen_t len = sizeof(addr);//存储addr变量的长度
    //监听新的客户端连接
    do {
        int fd = accept(listenFd_, (struct sockaddr *) &addr, &len);
        //如果accept函数返回的文件描述符fd小于等于0，就直接返回，表示没有新的连接到来
        if (fd <= 0) { return; }
        //如果当前连接的数量(HttpConn::userCount)已经超过了Web服务器可以处理的最大连接数(MAX_FD)，
        //就调用SendError_函数向新的连接返回错误信息，然后记录一个日志表示连接已满，
        //然后直接返回，不再处理该连接
        else if (HttpConn::userCount >= MAX_FD) {
            SendError_(fd, "Server busy!");
            LOG_WARN("Clients is full!");
            return;
        }
        //如果当前连接数量还没有达到最大值，
        //就调用WebServer类的AddClient_函数，
        //将新的连接添加到Web服务器中，处理该连接
        AddClient_(fd, addr);
        //使用EPOLLET事件模式时，如果还有新的连接在等待，就继续进行循环，等待新的连接到来
    } while (listenEvent_ & EPOLLET);   
    //否则退出循环。EPOLLET是WebServer类的成员变量，表示是否启用边缘触发模式
}

/**
 * @brief 处理客户端连接的读事件
 * 
 * @param client 需要处理的客户端连接
 */
void WebServer::DealRead_(HttpConn *client) {
    assert(client);         //检查client指针是否为空
    ExtentTime_(client);    //更新客户端连接的超时时间
    //将一个任务添加到线程池中,该任务是一个绑定到OnRead_函数上的函数对象
    //绑定的对象是WebServer对象本身和client指针
    //以便在OnRead_函数中可以访问到HttpConn对象的成员
    threadpool_->AddTask(std::bind(&WebServer::OnRead_, this, client));
}

/**
 * @brief 处理客户端连接的写事件
 * 
 * @param client 需要处理的客户端连接
 */
void WebServer::DealWrite_(HttpConn *client) {
    assert(client);     //检查client指针是否为空
    ExtentTime_(client);//更新客户端连接的超时时间
    //将一个任务添加到线程池中,该任务是一个绑定到OnWrite_函数上的函数对象
    //绑定的对象是WebServer对象本身和client指针
    //以便在OnWrite_函数中可以访问到HttpConn对象的成员
    threadpool_->AddTask(std::bind(&WebServer::OnWrite_, this, client));
}

/**
 * @brief 更新客户端连接的超时时间
 * 
 * @param client 需要更新的客户端连接
 */
void WebServer::ExtentTime_(HttpConn *client) {
    assert(client);
    if (timeoutMS_ > 0) {
        //将client对象的文件描述符和timeoutMS_变量作为参数传递给Timer类的adjust函数
        timer_->adjust(client->GetFd(), timeoutMS_);
    }
}

/**
 * @brief 处理客户端连接的读事件
 * 
 * @param client 需要处理的客户端连接
 */
void WebServer::OnRead_(HttpConn *client) {
    assert(client);
    int ret = -1;
    int readErrno = 0;
    //将读取到的数据保存到client对象的inBuf_成员变量中
    ret = client->read(&readErrno);
    if (ret <= 0 && readErrno != EAGAIN) {
        CloseConn_(client);
        return;
    }
    //OnProcess函数会根据请求的具体类型，调用相应的业务逻辑处理函数
    OnProcess(client);
}

/**
 * @brief 处理客户端连接的请求
 * 
 * @param client 需要处理的客户端连接
 */
void WebServer::OnProcess(HttpConn *client) {
    if (client->process()) {    //如果client对象的process函数返回值为true，表示该客户端连接需要进行写操作
        //修改客户端连接的文件描述符的事件类型为可写，从而让Epoll监控该客户端连接的可写事件
        epoller_->ModFd(client->GetFd(), connEvent_ | EPOLLOUT);
    } else {                    //如果process函数返回值为false，表示该客户端连接需要进行读操作
        //修改客户端连接的文件描述符的事件类型为可读，从而让Epoll监控该客户端连接的可读事件
        epoller_->ModFd(client->GetFd(), connEvent_ | EPOLLIN);
    }
}

/**
 * @brief 处理客户端连接的写操作
 * 
 * @param client 表示需要进行写操作的客户端连接
 */
void WebServer::OnWrite_(HttpConn *client) {
    assert(client);
    int ret = -1;
    int writeErrno = 0;
    ret = client->write(&writeErrno);
    //检查客户端连接还有没有未发送完的数据
    if (client->ToWriteBytes() == 0) {  //所有数据都已经发送完毕
        /* 传输完成 */
        //客户端连接的HTTP协议版本和是否支持持久连接
        if (client->IsKeepAlive()) {
            //继续处理该客户端连接的下一个请求
            OnProcess(client);
            return;
        }
    } else if (ret < 0) {   //还有数据未发送完毕
        if (writeErrno == EAGAIN) { //当前写缓冲区已满
            /* 继续传输 */
            //修改客户端连接的文件描述符的事件类型为可写，从而让Epoll监控该客户端连接的可写事件
            epoller_->ModFd(client->GetFd(), connEvent_ | EPOLLOUT);
            return;
        }
    }
    //如果write函数返回的错误信息不是EAGAIN，那么说明写操作发生了严重错误
    CloseConn_(client);
}

/**
 * @brief 初始化服务器的Soccket
 * 
 */
/* Create listenFd */
bool WebServer::InitSocket_() {
    int ret;
    struct sockaddr_in addr;
    //1.检查指定的端口是否合法
    if (port_ > 65535 || port_ < 1024) {
        LOG_ERROR("Port:%d error!", port_);
        return false;
    }
    //2.初始化 sockaddr_in 结构体变量
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port_);
    struct linger optLinger = {0};
    //3.如果开启了优雅关闭选项，则设置 SO_LINGER 套接字选项
    if (openLinger_) {
        /* 优雅关闭: 直到所剩数据发送完毕或超时 */
        optLinger.l_onoff = 1;
        optLinger.l_linger = 1;
    }

    //4.创建一个 SOCK_STREAM
    listenFd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd_ < 0) {
        LOG_ERROR("Create socket error!", port_);
        return false;
    }

    
    ret = setsockopt(listenFd_, SOL_SOCKET, SO_LINGER, &optLinger, sizeof(optLinger));
    if (ret < 0) {
        close(listenFd_);
        LOG_ERROR("Init linger error!", port_);
        return false;
    }

    int optval = 1;
    //5.设置 SO_REUSEADDR 套接字选项
    /* 端口复用 */
    /* 只有最后一个套接字会正常接收数据。 */
    ret = setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, (const void *) &optval, sizeof(int));
    if (ret == -1) {
        LOG_ERROR("set socket setsockopt error !");
        close(listenFd_);
        return false;
    }

    //6.将套接字绑定到指定的地址
    ret = bind(listenFd_, (struct sockaddr *) &addr, sizeof(addr));
    if (ret < 0) {
        LOG_ERROR("Bind Port:%d error!", port_);
        close(listenFd_);
        return false;
    }

    //7.开始监听该套接字
    ret = listen(listenFd_, 6);
    if (ret < 0) {
        LOG_ERROR("Listen port:%d error!", port_);
        close(listenFd_);
        return false;
    }

    //8.开始监听该套接字
    ret = epoller_->AddFd(listenFd_, listenEvent_ | EPOLLIN);
    if (ret == 0) {
        LOG_ERROR("Add listen error!");
        close(listenFd_);
        return false;
    }
    //9.将套接字设置为非阻塞模式
    SetFdNonblock(listenFd_);
    //10.记录服务器启动的信息,并返回 true
    LOG_INFO("Server port:%d", port_);
    return true;
}

/**
 * @brief 将文件描述符 fd 设置为非阻塞模式
 * 在调用读写函数时，如果没有数据可以读取或写入，
 * 则函数会立即返回，而不是一直等待数据的到来或写入完成
 * 
 * @param fd 
 * @return int 
 */
int WebServer::SetFdNonblock(int fd) {
    assert(fd > 0);
    //调用 fcntl 函数，并传递 F_SETFL 和 O_NONBLOCK 两个参数实现
    //F_SETFL 用于设置文件描述符的状态标志
    //O_NONBLOCK 则表示将文件描述符设置为非阻塞模式
    return fcntl(fd, F_SETFL, fcntl(fd, F_GETFD, 0) | O_NONBLOCK);
}


