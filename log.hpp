#ifndef CLOUD_BACKUP_LOG_HPP
#define CLOUD_BACKUP_LOG_HPP

#include "log_system/log.h"

namespace cloud_backup
{
#define CLOUD_BACKUP_LOGGER_NAME "CloudBackupLogger"       // 日志器名称
#define CLOUD_BACKUP_LOGGER_LEVEL log_system::Level::DEBUG // 日志器输出等级
#define CLOUD_BACKUP_LOGGER_TYPE log_system::ASYNC_LOGGER  // 日志器类型(同步或异步)

#define LOG_DEBUG(msg, ...) LOG_SYSTEM_LOG_DEBUG(log_system::get_logger(CLOUD_BACKUP_LOGGER_NAME), msg, ##__VA_ARGS__)
#define LOG_INFO(msg, ...) LOG_SYSTEM_LOG_INFO(log_system::get_logger(CLOUD_BACKUP_LOGGER_NAME), msg, ##__VA_ARGS__)
#define LOG_WARN(msg, ...) LOG_SYSTEM_LOG_WARN(log_system::get_logger(CLOUD_BACKUP_LOGGER_NAME), msg, ##__VA_ARGS__)
#define LOG_ERROR(msg, ...) LOG_SYSTEM_LOG_ERROR(log_system::get_logger(CLOUD_BACKUP_LOGGER_NAME), msg, ##__VA_ARGS__)
#define LOG_FATAL(msg, ...) LOG_SYSTEM_LOG_FATAL(log_system::get_logger(CLOUD_BACKUP_LOGGER_NAME), msg, ##__VA_ARGS__)

    // 初始化日志器，在没读取配置文件的日志输出路径前将日志输出到标准输出
    bool InitCloudBackupLogger()
    {
        std::vector<log_system::LogSink::ptr> sinks{log_system::get_sink<log_system::StdoutSink>()};
        return log_system::add_logger(CLOUD_BACKUP_LOGGER_NAME, log_system::SYNC_LOGGER, sinks, CLOUD_BACKUP_LOGGER_LEVEL);
    }

    // 修改日志器的落地方向，在读取配置文件的日志输出路径后将其添加到日志落地方向中，并以滚动文件的方式输出日志，若传入的路径有问题则不修改并返回false
    bool ModifyCloudBackupLoggerSinks(const std::string &log_path, long long roll_file_size = 10 * 1024 * 1024)
    {
        log_system::LogSink::ptr newSink = log_system::get_sink<log_system::RollFileSinkBySize>(log_path, roll_file_size);
        if (newSink == nullptr)
        {
            LOG_ERROR("ModifyCloudBackupLogger error, get_sink failed");
            return false;
        }
        std::vector<log_system::LogSink::ptr> sinks{log_system::get_sink<log_system::StdoutSink>()};
        sinks.push_back(newSink);
        log_system::delete_logger(CLOUD_BACKUP_LOGGER_NAME); // 删除原有的日志器
        return log_system::add_logger(CLOUD_BACKUP_LOGGER_NAME, CLOUD_BACKUP_LOGGER_TYPE, sinks, CLOUD_BACKUP_LOGGER_LEVEL);
    }
}

#endif