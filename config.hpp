#ifndef CONFIG_HPP
#define CONFIG_HPP

#include "util.hpp"

namespace cloud_backup
{
    class Config
    {
    public:
        ~Config() {}
        static std::shared_ptr<Config> GetInstance()
        {
            static std::shared_ptr<Config> cnf(new Config());
            if (cnf == nullptr)
                LOG_DEBUG(log_system::get_logger("root"), "create Config object fail");
            return cnf;
        }

        std::string GetServerIp() { return _server_ip; }
        uint16_t GetServerPort() { return _server_port; }
        size_t GetHotTime() { return _hot_time; }
        std::string GetCompressionFileSuffix() { return _compression_file_suffix; }

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
                LOG_DEBUG(log_system::get_logger("root"), "GetContent error, ReadConfigFile fail");
                return false;
            }
            Json::Value value;
            if (JsonUtil::Deserialize(content, &value) == false)
            {
                LOG_DEBUG(log_system::get_logger("root"), "JsonUtil::Deserialize error, ReadConfigFile fail");
                return false;
            }
            _server_ip = value["server_ip"].asString();
            _server_port = value["server_port"].asUInt();
            _hot_time = value["hot_time"].asUInt();
            _compression_file_suffix = value["compression_file_suffix"].asString();

            return true;
        }

    private:
        std::string _server_ip;
        uint16_t _server_port;
        size_t _hot_time;
        std::string _compression_file_suffix;

    private:
        const std::string _config_file = "./cloud_backup.cnf";
    };
}
#endif