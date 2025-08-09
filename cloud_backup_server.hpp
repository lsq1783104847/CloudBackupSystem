#ifndef CLOUD_BACKUP_SERVER_HPP
#define CLOUD_BACKUP_SERVER_HPP

#include <atomic>
#include "util.hpp"
#include "config.hpp"
#include "data_manager.hpp"
#include "llhttp.h"

namespace cloud_backup
{
    struct HTTPConnection
    {
        using ptr = std::shared_ptr<HTTPConnection>;
        HTTPConnection(int fd) : _fd(fd)
        {
            llhttp_init(&_parser, HTTP_REQUEST, nullptr);
            _parser.data = this; // 设置回调函数的上下文
        }
        ~HTTPConnection()
        {
            if (_fd != -1)
                close(_fd);
        }
        void Reader()
        {
        }
        void Writer()
        {
        }
        void Excepter()
        {
        }
        int _fd = -1;
        std::atomic<bool> _is_connected = true;
        llhttp_t _parser;
        std::string _request_buffer;
        std::string _response_buffer;

        static std::unordered_map<int, HTTPConnection::ptr> _connections;
        static std::mutex _connections_mutex;
    };

    class CloudBackupServer
    {
    public:
        CloudBackupServer(const std::string &work_path)
        {
            // 初始化日志器
            cloud_backup::InitCloudBackupLogger();
            // 将进程变成守护进程
            Daemon(work_path);
            // 读取配置文件修改日志器的落地方向
            auto config = Config::GetInstance();
            if (ModifyCloudBackupLoggerSinks(config->GetLogFilePath()) == false)
            {
                LOG_ERROR("ModifyCloudBackupLoggerSinks error, exit");
                exit(LOAD_CONFIG_FILE_ERROR);
            }
            // 读取配置文件获取服务器ip和端口号
            _server_port = config->GetServerPort();

            InitializeServer();
            StartServer();
            Dispatcher();
        }
        ~CloudBackupServer() { DestoryServer(); }

    private:
        // 初始化服务器，bind port
        void InitializeServer()
        {
            _socket.InitSocket();
            _socket.Bind(_server_port);
            _events = new epoll_event[_maxevents];
            if (_events == nullptr)
                LOG_ERROR("CloudBackupServer Initialize ERROR, memory allocation failed");
            LOG_INFO("CloudBackupServer Initialize Succeed, bind on %d port", _server_port);
        }
        void DestoryServer()
        {
            if (_events != nullptr)
                delete[] _events;
            _events = nullptr;
        }
        // 启动服务器，开始listen
        void StartServer()
        {
            _socket.Listen(Config::GetInstance()->GetListenQueueSize());
            LOG_INFO("CloudBackupServer Start Listen");
        }
        //
        void Dispatcher()
        {
            while (true)
            {
                int n = _epoller.EpollBlockWait(_events, _maxevents);
                if (n == -1)
                    LOG_ERROR("EpollBlockWait ERROR");
                else if (n == 0)
                    LOG_INFO("EpollBlockWait Timeout");
                else if (n > 0)
                {
                    for (int pos = 0; pos < n; pos++)
                    {
                        if (_events[pos].data.fd == _socket.GetSocketet())
                        {
                            if (_events[pos].events & EPOLLIN)
                                Accepter();
                        }
                        else
                        {
                            auto it = HTTPConnection::_connections.find(_events[pos].data.fd);
                            if (it != HTTPConnection::_connections.end())
                            {
                                if (_events[pos].events & EPOLLIN)
                                    it->second->Reader();
                                if (_events[pos].events & EPOLLOUT)
                                    it->second->Writer();
                            }
                            else
                            {
                                LOG_ERROR("Connection not found for fd: %d", _events[pos].data.fd);
                                continue;
                            }
                        }
                    }
                }
            }
        }
        void Accepter()
        {
            std::string client_ip;
            uint16_t client_port;
            int new_fd = _socket.Accept(&client_ip, &client_port);
            if (new_fd == -1)
            {
                LOG_WARN("Accepter ERROR");
                return;
            }
            LOG_INFO("New connection accepted, client_ip:%s client_port:%d new_fd:%d", client_ip.c_str(), client_port, new_fd);
            if (_epoller.EpollAdd(new_fd, EPOLLIN | EPOLLOUT | EPOLLET) == false)
            {
                LOG_WARN("EpollAdd ERROR");
                close(new_fd);
                return;
            }
            std::unique_lock<std::mutex> connections_lock(HTTPConnection::_connections_mutex);
            HTTPConnection::ptr conn = std::make_shared<HTTPConnection>(new_fd);
            HTTPConnection::_connections[new_fd] = conn;
        }
        void Reader()
        {
        }
        void Writer()
        {
        }

    private:
        uint16_t _server_port;
        NetSocketUtil _socket;
        EpollUtil _epoller;
        epoll_event *_events = nullptr;
        int _maxevents = Config::GetInstance()->GetEpollEventsSize();
    };
}

#endif