#ifndef CLOUD_BACKUP_ERROR_HPP
#define CLOUD_BACKUP_ERROR_HPP

enum
{
    // 错误码
    LOGGER_INIT_ERROR = 1,   // 日志器初始化失败
    DATA_MANAGER_INIT_ERROR, // 数据管理器初始化失败
    LOAD_CONFIG_FILE_ERROR,  // 加载配置文件失败
    SERVER_LISTEN_ERROR,     // 服务器监听失败
};

#endif