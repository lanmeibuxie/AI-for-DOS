#pragma once
#include <string>

namespace mysocket
{
    namespace socket
    {

        class Socket
        {
        public:
            Socket();
            Socket(int sockfd);
            ~Socket();
            bool bind(const std::string &ip, int port);
            bool listen(int backlog);
            bool connect(const std::string &ip, int port);
            int accept();
            int send(const char *buf, int len);
            int recv(char *buf, int len);
            void close();

        protected:
            std::string m_ip;
            int m_port;
            int m_socketfd;
        };

    }
}