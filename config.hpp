#ifndef CLOUD_BACKUP_CONFIG_HPP
#define CLOUD_BACKUP_CONFIG_HPP

#include "util.hpp"

namespace cloud_backup
{
    class Config
    {
    private:
        const std::string _config_file_path = "./config.json";

    public:
        ~Config() {}
        static std::shared_ptr<Config> GetInstance()
        {
            static std::shared_ptr<Config> cnf(new Config());
            if (cnf == nullptr)
            {
                LOG_FATAL("create Config object fail");
                exit(NEW_OBJECT_ERROR);
            }
            return cnf;
        }

        uint16_t GetServerPort() { return _server_port; }
        std::string GetLogFilePath() { return _log_filepath; }
        long long GetRollFileSize() { return _roll_file_size; }
        size_t GetLRUFileCapacity() { return _LRU_file_capacity; }
        long long GetLRUFileContentSize() { return _LRU_file_content_size; }
        long long GetMaxFileReadSize() { return _max_file_read_size; }
        int GetThreadPoolQueueCapacity() { return _thread_pool_queue_capacity; }
        int GetThreadPoolThreadsSize() { return _thread_pool_threads_size; }
        int GetListenQueueSize() { return _listen_queue_size; }
        int GetEpollEventsSize() { return _epoll_events_size; }
        std::string GetDataManagerFilePath() { return _data_manager_filepath; }
        std::string GetBackupFileDir() { return _backup_file_dir; }
        std::string GetUploadUrlPrefix() { return _upload_url_prefix; }
        std::string GetShowListUrlPrefix() { return _showlist_url_prefix; }
        std::string GetDownloadUrlPrefix() { return _download_url_prefix; }
        std::string GetDeleteUrlPrefix() { return _delete_url_prefix; }

    private:
        Config() { ReadConfigFile(); }
        Config(const Config &cnf) = delete;
        Config &operator=(const Config &cnf) = delete;

        bool ReadConfigFile()
        {
            FileUtil file(_config_file_path);
            std::string content;
            if (file.GetContent(&content) == false)
            {
                LOG_ERROR("GetContent error, ReadConfigFile fail");
                exit(LOAD_CONFIG_FILE_ERROR);
            }
            Json::Value root;
            if (JsonUtil::Deserialize(content, &root) == false)
            {
                LOG_ERROR("JsonUtil::Deserialize error, ReadConfigFile fail");
                exit(LOAD_CONFIG_FILE_ERROR);
            }
            _server_port = root["server_port"].asUInt();
            _log_filepath = root["log_filepath"].asString();
            _roll_file_size = root["roll_file_size"].asInt64();
            _LRU_file_capacity = root["LRU_file_capacity"].asUInt();
            _LRU_file_content_size = root["LRU_file_content_size"].asInt64();
            _max_file_read_size = root["max_file_read_size"].asInt64();
            _thread_pool_queue_capacity = root["thread_pool_queue_capacity"].asInt();
            _thread_pool_threads_size = root["thread_pool_threads_size"].asInt();
            _listen_queue_size = root["listen_queue_size"].asInt();
            _epoll_events_size = root["epoll_events_size"].asInt();
            _data_manager_filepath = root["data_manager_filepath"].asString();
            _backup_file_dir = root["backup_file_dir"].asString();
            _upload_url_prefix = root["upload_url_prefix"].asString();
            _showlist_url_prefix = root["showlist_url_prefix"].asString();
            _download_url_prefix = root["download_url_prefix"].asString();
            _delete_url_prefix = root["delete_url_prefix"].asString();
            return true;
        }

    private:
        uint16_t _server_port;              // 服务器bind的端口号
        std::string _log_filepath;          // 日志文件路径，存储日志信息
        long long _roll_file_size;          // 日志文件滚动大小，单位为字节
        size_t _LRU_file_capacity;          // LRU存储的热点文件数量
        long long _LRU_file_content_size;   // LRU中缓存的文件的内容大小
        long long _max_file_read_size;      // 单次读取文件的最大字节数
        int _thread_pool_queue_capacity;    // 线程池任务队列容量
        int _thread_pool_threads_size;      // 线程池中的线程数量
        int _listen_queue_size;             // listen socket下阻塞等待队列的最大大小
        int _epoll_events_size;             // epoll每次wait能够返回的最多事件数
        std::string _data_manager_filepath; // 数据管理器文件路径，存储所有备份文件的属性信息
        std::string _backup_file_dir;       // 备份文件存储目录
        std::string _upload_url_prefix;     // 文件上传请求的url前缀
        std::string _showlist_url_prefix;   // 文件列表展示请求的url前缀
        std::string _download_url_prefix;   // 文件下载请求的url前缀
        std::string _delete_url_prefix;     // 文件删除请求的url前缀
    };
}
#endif