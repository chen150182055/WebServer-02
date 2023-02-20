#include "sqlconnpool.h"

using namespace std;

/**
 *
 */
SqlConnPool::SqlConnPool() {
    useCount_ = 0;
    freeCount_ = 0;
}

/**
 *
 * @return
 */
SqlConnPool *SqlConnPool::Instance() {
    static SqlConnPool connPool;
    return &connPool;
}

/**
 * 数据库连接池的初始化函数
 * @param host
 * @param port
 * @param user
 * @param pwd
 * @param dbName
 * @param connSize
 */
void SqlConnPool::Init(const char *host, int port,
                       const char *user, const char *pwd, const char *dbName,
                       int connSize = 10) {
    assert(connSize > 0);
    //初始化给定数量（connSize）的MySQL连接
    for (int i = 0; i < connSize; i++) {
        MYSQL *sql = nullptr;
        sql = mysql_init(sql);
        if (!sql) {
            //如果初始化MySQL连接出现问题，则函数记录错误日志并停止程序
            LOG_ERROR("MySql init error!");
            assert(sql);
        }
        sql = mysql_real_connect(sql, host,
                                 user, pwd,
                                 dbName, port, nullptr, 0);
        if (!sql) {
            LOG_ERROR("MySql Connect error!");
        }
        connQue_.push(sql);
    }
    MAX_CONN_ = connSize;
    //初始化一个信号量，将它的初始值设置为连接池的最大容量，以确保每次从连接池获取MySQL连接时保证连接池中存在可用的连接
    sem_init(&semId_, 0, MAX_CONN_);
}

/**
 * 从数据库连接池中获取一个连接
 * 多线程访问连接池时存在并发问题，因此使用信号量和互斥锁保证线程安全
 * @return
 */
MYSQL *SqlConnPool::GetConn() {
    MYSQL *sql = nullptr;
    //当连接池为空
    if (connQue_.empty()) {
        //打印一条日志并返回nullptr
        LOG_WARN("SqlConnPool busy!");
        return nullptr;
    }
    //先等待信号量semId_的值减一，表示有一个连接被占用
    sem_wait(&semId_);
    {
        //获取锁并从连接队列connQue_中获取一个连接
        lock_guard <mutex> locker(mtx_);
        sql = connQue_.front();
        connQue_.pop();
    }
    //然后将信号量semId_的值加一，表示连接被释放
    //最后返回获取到的连接
    return sql;
}

/**
 * 将一个已经使用完的MySQL连接放回连接池
 * @param sql
 */
void SqlConnPool::FreeConn(MYSQL *sql) {
    assert(sql);    //确保sql不是null
    //通过互斥锁将连接放回队列中
    lock_guard <mutex> locker(mtx_);
    connQue_.push(sql);
    //调用sem_post()递增信号量的值
    sem_post(&semId_);
}

/**
 * 实现关闭连接池
 */
void SqlConnPool::ClosePool() {
    //获取互斥锁
    lock_guard <mutex> locker(mtx_);
    //循环遍历连接队列
    while (!connQue_.empty()) {
        //将队列中的连接逐一关闭，并释放连接池占用的资源
        auto item = connQue_.front();
        connQue_.pop();
        mysql_close(item);
    }
    //释放MySQL客户端库占用的资源
    mysql_library_end();
}

/**
 * 获取当前连接池中可用的连接数,没用修改操作
 * 使用了 lock_guard 这种自动加锁的方式保证了线程安全
 * @return
 */
int SqlConnPool::GetFreeConnCount() {
    //加了一个 lock_guard 锁定了 mtx_ 互斥量
    lock_guard <mutex> locker(mtx_);
    //返回当前连接队列 connQue_ 的大小，就是连接池中可用的连接数
    return connQue_.size();
}

SqlConnPool::~SqlConnPool() {
    ClosePool();
}
