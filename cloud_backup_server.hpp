#ifndef CLOUD_BACKUP_SERVER_HPP
#define CLOUD_BACKUP_SERVER_HPP

#include "util.hpp"
#include "config.hpp"
#include "data_manager.hpp"
#include "HTTPconnection.hpp"

namespace cloud_backup
{
    class CloudBackupServer
    {

    public:
        CloudBackupServer(const std::string &program_path)
        {
            // 将进程变成守护进程
            Daemon(program_path);
            // 读取配置文件修改日志器的落地方向
            auto config = Config::GetInstance();
            if (ModifyCloudBackupLoggerSinks(config->GetLogFilePath(), config->GetRollFileSize()) == false)
            {
                LOG_ERROR("ModifyCloudBackupLoggerSinks error, exit");
                exit(LOAD_CONFIG_FILE_ERROR);
            }
            // 读取配置文件获取服务器端口号
            _server_port = config->GetServerPort();

            InitializeServer();
            StartServer();
            Dispatcher();
        }
        ~CloudBackupServer() { DestoryServer(); }

    private:
        // 初始化服务器，绑定端口，创建管道等
        void InitializeServer()
        {
            int pipefd[2] = {-1, -1};
            if (pipe(pipefd) == -1)
            {
                LOG_ERROR("CloudBackupServer Initialize ERROR, Create pipe error:%d  message:%s", errno, strerror(errno));
                exit(INIT_PIPE_ERROR);
            }
            _write_pipe_fd = pipefd[1];
            _read_pipe_fd = pipefd[0];
            if (SetNonBlock(_read_pipe_fd) == false)
            {
                LOG_ERROR("CloudBackupServer Initialize ERROR, read pipe SetNonBlock error");
                exit(INIT_PIPE_ERROR);
            }
            if (_epoller.EpollAdd(_read_pipe_fd, EPOLLIN | EPOLLET) == false)
            {
                LOG_ERROR("CloudBackupServer Initialize ERROR, EpollAdd read pipe error");
                exit(INIT_PIPE_ERROR);
            }
            _socket.InitSocket();
            _socket.Bind(_server_port);
            _events = new epoll_event[_maxevents];
            if (_events == nullptr)
                LOG_ERROR("CloudBackupServer Initialize ERROR, memory allocation failed");
            LOG_INFO("CloudBackupServer Initialize Succeed, bind on %d port", _server_port);
        }
        // 服务器析构时清理残留数据，防止内存泄漏
        void DestoryServer()
        {
            if (_events != nullptr)
                delete[] _events;
            if (_read_pipe_fd != -1)
                close(_read_pipe_fd);
            if (_write_pipe_fd != -1)
                close(_write_pipe_fd);
        }
        // 启动服务器，开始listen
        void StartServer()
        {
            _socket.Listen(Config::GetInstance()->GetListenQueueSize());
            if (SetNonBlock(_socket.GetSocketet()) == false)
            {
                LOG_ERROR("CloudBackupServer Start ERROR, socket SetNonBlock error");
                exit(SERVER_START_ERROR);
            }
            if (_epoller.EpollAdd(_socket.GetSocketet(), EPOLLIN | EPOLLET) == false)
            {
                LOG_ERROR("CloudBackupServer Start ERROR, EpollAdd socket error");
                exit(SERVER_START_ERROR);
            }
            LOG_INFO("CloudBackupServer Start Listen Succeed");
        }
        // 循环监听就绪事件并处理
        void Dispatcher()
        {
            while (true)
            {
                int n = _epoller.EpollBlockWait(_events, _maxevents);
                if (n == -1)
                    LOG_ERROR("Dispatcher ERROR, EpollBlockWait ERROR");
                else if (n == 0)
                    LOG_INFO("Dispatcher Looping, EpollBlockWait Timeout");
                else if (n > 0)
                {
                    for (int pos = 0; pos < n; pos++)
                    {
                        if (_events[pos].data.fd == _socket.GetSocketet())
                        {
                            LOG_INFO("Dispatcher INFO, server accepter fd:%d event ready", _events[pos].data.fd);
                            if (_events[pos].events & EPOLLIN)
                                Accepter();
                        }
                        else if (_events[pos].data.fd == _read_pipe_fd)
                        {
                            LOG_INFO("Dispatcher INFO, pipe reader fd:%d event ready", _events[pos].data.fd);
                            if (_events[pos].events & EPOLLIN)
                                PipeDataReader();
                        }
                        else
                        {
                            if (_events[pos].events & EPOLLIN)
                            {
                                LOG_INFO("Dispatcher INFO, net_fd:%d reader socket event ready", _events[pos].data.fd);
                                NetReader(_events[pos].data.fd);
                            }
                            if (_events[pos].events & EPOLLOUT)
                            {
                                LOG_INFO("Dispatcher INFO, net_fd:%d writer socket event ready", _events[pos].data.fd);
                                NetWriter(_events[pos].data.fd);
                            }
                        }
                    }
                }
            }
        }
        // 从底层获取新到来的连接，并将其放入epoll监听队列中
        void Accepter()
        {
            while (true)
            {
                std::string client_ip;
                uint16_t client_port;
                int new_net_fd = _socket.Accept(&client_ip, &client_port);
                if (new_net_fd == -1)
                {
                    if (errno == EAGAIN || errno == EWOULDBLOCK)
                        break;
                    else if (errno == EINTR)
                        continue;
                    else
                        LOG_ERROR("Accepter ERROR, accept error:%d  message:%s", errno, strerror(errno));
                    continue;
                }
                LOG_INFO("New connection accepted, client_ip:%s client_port:%d new_net_fd:%d", client_ip.c_str(), client_port, new_net_fd);
                AddConnection(new_net_fd, client_ip, client_port);
            }
        }
        // 将新的连接放入epoll监听队列和连接池中
        void AddConnection(int net_fd, const std::string &client_ip, uint16_t client_port)
        {
            if (SetNonBlock(net_fd) == false)
            {
                LOG_WARN("Accepter ERROR, net_fd SetNonBlock error");
                close(net_fd);
                return;
            }
            if (_epoller.EpollAdd(net_fd, EPOLLIN | EPOLLET) == false)
            {
                LOG_WARN("Accepter ERROR, EpollAdd ERROR");
                close(net_fd);
                return;
            }
            ++_record_net_fd_use_time[net_fd] %= 10000;
            std::string net_fd_identifier = std::to_string(net_fd) + "_" + std::to_string(_record_net_fd_use_time[net_fd]);
            HTTPConnection::ptr new_connection = std::make_shared<HTTPConnection>(net_fd_identifier, _write_pipe_fd, client_ip, client_port);
            _connections[net_fd] = new_connection;
        }
        // 约定pipe内的数据格式为"wfd_usetime,rfd_usetime,cfd_usetime,..."如："w12_3"，每个fd之间用逗号分隔，多个工作线程通过pipe告知主线程哪个net_fd有新的事件需要处理，
        // 'w'表示该net_fd有数据需要发送，'r'表示该net_fd可以继续处理新的数据
        // 读取管道中的数据并放入_read_pipe_buffer中
        void PipeDataReader()
        {
            static const int tmp_buffer_size = 1024;
            static char tmp_buffer[tmp_buffer_size];
            while (true)
            {
                ssize_t read_bytes = read(_read_pipe_fd, tmp_buffer, tmp_buffer_size - 1);
                if (read_bytes < 0)
                {
                    if (errno == EAGAIN || errno == EWOULDBLOCK)
                        break;
                    else if (errno == EINTR)
                        continue;
                    LOG_WARN("PipeDataReader ERROR, read pipe error:%d message:%s", errno, strerror(errno));
                    break;
                }
                else if (read_bytes == 0)
                {
                    LOG_ERROR("PipeDataReader ERROR, write pipe fd closed unexpectedly");
                    close(_read_pipe_fd);
                    _write_pipe_fd = -1;
                    _read_pipe_fd = -1;
                    break;
                }
                else if (read_bytes > 0)
                {
                    LOG_INFO("PipeDataReader INFO, read %d bytes from pipe", read_bytes);
                    tmp_buffer[read_bytes] = '\0';
                    _read_pipe_buffer += tmp_buffer;
                }
            }
            PipeDataHandler();
        }
        // 处理从管道中获取上来的数据
        void PipeDataHandler()
        {
            if (_read_pipe_buffer.empty())
                return;
            int pos = 0;
            while (pos < _read_pipe_buffer.size())
            {
                int comma_pos = _read_pipe_buffer.find(',', pos);
                if (comma_pos == std::string::npos)
                    break;
                std::string net_fd_str = _read_pipe_buffer.substr(pos, comma_pos - pos);
                pos = comma_pos + 1;
                if (net_fd_str.size() < 4)
                    continue;
                char op = net_fd_str[0];
                std::string net_fd_identifier = net_fd_str.substr(1);
                int underscore_pos = net_fd_identifier.find_last_of('_');
                if (underscore_pos == std::string::npos)
                    continue;
                int net_fd = std::stoi(net_fd_identifier.substr(0, underscore_pos));
                int use_time = std::stoi(net_fd_identifier.substr(underscore_pos + 1));
                if (net_fd < 0 || _connections.find(net_fd) == _connections.end() || use_time != _record_net_fd_use_time[net_fd])
                    continue;
                LOG_INFO("PipeDataHandler INFO, handle net_fd:%d, op:%c", net_fd, op);
                if (op == 'r')
                    NetReader(net_fd);
                else if (op == 'w')
                    NetWriter(net_fd);
                else if (op == 'c')
                    NetExcepter(net_fd);
            }
            pos = _read_pipe_buffer.find_last_of(',');
            if (pos != std::string::npos)
                _read_pipe_buffer.erase(0, pos + 1);
        }
        // 处理网络连接的读事件
        void NetReader(int net_fd)
        {
            if (_connections.find(net_fd) == _connections.end())
            {
                LOG_WARN("NetReader ERROR, net_fd not found in _connections: %d", net_fd);
                return;
            }
            HTTPConnection::ptr connection = _connections[net_fd];
            static const long long tmp_buffer_size = Config::GetInstance()->GetTCPBufferReadSize();
            char tmp_buffer[tmp_buffer_size] = {0};
            while (true)
            {
                ssize_t read_bytes = read(net_fd, tmp_buffer, tmp_buffer_size - 1);
                if (read_bytes < 0)
                {
                    if (errno == EAGAIN || errno == EWOULDBLOCK)
                        break;
                    else if (errno == EINTR)
                        continue;
                    LOG_WARN("NetReader ERROR, read error:%d message:%s", errno, strerror(errno));
                    NetExcepter(net_fd);
                    return;
                }
                else if (read_bytes == 0)
                {
                    LOG_INFO("NetReader INFO, client closed connection client ip:%s client_port:%d net_fd:%d",
                             connection->_client_ip.c_str(), connection->_client_port, net_fd);
                    NetExcepter(net_fd);
                    return;
                }
                else if (read_bytes > 0)
                {
                    tmp_buffer[read_bytes] = '\0';
                    LOG_INFO("NetReader INFO, read %d bytes from net_fd:%d", read_bytes, net_fd);
                    // LOG_DEBUG("%s", tmp_buffer);
                    std::unique_lock<std::mutex> request_lock(connection->_request_mutex);
                    for (int i = 0; i < read_bytes; ++i)
                        connection->_request_buffer += tmp_buffer[i];
                    if (connection->_is_processing == false)
                    {
                        connection->_is_processing = true;
                        TaskThreadPool::GetInstance()->push(std::bind(&HTTPConnection::handler, connection));
                    }
                }
            }
        }
        // 处理网络连接的写事件
        void NetWriter(int net_fd)
        {
            if (_connections.find(net_fd) == _connections.end())
            {
                LOG_WARN("NetWriter fail, net_fd not found in _connections: %d", net_fd);
                return;
            }
            HTTPConnection::ptr connection = _connections[net_fd];
            std::unique_lock<std::mutex> response_lock(connection->_response_mutex);
            if (connection->_response_buffer.empty())
            {
                LOG_WARN("NetWriter WARN, response buffer is empty for net_fd: %d", net_fd);
                return;
            }
            while (true)
            {
                ssize_t write_bytes = write(net_fd, connection->_response_buffer.c_str(), connection->_response_buffer.size());
                if (write_bytes < 0)
                {
                    if (errno == EAGAIN || errno == EWOULDBLOCK)
                        break;
                    else if (errno == EINTR)
                        continue;
                    LOG_WARN("NetWriter ERROR, write error:%d message:%s", errno, strerror(errno));
                    NetExcepter(net_fd);
                    break;
                }
                else if (write_bytes >= 0)
                {
                    LOG_INFO("NetWriter INFO, write %d bytes to net_fd:%d", write_bytes, net_fd);
                    // LOG_DEBUG("%s", connection->_response_buffer.substr(0, write_bytes).c_str());
                    connection->_response_buffer.erase(0, write_bytes);
                    if (connection->_response_buffer.empty())
                    {
                        if (_epoller.EpollMod(net_fd, EPOLLIN | EPOLLET) == false)
                            LOG_WARN("NetWriter WARN, EpollMod net_fd:%d to EPOLLIN failed", net_fd);
                    }
                    else
                    {
                        if (_epoller.EpollMod(net_fd, EPOLLIN | EPOLLOUT | EPOLLET) == false)
                            LOG_WARN("NetWriter WARN, EpollMod net_fd:%d to EPOLLIN|EPOLLOUT failed", net_fd);
                    }
                    break;
                }
            }
        }
        // 网络连接异常处理
        void NetExcepter(int net_fd)
        {
            if (net_fd < 0)
            {
                LOG_WARN("NetExcepter WARN, net_fd:%d is not valid", net_fd);
                return;
            }
            if (_epoller.EpollDel(net_fd) == false)
                LOG_WARN("NetExcepter WARN, EpollDel net_fd:%d failed", net_fd);
            if (_connections.find(net_fd) == _connections.end())
                LOG_WARN("NetExcepter WARN, net_fd not found in _connections: %d", net_fd);
            else
            {
                _connections[net_fd]->_is_closed = true;
                LOG_INFO("connection close, client ip:%s client_port:%d", _connections[net_fd]->_client_ip.c_str(), _connections[net_fd]->_client_port);
                _connections.erase(net_fd);
            }
            close(net_fd);
        }

    private:
        uint16_t _server_port;
        NetSocketUtil _socket;
        int _write_pipe_fd;
        int _read_pipe_fd;
        std::string _read_pipe_buffer;
        EpollUtil _epoller;
        epoll_event *_events = nullptr;
        int _maxevents = Config::GetInstance()->GetEpollEventsSize();
        std::unordered_map<int, int> _record_net_fd_use_time;
        std::unordered_map<int, HTTPConnection::ptr> _connections;
    };
}

#endif