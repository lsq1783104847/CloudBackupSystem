#ifndef CLOUD_BACKUP_CONFIG_HPP
#define CLOUD_BACKUP_CONFIG_HPP

#include "util.hpp"

namespace cloud_backup
{
    class Config
    {
    private:
        const std::string _config_file = "./cloud_backup.cnf";

    public:
        ~Config() {}
        static std::shared_ptr<Config> GetInstance()
        {
            static std::shared_ptr<Config> cnf(new Config());
            if (cnf == nullptr)
                LOG_FATAL("create Config object fail");
            return cnf;
        }

        std::string GetServerIp() { return _server_ip; }
        uint16_t GetServerPort() { return _server_port; }
        time_t GetHotTime() { return _hot_time; }
        std::string GetCompressionFileSuffix() { return _compression_file_suffix; }
        std::string GetLogFilePath() { return _log_filepath; }
        std::string GetDataManagerFilePath() { return _data_manager_filepath; }

    private:
        Config() { ReadConfigFile(); }
        Config(const Config &cnf) = delete;
        Config &operator=(const Config &cnf) = delete;

        bool ReadConfigFile()
        {
            FileUtil file(_config_file);
            std::string content;
            if (file.GetContent(&content) == false)
            {
                LOG_ERROR("GetContent error, ReadConfigFile fail");
                return false;
            }
            Json::Value root;
            if (JsonUtil::Deserialize(content, &root) == false)
            {
                LOG_ERROR("JsonUtil::Deserialize error, ReadConfigFile fail");
                return false;
            }
            _server_ip = root["server_ip"].asString();
            _server_port = root["server_port"].asUInt();
            _hot_time = root["hot_time"].asInt64();
            _compression_file_suffix = root["compression_file_suffix"].asString();
            _log_filepath = root["log_filepath"].asString();
            _data_manager_filepath = root["data_manager_filepath"].asString();

            return true;
        }

    private:
        std::string _server_ip;               // 服务器监听的IP地址
        uint16_t _server_port;                // 服务器bind的端口号
        time_t _hot_time;                     // 热点文件最长存在时间（超过该时间文件未被访问就自动压缩该文件，单位秒）
        std::string _compression_file_suffix; // 压缩文件的后缀名，如".lz"
        std::string _log_filepath;            // 日志文件路径，存储日志信息
        std::string _data_manager_filepath;   // 数据管理器文件路径，存储所有备份文件的属性信息
    };
}
#endif