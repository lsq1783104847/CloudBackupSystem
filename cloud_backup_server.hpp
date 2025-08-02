#ifndef CLOUD_BACKUP_SERVER_HPP
#define CLOUD_BACKUP_SERVER_HPP

#include <functional>
#include "util.hpp"
#include "config.hpp"
#include "data_manager.hpp"
#include "llhttp.h"

namespace cloud_backup
{
    struct Channel
    {
        using func_t = std::function<void()>;
        using ptr = std::shared_ptr<Channel>;
        Channel(int fd, uint32_t envents) : _fd(fd), _envents(envents) {}
        ~Channel() { close(_fd); }
        int _fd;
        uint32_t _envents;
        std::string _inbuffer;
        std::string _outbuffer;
        func_t _reader;
        func_t _writer;
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
                else if(n > 0)
                {
                    for(int pos = 0;pos < n;pos++)
                    {
                        
                    }
                }
            }
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