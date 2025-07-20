#ifndef CLOUD_BACKUP_DATA_MANAGER_HPP
#define CLOUD_BACKUP_DATA_MANAGER_HPP

#include "util.hpp"
#include "config.hpp"
#include <shared_mutex>
#include <condition_variable>

namespace cloud_backup
{
    struct BackupInfo
    {
        using ptr = std::shared_ptr<BackupInfo>;
        bool _compress_flag = false; // 文件是否压缩存储标志位，默认不压缩
        size_t _fsize;               // 文件大小
        time_t _atime;               // 文件最后一次访问时间
        time_t _mtime;               // 文件最后一次修改时间
        std::string _filename;       // 文件名（仅文件名，不含路径）
        // 根据传入的文件路径，用获取到的文件属性信息创建 BackupInfo 对象，并返回其指针
        static BackupInfo::ptr NewBackupInfo(const std::string &filepath)
        {
            FileUtil file(filepath);
            if (file.Exists() == false)
            {
                LOG_ERROR("NewBackupInfo error, file not exist: %s", file.GetFilePath().c_str());
                return BackupInfo::ptr(nullptr);
            }
            BackupInfo::ptr info(new BackupInfo());
            info->_fsize = file.GetFileSize();
            info->_atime = file.LastAccTime();
            info->_mtime = file.LastModTime();
            info->_filename = file.GetFileName();
            if (info->_fsize == -1 || info->_atime == -1 || info->_mtime == -1 || info->_filename == "")
            {
                LOG_ERROR("NewBackupInfo error, get file attributes failed: %s", file.GetFilePath().c_str());
                return BackupInfo::ptr(nullptr);
            }
            return info;
        }
    };

    // DataManager类是用于记录所有存储到云端的备份文件属性信息的单例类
    // 文件的属性信息会由DataManager管理并单独存放到文件中保存下来，后序如果有文件要压缩存储也可以在不解压的状态下获取其属性信息
    // 整个云备份系统维护一个原则：只要是在DataManager管理的文件那么在云服务器上肯定有
    // (所以上传的文件要先落盘才会添加到DataManager中，删除的文件要先从DataManager中删除才会从云服务器上删除)
    // 未来针对大文件的优化:可以前面一小部分正常存储，后面大半压缩存储，这样可以快速的给予client响应，后序再通过流式的边解压边发送持续传输大文件
    class DataManager
    {
    public:
        using ptr = std::shared_ptr<DataManager>;
        ~DataManager() { _file_storage_thread.join(); }
        // 向数据管理器中插入一个新的文件备份信息，失败返回false
        bool Insert(const BackupInfo &info)
        {
            std::unique_lock<std::shared_mutex> write_lock(_rwlock);
            if (_hash.find(info._filename) != _hash.end())
            {
                LOG_WARN("Insert error, file already exists: %s", info._filename.c_str());
                return false;
            }
            _hash[info._filename] = info;
            _is_dirty = true;
            _file_storage_cond.notify_all();
            return true;
        }
        // 根据文件名在数据管理器中删除一个文件备份信息，失败返回false
        bool Delete(const std::string &filename)
        {
            std::unique_lock<std::shared_mutex> write_lock(_rwlock);
            if (_hash.erase(filename) == 0)
            {
                LOG_WARN("Delete error, file not found: %s", filename.c_str());
                return false;
            }
            _is_dirty = true;
            _file_storage_cond.notify_all();
            return true;
        }
        // 根据文件名获取文件备份属性信息，失败返回nullptr
        BackupInfo::ptr GetOneByFileName(const std::string &filename)
        {
            std::shared_lock<std::shared_mutex> read_lock(_rwlock);
            if (_hash.find(filename) != _hash.end())
                return std::make_shared<BackupInfo>(_hash[filename]);
            LOG_WARN("GetOneByFileName error, file not found: %s", filename.c_str());
            return BackupInfo::ptr(nullptr);
        }
        // 从数据管理器中获取所有的文件备份属性信息
        bool GetAll(std::vector<BackupInfo> *infos)
        {
            infos->clear();
            std::shared_lock<std::shared_mutex> read_lock(_rwlock);
            for (auto &[filename, info] : _hash)
                infos->push_back(info);
            return true;
        }

    private:
        DataManager(const DataManager &) = delete;
        DataManager &operator=(const DataManager &) = delete;
        DataManager(const std::string &filepath) : _file(filepath),
                                                   _file_storage_thread(&DataManager::FileStorageThread, this)
        {
            // 初始化加载数据管理器文件中的内容到程序中
            std::string content;
            if (!_file.GetContent(&content))
            {
                LOG_FATAL("DataManager initialization error, GetContent error");
                exit(DATA_MANAGER_INIT_ERROR);
            }
            if (!content.empty())
            {
                Json::Value root;
                if (!JsonUtil::Deserialize(content, &root))
                {
                    LOG_FATAL("DataManager initialization error, JsonUtil::Deserialize error");
                    exit(DATA_MANAGER_INIT_ERROR);
                }
                for (auto &item : root)
                {
                    BackupInfo info;
                    info._compress_flag = item["compress_flag"].asBool();
                    info._fsize = item["fsize"].asUInt64();
                    info._atime = item["atime"].asInt64();
                    info._mtime = item["mtime"].asInt64();
                    info._filename = item["filename"].asString();
                    _hash[info._filename] = info;
                }
            }
            LOG_INFO("DataManager initialized successfully, loaded %zu files", _hash.size());
        }
        // 异步文件存储线程执行的函数
        void FileStorageThread()
        {
            while (1)
            {
                Json::Value root;
                {
                    std::shared_lock<std::shared_mutex> read_lock(_rwlock);
                    _file_storage_cond.wait(read_lock, [&]()
                                            { return _is_dirty; });
                    // 将所有的文件备份属性信息序列化成JSON格式字符串
                    for (const auto &[filename, info] : _hash)
                    {
                        Json::Value item;
                        item["compress_flag"] = info._compress_flag;
                        item["fsize"] = static_cast<Json::UInt64>(info._fsize);
                        item["atime"] = static_cast<Json::Int64>(info._atime);
                        item["mtime"] = static_cast<Json::Int64>(info._mtime);
                        item["filename"] = info._filename;
                        root.append(item);
                    }
                    _is_dirty = false;
                }
                std::string infos_str;
                if (!JsonUtil::Serialize(root, &infos_str))
                {
                    LOG_WARN("Storage error, serialize to JSON failed");
                    continue;
                }
                // 将备份属性信息的字符串写入到文件中
                if (!_file.SetContent(infos_str))
                {
                    LOG_WARN("Storage error, write to file failed: %s", _file.GetFilePath().c_str());
                    continue;
                }
            }
        }

    public:
        static DataManager::ptr GetInstance()
        {
            static DataManager::ptr data_manager(new DataManager(Config::GetInstance()->GetDataManagerFilePath()));
            if (data_manager == nullptr)
                LOG_FATAL("create DataManager object fail");
            return data_manager;
        }

    private:
        FileUtil _file;                                    // 将来将文件持久化存储的资源路径
        std::unordered_map<std::string, BackupInfo> _hash; // 通过hash表用文件名快速的访问到文件属性信息
        std::shared_mutex _rwlock;                         // 读写锁，保证多线程环境下对_hash的安全访问
        bool _is_dirty = false;                            // 标记数据管理器是否需要存储到文件中，若有修改则设置为true
        std::condition_variable_any _file_storage_cond;    // 条件变量，异步的文件存储线程在不满足条件时就在该条件变量下等待
        std::thread _file_storage_thread;                  // 异步的文件存储线程
    };
}

#endif