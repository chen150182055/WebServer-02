/*
 * @Author       : mark
 * @Date         : 2020-06-26
 * @copyleft Apache 2.0
 */
#include "buffer.h"

/**
 * 将initBuffSize作为参数传入类的buffer_成员变量，将readPos_和writePos_设为0
 * @param initBuffSize
 */
Buffer::Buffer(int initBuffSize) : buffer_(initBuffSize), readPos_(0), writePos_(0) {}

/**
 * 计算Buffer中可读字节数
 * @return
 */
size_t Buffer::ReadableBytes() const {
    //其中writePos_为Buffer中最后一个可写字节的下一个位置，
    // readPos_为Buffer中最后一个可读字节的下一个位置，
    // 因此可读字节数等于writePos_减去readPos_
    return writePos_ - readPos_;
}

/**
 * 返回当前可写入的字节数
 * @return
 */
size_t Buffer::WritableBytes() const {
    //其中buffer_是一个vector，存储了数据，
    // writePos_是一个size_t类型的变量，
    // 表示当前的写入位置。
    // 所以返回的就是buffer_的容量减去当前的写入位置，就是当前可写入的字节数
    return buffer_.size() - writePos_;
}

/**
 * 返回Buffer中可以被预先添加的字节数
 * @return
 */
size_t Buffer::PrependableBytes() const {
    return readPos_;
}

/**
 * 返回一个指向当前读取位置的指针
 * @return
 */
const char *Buffer::Peek() const {
    //readPos_为当前缓冲区读取位置，BeginPtr_()为缓冲区起始指针
    return BeginPtr_() + readPos_;
}

/**
 * 用于从缓冲区中取出len长度的数据
 * @param len
 */
void Buffer::Retrieve(size_t len) {
    //断言len不能超过可读数据的长度
    assert(len <= ReadableBytes());
    //将读指针readPos_向前移动len个位置
    readPos_ += len;
}

/**
 * 用于指示从缓冲区中取出数据的结束位置
 * @param end
 */
void Buffer::RetrieveUntil(const char *end) {
    //assert用于确保Peek()函数返回的值小于end
    assert(Peek() <= end);
    //Retrieve()函数则从缓冲区中取出end - Peek()字节的数据，这些数据被取出后从缓冲区中移除
    Retrieve(end - Peek());
}

/**
 * 清空缓冲区数据
 */
void Buffer::RetrieveAll() {
    //bzero函数用于将缓冲区中的数据置为0
    bzero(&buffer_[0], buffer_.size());
    //readPos_和writePos_分别表示读取位置和写入位置，将它们置为0表示重新开始读取和写入
    readPos_ = 0;
    writePos_ = 0;
}

/**
 * 将Buffer中所有可读字节复制到一个std::string中，之后清空Buffer中所有可读字节
 * @return
 */
std::string Buffer::RetrieveAllToStr() {
    //其中函数Buffer::Peek()用于获取Buffer当前可读字节的起始位置，
    // Buffer::ReadableBytes()用于获取Buffer当前可读字节的长度，
    // Buffer::RetrieveAll()用于清空Buffer中所有可读字节
    std::string str(Peek(), ReadableBytes());
    RetrieveAll();
    return str;
}

/**
 * 返回一个指向Buffer类内部存储数据的起始位置，即writePos_指向的位置
 * @return
 */
const char *Buffer::BeginWriteConst() const {
    return BeginPtr_() + writePos_;
}

/**
 * 返回写指针指向的位置，即字符缓冲区中写入数据的起始位置
 * @return
 */
char *Buffer::BeginWrite() {
    return BeginPtr_() + writePos_;
}

/**
 * 更新Buffer类的writePos_成员变量值
 * @param len 要更新的字节数
 */
void Buffer::HasWritten(size_t len) {
    //writePos_成员变量表示缓冲区已写入的字节数
    writePos_ += len;
}

/**
 * 将一个字符串追加到缓冲区
 * @param str
 */
void Buffer::Append(const std::string &str) {
    //它使用std :: string的data（）和length（）函数从给定的字符串中提取数据和长度，
    // 然后将它们传递给Append（）函数
    Append(str.data(), str.length());
}

/**
 * 重载函数
 * @param data
 * @param len
 */
void Buffer::Append(const void *data, size_t len) {
    assert(data);   //检查data是否为null指针
    //将data强制转换为char类型的指针并调用另一个Append函数，传入转换后的指针和len参数
    Append(static_cast<const char *>(data), len);
}

/**
 *
 * @param str
 * @param len
 */
void Buffer::Append(const char *str, size_t len) {
    assert(str);
    //确保Buffer有足够的内存空间存储str指向的字符串
    EnsureWriteable(len);
    //将str指向的字符串拷贝到Buffer中
    std::copy(str, str + len, BeginWrite());
    //更新Buffer的写指针
    HasWritten(len);
}

/**
 *
 * @param buff
 */
void Buffer::Append(const Buffer &buff) {
    //Peek()函数用于返回buff对象中可读字节的首地址，
    //ReadableBytes()用于返回buff对象中可读字节的数量
    Append(buff.Peek(), buff.ReadableBytes());
}

/**
 *
 * @param len
 */
void Buffer::EnsureWriteable(size_t len) {
    if (WritableBytes() < len) {
        MakeSpace_(len);
    }
    assert(WritableBytes() >= len);
}

/**
 * 实现从文件描述符fd中读取数据到Buffer结构体
 * @param fd
 * @param saveErrno
 * @return
 */
ssize_t Buffer::ReadFd(int fd, int *saveErrno) {
    char buff[65535];
    struct iovec iov[2];
    const size_t writable = WritableBytes();
    /* 分散读， 保证数据全部读完 */
    iov[0].iov_base = BeginPtr_() + writePos_;
    iov[0].iov_len = writable;
    iov[1].iov_base = buff;
    iov[1].iov_len = sizeof(buff);

    //readv函数实现分散读，将文件读取到两个位置
    const ssize_t len = readv(fd, iov, 2);
    if (len < 0) {
        *saveErrno = errno;
    } else if (static_cast<size_t>(len) <= writable) {
        //如果len小于等于可写的位置，则将数据写入buffer_中
        writePos_ += len;
    } else {
        //如果大于可写的位置，则将第二个位置的数据写入buffer_中
        writePos_ = buffer_.size();
        Append(buff, len - writable);
    }
    return len;
}

/**
 * 用于将缓冲区的数据写入文件描述符fd中
 * @param fd
 * @param saveErrno
 * @return
 */
ssize_t Buffer::WriteFd(int fd, int *saveErrno) {
    //获取可读字节的大小
    size_t readSize = ReadableBytes();
    //将缓冲区的数据写入fd中,返回实际写入的字节数。
    ssize_t len = write(fd, Peek(), readSize);
    if (len < 0) {  //如果返回值小于0，表示写入失败
        //将错误码保存在saveErrno中，并返回写入失败的错误码
        *saveErrno = errno;
        return len;
    }
    //如果写入成功，则将读位置加上实际写入的字节数，返回写入成功的字节数
    readPos_ += len;
    return len;
}

/**
 * 返回buffer_成员变量的第一个元素的地址
 * @return
 */
char *Buffer::BeginPtr_() {
    //buffer_是一个容器，
    // begin()函数返回一个迭代器指向容器的第一个元素，
    // &*操作取出迭代器指向的元素的地址
    return &*buffer_.begin();
}

/**
 *
 * @return
 */
const char *Buffer::BeginPtr_() const {
    return &*buffer_.begin();
}

/**
 * 确保buffer_中有足够的空间来存放len长度的数据
 * @param len
 */
void Buffer::MakeSpace_(size_t len) {
    //检查WritableBytes()+PrependableBytes()是否大于len
    if (WritableBytes() + PrependableBytes() < len) {
        //如果不够，就将buffer_的大小重新改为writePos_ + len + 1
        buffer_.resize(writePos_ + len + 1);
    } else {
        //如果可写的空间够用，
        // 就使用std::copy()将readPos_到writePos_之间的数据复制到buffer_的开头，
        // 然后更新readPos_和writePos_，
        // 并且断言readable == ReadableBytes()，
        // 以确保数据复制正确
        size_t readable = ReadableBytes();
        std::copy(BeginPtr_() + readPos_, BeginPtr_() + writePos_, BeginPtr_());
        readPos_ = 0;
        writePos_ = readPos_ + readable;
        assert(readable == ReadableBytes());
    }
}