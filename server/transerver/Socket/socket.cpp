#include "socket.h"
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <cstring>
#include <arpa/inet.h>
#include <unistd.h>

using namespace mysocket::socket;

Socket::Socket() : m_ip(""), m_port(0), m_socketfd(0)
{
    m_socketfd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m_socketfd < 0)
    {
        printf("create socket error: error: errno=%d errmsg=%s\n", errno, strerror(errno));
    }
    else
    {
        printf("create socket success!\n");
    }
}

Socket::Socket(int sockfd) : m_ip(""), m_port(0), m_socketfd(sockfd)
{
}

Socket::~Socket()
{
    close();
}
bool Socket::bind(const std::string &ip, int port)
{

    struct sockaddr_in sockaddr;
    std::memset(&sockaddr, 0, sizeof(sockaddr));
    sockaddr.sin_family = AF_INET;
    if (ip.empty())
    {
        // ip为空绑定任意Ip
        sockaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    }
    else
    {
        sockaddr.sin_addr.s_addr = inet_addr(ip.c_str());
    }
    sockaddr.sin_port = htons(port); // 主机地址转换为网络地址
    if (::bind(m_socketfd, (struct sockaddr *)&sockaddr, sizeof(sockaddr)) < 0)
    {
        printf("socket bind error : errno=%d errmsg=%s\n", errno, strerror(errno));
        return false;
    }

    m_port = port;
    m_ip = ip;
    printf("socket bind success: ip=%s port=%d\n", ip.c_str(), port);
    return true;
}

bool Socket::listen(int backlog = 1024)
{
    // 3.监听 socket
    if (::listen(this->m_socketfd, backlog) < 0)
    {
        printf("socket listen error : errno=%d errmsg=%s\n", errno, strerror(errno));
        return false;
    }

    printf("socket listening\n");
    return true;
}

bool Socket::connect(const std::string &ip, int port)
{
    struct sockaddr_in sockaddr;
    std::memset(&sockaddr, 0, sizeof(sockaddr));
    sockaddr.sin_family = AF_INET;
    sockaddr.sin_addr.s_addr = inet_addr(ip.c_str());
    sockaddr.sin_port = htons(port); // 主机地址转换为网络地址
    if (::connect(m_socketfd, (struct sockaddr *)&sockaddr, sizeof(sockaddr)) < 0)
    {
        printf(" socket connect error: error: errno=%d errmsg=%s\n", errno, strerror(errno));
        return false;
    }
    m_ip = ip;
    m_port = port;
    return true;
}

int Socket::accept()
{
    int connfd = ::accept(m_socketfd, nullptr, nullptr);
    if (connfd < 0)
    {
        printf("socket accept error : errno=%d errmsg=%s\n", errno, strerror(errno));
        return false;
    }
    printf("socket accept: conn=%d\n", connfd);
    return connfd;
}

int Socket::send(const char *buf, int len)
{
    return ::send(m_socketfd, buf, len, 0);
}

int Socket::recv(char *buf, int len)
{
    return ::recv(m_socketfd, buf, len, 0);
}

void Socket::close()
{
    if (m_socketfd > 0)
    {
        ::close(m_socketfd);
        m_socketfd = 0;
    }
}