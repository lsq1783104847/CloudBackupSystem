#ifndef CLOUD_BACKUP_HPP
#define CLOUD_BACKUP_HPP

#include "util.hpp"
#include "config.hpp"
#include "data_manager.hpp"
#include "llhttp.h"

namespace cloud_backup
{
    class CloudBackup
    {
    public:
        CloudBackup(const std::string &WorkPath)
        {
            // 初始化日志器
            cloud_backup::InitCloudBackupLogger();
            // 将进程变成守护进程

            // 读取配置文件修改日志器的落地方向
            auto config = Config::GetInstance();
            if (ModifyCloudBackupLoggerSinks(config->GetLogFilePath()) == false)
            {
                LOG_ERROR("ModifyCloudBackupLoggerSinks error, exit");
                exit(LOAD_CONFIG_FILE_ERROR);
            }
            // 读取配置文件获取服务器ip和端口号
            _server_ip = config->GetServerIp();
            _server_port = config->GetServerPort();
        }
        ~CloudBackup() {}

    private:
        // 初始化服务器，设置处理请求的回调函数
        void InitializeServer()
        {
        }
        // 启动服务器，监听指定的ip和端口号
        void StartServer()
        {
            InitializeServer();

            LOG_INFO("CloudBackup server start succeed, listening on %s:%d", _server_ip.c_str(), _server_port);
        }

    private:
        uint16_t _server_port;
        std::string _server_ip;
    };
}

#endif