#ifndef CLOUD_BACKUP_LOG_HPP
#define CLOUD_BACKUP_LOG_HPP

#include "./log_system/log.h"

namespace cloud_backup
{
#define CLOUD_BACKUP_LOGGER_NAME "AsynCloudBackupLogger"
    bool InitCloudBackupLogger(const std::string &LogFilePath)
    {
        std::vector<log_system::LogSink::ptr> sinks{
            log_system::get_sink<log_system::StdoutSink>(),
            log_system::get_sink<log_system::RollFileSinkBySize>(LogFilePath, 1024 * 1024 * 10)};
        return log_system::add_logger(CLOUD_BACKUP_LOGGER_NAME, log_system::ASYNC_LOGGER, sinks, log_system::Level::DEBUG);
    }
#define LOG_DEBUG(msg, ...) LOG_SYSTEM_LOG_DEBUG(log_system::get_logger(CLOUD_BACKUP_LOGGER_NAME), msg, ##__VA_ARGS__)
#define LOG_INFO(msg, ...) LOG_SYSTEM_LOG_DEBUG(log_system::get_logger(CLOUD_BACKUP_LOGGER_NAME), msg, ##__VA_ARGS__)
#define LOG_WARN(msg, ...) LOG_SYSTEM_LOG_DEBUG(log_system::get_logger(CLOUD_BACKUP_LOGGER_NAME), msg, ##__VA_ARGS__)
#define LOG_ERROR(msg, ...) LOG_SYSTEM_LOG_DEBUG(log_system::get_logger(CLOUD_BACKUP_LOGGER_NAME), msg, ##__VA_ARGS__)
#define LOG_FATAL(msg, ...) LOG_SYSTEM_LOG_DEBUG(log_system::get_logger(CLOUD_BACKUP_LOGGER_NAME), msg, ##__VA_ARGS__)
}

#endif