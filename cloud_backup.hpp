#ifndef CLOUD_BACKUP_HPP
#define CLOUD_BACKUP_HPP

#include "util.hpp"
#include "config.hpp"
#include "data_manager.hpp"

namespace cloud_backup
{
    class CloudBackup
    {
    public:
        CloudBackup() {}
        ~CloudBackup() {}

    private:
        // 修改日志器的落地方向，在读取配置文件的日志输出路径后将其添加到日志落地方向中，若传入的路径有问题则不修改并返回false
        bool ModifyCloudBackupLoggerSinks(const std::string &log_path)
        {
            std::string file_path = FileUtil(log_path).GetFilePath();
            if (file_path == "")
            {
                LOG_ERROR("ModifyCloudBackupLogger error, log_path is error path:[%s]", log_path.c_str());
                return false;
            }
            log_system::delete_logger(CLOUD_BACKUP_LOGGER_NAME); // 删除原有的日志器
            // 创建新的日志器，添加标准输出和文件输出的日志落地方向
            std::vector<log_system::LogSink::ptr> sinks{log_system::get_sink<log_system::StdoutSink>()};
            log_system::LogSink::ptr newSink = log_system::get_sink<log_system::FileSink>(file_path);
            if (newSink == nullptr)
            {
                LOG_ERROR("ModifyCloudBackupLogger error, get_sink failed");
                return false;
            }
            sinks.push_back(newSink);
            return log_system::add_logger(CLOUD_BACKUP_LOGGER_NAME, CLOUD_BACKUP_LOGGER_TYPE, sinks, CLOUD_BACKUP_LOGGER_LEVEL);
        }
    };
}

#endif