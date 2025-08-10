#ifndef CLOUD_BACKUP_ERROR_HPP
#define CLOUD_BACKUP_ERROR_HPP

enum
{
    // 错误码
    DAEMON_ERROR = 1,        // 守护进程创建失败
    LOGGER_INIT_ERROR,       // 日志器初始化失败
    DATA_MANAGER_INIT_ERROR, // 数据管理器初始化失败
    LOAD_CONFIG_FILE_ERROR,  // 加载配置文件失败
    SERVER_LISTEN_ERROR,     // 服务器监听失败
    INIT_SOCKET_ERROR,       // 初始化socket失败
    BIND_SOCKET_ERROR,       // 绑定socket失败
    LISTEN_SOCKET_ERROR,     // 监听socket失败
    NEW_OBJECT_ERROR,        // 创建对象失败
    EPOLL_CREATE_ERROR,      // 创建epoll失败
    INIT_PIPE_ERROR,         // 初始化管道失败
    SERVER_START_ERROR,      // 服务器启动失败
};

#endif