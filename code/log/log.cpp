#include "log.h"

using namespace std;

/**
 * @brief Construct a new Log:: Log object
 * 
 */
Log::Log() {
    lineCount_ = 0;
    isAsync_ = false;
    writeThread_ = nullptr;
    deque_ = nullptr;
    toDay_ = 0;
    fp_ = nullptr;
}

/**
 * @brief Destroy the Log:: Log object
 * 
 */
Log::~Log() {
    //检查是否已经创建写线程
    if (writeThread_ && writeThread_->joinable()) {
        while (!deque_->empty()) {
            deque_->flush();
        };
        //确保所有的日志消息都被写入磁盘，以避免丢失数据
        deque_->Close();        //关闭写入队列
        writeThread_->join();   //等待写线程退出
    }
    if (fp_) {  //如果文件指针fp_非空
        //加锁以确保线程安全
        lock_guard <mutex> locker(mtx_);
        flush();    //刷新文件缓冲区
        fclose(fp_);//关闭文件指针
    }
}

/**
 * @brief 获取当前日志的级别
 * 
 * @return int 
 */
int Log::GetLevel() {
    lock_guard <mutex> locker(mtx_);
    return level_;
}

/**
 * @brief 设置日志级别
 * 
 * @param level 
 */
void Log::SetLevel(int level) {
    //对mtx_进行加锁，以确保线程安全
    lock_guard <mutex> locker(mtx_);
    //将类成员变量level_设置为参数level，以更改日志级别
    level_ = level;
    //在函数退出之前，std::lock_guard对象会自动解锁mtx_
}

/**
 * @brief 日志类初始化,设置日志级别、路径、文件名后缀和最大队列大小
 * 
 * @param level 
 * @param path 
 * @param suffix 
 * @param maxQueueSize 
 */
void Log::init(int level = 1, const char *path, const char *suffix,
               int maxQueueSize) {
    isOpen_ = true;     //表示打开日志文件
    level_ = level;
    if (maxQueueSize > 0) {
        //启用异步写入方式
        isAsync_ = true;
        if (!deque_) {
            //使用unique_ptr来管理这些对象的生命周期，以避免内存泄漏
            unique_ptr <BlockDeque<std::string>> newDeque(new BlockDeque<std::string>);
            deque_ = move(newDeque);

            std::unique_ptr <std::thread> NewThread(new thread(FlushLogThread));
            writeThread_ = move(NewThread);
        }
    } else {
        //启用同步写入方式
        isAsync_ = false;
    }
    //将lineCount_计数器重置为0
    lineCount_ = 0;

    //获取当前时间并根据时间设置日志文件名
    time_t timer = time(nullptr);
    struct tm *sysTime = localtime(&timer);
    struct tm t = *sysTime;
    path_ = path;
    suffix_ = suffix;
    char fileName[LOG_NAME_LEN] = {0};
    //格式化出文件名
    snprintf(fileName, LOG_NAME_LEN - 1, "%s/%04d_%02d_%02d%s",
             path_, t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, suffix_);
    toDay_ = t.tm_mday;

    {
        lock_guard <mutex> locker(mtx_);    //对日志对象的互斥量mtx_加锁
        buff_.RetrieveAll();                //将缓冲区buff_清空并关闭当前打开的文件fp_
        if (fp_) {
            flush();
            fclose(fp_);
        }

        //重新打开一个新的日志文件，如果文件不存在，则需要先创建它
        fp_ = fopen(fileName, "a");
        if (fp_ == nullptr) {
            //如果文件创建失败，则需要使用mkdir函数尝试创建目录，以确保能够正确写入日志
            mkdir(path_, 0777);
            fp_ = fopen(fileName, "a");
        }
        assert(fp_ != nullptr);
    }
}

/**
 * @brief 往日志中写入一条日志信息
 * 
 * @param level 指定了日志的等级
 * @param format 指定了日志的具体格式
 * @param ... 
 */
void Log::write(int level, const char *format, ...) {
    struct timeval now = {0, 0};
    gettimeofday(&now, nullptr);    //获取当前时间
    time_t tSec = now.tv_sec;
    struct tm *sysTime = localtime(&tSec);
    struct tm t = *sysTime;
    va_list vaList;

    /* 日志日期 日志行数 */
    // 判断是否需要在新的文件中写日志
    if (toDay_ != t.tm_mday || (lineCount_ && (lineCount_ % MAX_LINES == 0))) {
        unique_lock <mutex> locker(mtx_);
        locker.unlock();

        char newFile[LOG_NAME_LEN];
        char tail[36] = {0};
        // 按照一定的格式生成新的日志文件名
        snprintf(tail, 36, "%04d_%02d_%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);

        if (toDay_ != t.tm_mday) {
            snprintf(newFile, LOG_NAME_LEN - 72, "%s/%s%s", path_, tail, suffix_);
            toDay_ = t.tm_mday;
            lineCount_ = 0;
        } else {
            snprintf(newFile, LOG_NAME_LEN - 72, "%s/%s-%d%s", path_, tail, (lineCount_ / MAX_LINES), suffix_);
        }

        locker.lock();
        flush();
        fclose(fp_);
        // 打开新的日志文件
        fp_ = fopen(newFile, "a");
        assert(fp_ != nullptr);
    }

    {
        unique_lock <mutex> locker(mtx_);
        lineCount_++;
        int n = snprintf(buff_.BeginWrite(), 128, "%d-%02d-%02d %02d:%02d:%02d.%06ld ",
                         t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
                         t.tm_hour, t.tm_min, t.tm_sec, now.tv_usec);

        buff_.HasWritten(n);
        // 添加日志级别到缓存中
        AppendLogLevelTitle_(level);

        va_start(vaList, format);
        int m = vsnprintf(buff_.BeginWrite(), buff_.WritableBytes(), format, vaList);
        va_end(vaList);

        buff_.HasWritten(m);
        buff_.Append("\n\0", 2);

        // 如果开启异步模式，则将日志添加到阻塞队列中，否则直接写入文件
        if (isAsync_ && deque_ && !deque_->full()) {
            deque_->push_back(buff_.RetrieveAllToStr());
        } else {
            fputs(buff_.Peek(), fp_);
        }
        buff_.RetrieveAll();
    }
}

/**
 * @brief 在日志消息中添加与日志级别相对应的前缀
 * 
 * @param level 日志级别
 */
void Log::AppendLogLevelTitle_(int level) {
    switch (level) {
        case 0:
            buff_.Append("[debug]: ", 9);
            break;
        case 1:
            buff_.Append("[info] : ", 9);
            break;
        case 2:
            buff_.Append("[warn] : ", 9);
            break;
        case 3:
            buff_.Append("[error]: ", 9);
            break;
        default:
            buff_.Append("[info] : ", 9);
            break;
    }
}

/**
 * @brief 刷新缓冲区中的数据
 * 
 */
void Log::flush() {
    if (isAsync_) {
        //将日志队列中的所有数据刷新到文件
        deque_->flush();
    }
    //将缓冲区中的剩余数据写入文件
    fflush(fp_);
}

/**
 * @brief 异步写日志
 * 
 */
void Log::AsyncWrite_() {
    string str = "";
    //使用一个循环不断地从阻塞队列中取出日志信息,直到队列为空
    while (deque_->pop(str)) {
        //使用lock_guard保护mtx_的锁
        lock_guard <mutex> locker(mtx_);
        //将字符串写入文件
        fputs(str.c_str(), fp_);
    }
}

/**
 * @brief 单例模式的实现
 * 
 * @return Log* 
 */
Log *Log::Instance() {
    //inst 保证了只有一个 Log 实例被创建
    static Log inst;
    return &inst;
}

/**
 * @brief 后台刷新日志文件的线程
 * 
 */
void Log::FlushLogThread() {
    //AsyncWrite_该函数从异步队列中取出消息，然后将其写入日志文件
    Log::Instance()->AsyncWrite_();
}