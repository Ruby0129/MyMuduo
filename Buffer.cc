#include "Buffer.h"

#include <errno.h>
#include <sys/uio.h>
#include <unistd.h>

/**
 * 从fd上读取数据 LT模式
 * Buffer缓冲区上有大小！但是从fd上读数据的时候，却不知道tcp最终数据的大小   
 */

ssize_t Buffer::readFd(int fd, int* saveErrno)
{
    char extrabuf[65535] = {0}; // 栈上的内存空间 64k
    struct iovec vec[2];
    const size_t writable = writeableBytes(); // 这是Buffer底层缓冲区剩余的可写空间大小
    vec[0].iov_base = begin() + writerIndex_; // 先填充vec[0]
    vec[0].iov_len =writable;

    vec[1].iov_base = extrabuf; // vec[0]不够的话，才会填充vec[1]
    vec[1].iov_len = sizeof(extrabuf);

    const int iovcnt = (writable < sizeof(extrabuf))  ? 2 : 1;
    const ssize_t n = ::readv(fd, vec, iovcnt);
    if(n < 0)
    {
        *saveErrno = errno;
    }
    else if (n <= writable) // Buffer的可写缓冲区已经够容纳要写的数据了
    {
        writerIndex_ += n;
    }
    else // extra里面也写入了数据
    {
        writerIndex_ += buffer_.size();
        append(extrabuf, n - writable); // writeIndex开始写n-writable的数据
    }
    return n;
}

// 通过fd发送数据
ssize_t Buffer::writeFd(int fd, int* saveErrno)
{
    ssize_t n = ::write(fd, peek(), readableBytes());
    if (n < 0)
    {
        *saveErrno = errno;
    }

    return n;
}