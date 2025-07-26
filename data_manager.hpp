#ifndef CLOUD_BACKUP_DATA_MANAGER_HPP
#define CLOUD_BACKUP_DATA_MANAGER_HPP

#include "util.hpp"
#include "config.hpp"
#include <shared_mutex>
#include <condition_variable>
#include <unordered_set>

namespace cloud_backup
{
    struct BackupInfoNode
    {
        std::string _filename;     // 文件名(仅文件名，不含路径)
        int64_t _size = -1;        // 文件大小(单位:字节)(为-1时表示正在上传该文件但还未完成)
        time_t _time = time_t(-1); // 文件上传完成的时间(为-1时同上)
    };
    struct DataManagerNode
    {
        using ptr = std::shared_ptr<DataManagerNode>;
        DataManagerNode()
        {
            int err = pthread_rwlock_init(&_rwlock, nullptr);
            if (err != 0)
                LOG_ERROR("DataManagerNode initialization error, pthread_rwlockattr_init failed with error: %s", strerror(err));
        }
        ~DataManagerNode()
        {
            int err = pthread_rwlock_destroy(&_rwlock);
            if (err != 0)
                LOG_ERROR("DataManagerNode destruction error, pthread_rwlock_destroy failed with error: %s", strerror(err));
        }

        BackupInfoNode _info;     // 文件备份信息节点
        pthread_rwlock_t _rwlock; // 读写锁，保证多线程环境下对当前文件以及信息节点的安全访问

        std::string _file_pre_content;    // 文件内容的前缀部分，作为LRU缓存中的Value值(用于快速响应下载的需求)
        DataManagerNode *_next = nullptr; // 链表指针，指向下一个节点
        DataManagerNode *_prev = nullptr; // 链表指针，指向上一个节点
    };

    // DataManager类是用于记录所有存储到云端的备份文件属性信息的单例类
    // 文件的属性信息会由DataManager管理并单独存放到文件中保存下来，后序如果有文件要压缩存储也可以在不解压的状态下获取其属性信息
    // 整个云备份系统维护一个原则：只要是被DataManager存储的文件
    // (所以上传的文件要先落盘才会添加到DataManager中，删除的文件要先从DataManager中删除才会从云服务器上删除)
    // 未来针对大文件的优化:可以前面一小部分正常存储，后面大半压缩存储，这样可以快速的给予client响应，后序再通过流式的边解压边发送持续传输大文件
    class DataManager
    {
    private:
        struct DataManagerList
        {
            size_t _size = 0;                                               // 链表内节点数量
            size_t _capacity = Config::GetInstance()->GetLRUFileCapacity(); // 链表内节点容量
            DataManagerNode *_guard = new DataManagerNode();                // 哨兵头节点
            DataManagerList()
            {
                _guard->_prev = _guard;
                _guard->_next = _guard;
            }
            ~DataManagerList()
            {
                DataManagerNode *current = _guard->_next;
                while (current != _guard)
                {
                    DataManagerNode *next_node = current->_next;
                    current->_file_pre_content.clear();
                    current->_prev = nullptr;
                    current->_next = nullptr;
                    current = next_node;
                }
                delete _guard;
            }
            // 将已在链表中的节点转移到链表头
            bool MoveToHead(DataManagerNode *node)
            {
                if (node == nullptr || node->_next == nullptr || node->_prev == nullptr)
                    return false;
                // 从当前节点位置移除
                node->_prev->_next = node->_next;
                node->_next->_prev = node->_prev;
                // 将节点node插入到头部
                node->_next = _guard->_next;
                node->_prev = _guard;
                _guard->_next->_prev = node;
                _guard->_next = node;
                return true;
            }
            // 将不在链表中的节点插入链表头
            bool PushToHead(DataManagerNode *node, const std::string &file_pre_content)
            {
                if (node == nullptr || node->_next != nullptr || node->_prev != nullptr)
                    return false;
                node->_file_pre_content = file_pre_content;

                node->_next = _guard->_next;
                node->_prev = _guard;
                _guard->_next->_prev = node;
                _guard->_next = node;
                if (++_size > _capacity)
                    RemoveTail();
                return true;
            }
            // 将链表中的指定节点移出链表
            bool Remove(DataManagerNode *node)
            {
                if (node == nullptr || node->_next == nullptr || node->_prev == nullptr)
                    return false;
                node->_prev->_next = node->_next;
                node->_next->_prev = node->_prev;
                node->_file_pre_content.clear();
                node->_prev = nullptr;
                node->_next = nullptr;
                _size--;
                return true;
            }
            // 移除链表的尾节点
            bool RemoveTail()
            {
                if (_guard->_prev == _guard) // 链表为空
                    return false;
                return Remove(_guard->_prev);
            }
        };

    public:
        using ptr = std::shared_ptr<DataManager>;
        ~DataManager() { _file_storage_thread.join(); }
        bool Register(const std::string &filename)
        {
            std::unique_lock<std::shared_mutex> write_lock(_rwlock);
            if (_hash.find(filename) != _hash.end())
            {
                LOG_INFO("Register error, file already exists: %s", filename.c_str());
                return false;
            }
            DataManagerNode::ptr new_node(new DataManagerNode);
            if (!new_node)
            {
                LOG_FATAL("Register error, create DataManagerNode failed");
                return false;
            }
            new_node->_info._filename = filename;
            _hash[filename] = new_node;
            return true;
        }
        // 注销一个文件备份信息，文件名必须是之前已经注册过并且未上传成功的
        bool Deregister(const std::string &filename)
        {
            std::unique_lock<std::shared_mutex> write_lock(_rwlock);
            if (_hash.find(filename) == _hash.end())
            {
                LOG_WARN("Deregister error, file not found: %s", filename.c_str());
                return false;
            }
            _hash.erase(filename);
            return true;
        }
        // 文件必须在之前已经通过Register注册过
        bool Insert(const std::string &filename, int64_t filesize)
        {
            std::unique_lock<std::shared_mutex> write_lock(_rwlock);
            if (_hash.find(filename) == _hash.end())
            {
                LOG_WARN("Insert error, file not registered: %s", filename.c_str());
                return false;
            }
            if (filesize < 0)
            {
                LOG_WARN("Insert error, invalid file size: %s", filename.c_str());
                return false;
            }
            _hash[filename]->_info._size = filesize;
            _hash[filename]->_info._time = time(nullptr);
            _is_dirty = true;
            _file_storage_cond.notify_all();
            return true;
        }
        // 删除文件备份信息的记录，文件必须是之前已经Insert过上传成功的，并且返回该记录的节点，失败返回nullptr
        DataManagerNode::ptr Delete(const std::string &filename)
        {
            std::unique_lock<std::shared_mutex> write_lock(_rwlock);
            if (_hash.find(filename) == _hash.end())
            {
                LOG_WARN("Delete error, file not found: %s", filename.c_str());
                return nullptr;
            }
            if (_hash[filename]->_info._size < 0)
            {
                LOG_WARN("Delete error, file not uploaded yet: %s", filename.c_str());
                return nullptr;
            }
            DataManagerNode::ptr node = _hash[filename];
            _hash.erase(filename);
            _is_dirty = true;
            _file_storage_cond.notify_all();
            return node;
        }
        // 根据文件名获取文件备份属性信息，失败返回nullptr
        DataManagerNode::ptr GetFileReadPermit(const std::string &filename)
        {
            std::shared_lock<std::shared_mutex> read_lock(_rwlock);
            if (_hash.find(filename) == _hash.end())
            {
                LOG_WARN("GetFileReadPermit error, file not found: %s", filename.c_str());
                return nullptr;
            }
            if (_hash[filename]->_info._size < 0)
            {
                LOG_WARN("GetFileReadPermit error, file not uploaded yet: %s", filename.c_str());
                return nullptr;
            }
            DataManagerNode::ptr node = _hash[filename];
            int err = pthread_rwlock_rdlock(&node->_rwlock);
            if (err != 0)
            {
                LOG_ERROR("GetFileReadPermit error, pthread_rwlock_rdlock failed with error: %s", strerror(err));
                return nullptr;
            }
            return node;
        }
        // 从数据管理器中获取所有的文件备份属性信息
        bool GetAllBackupInfo(std::vector<BackupInfoNode> *infos)
        {
            infos->clear();
            std::shared_lock<std::shared_mutex> read_lock(_rwlock);
            for (auto &[filename, node] : _hash)
            {
                if (node->_info._size >= 0)
                {
                    BackupInfoNode info;
                    info._filename = node->_info._filename;
                    info._size = node->_info._size;
                    info._time = node->_info._time;
                    infos->push_back(info);
                }
            }
            return true;
        }

    private:
        DataManager(const DataManager &) = delete;
        DataManager &operator=(const DataManager &) = delete;
        DataManager(const std::string &filepath) : _file(filepath)
        {
            LoadFromFile();
            VerifyFileLegality();
            _file_storage_thread = std::thread(&DataManager::FileStorageThread, this); // 启动异步文件存储线程
            LOG_INFO("DataManager initialized successfully, loaded %zu files", _hash.size());
        }

        // 从文件中加载数据管理器备份的文件属性信息，初始化时调用
        void LoadFromFile()
        {
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
                    BackupInfoNode info;
                    info._filename = item["filename"].asString();
                    info._size = item["size"].asInt64();
                    info._time = item["time"].asInt64();
                    if (info._filename.empty() || info._size < 0)
                    {
                        LOG_WARN("DataManager initialization error, invalid item");
                        continue;
                    }
                    DataManagerNode::ptr node(new DataManagerNode);
                    if (!node)
                    {
                        LOG_ERROR("DataManager initialization error, create DataManagerNode failed");
                        continue;
                    }
                    node->_info = info;
                    _hash[info._filename] = node;
                }
            }
        }
        //  验证文件的合法性，确保程序在上次退出前保存的文件信息都是正确的
        void VerifyFileLegality()
        {
            std::string backup_file_dir = Config::GetInstance()->GetBackupFileDir();
            std::vector<FileUtil> files;
            if (!FileUtil(backup_file_dir).ScanDirectory(&files))
            {
                LOG_ERROR("DataManager initialization error, ScanDirectory failed");
                return;
            }
            std::unordered_set<std::string> backup_files;
            for (auto &file : files)
            {
                if (_hash.find(file.GetFileName()) == _hash.end())
                    file.Remove(); // 如果文件不在DataManager中注册，则删除该文件
                else
                    backup_files.insert(file.GetFileName());
            }
            for (auto &[filename, node] : _hash)
            {
                if (backup_files.find(filename) == backup_files.end())
                {
                    LOG_WARN("DataManager file verification error, file not found in backup directory: %s", filename.c_str());
                    _hash.erase(filename); // 删除不存在的文件记录
                    _is_dirty = true;
                }
                else if (node->_info._size < 0)
                {
                    LOG_WARN("DataManager file verification error, file size is invali: %s:%lld", filename.c_str(), node->_info._size);
                    _hash.erase(filename); // 删除大小无效的文件记录
                    _is_dirty = true;
                }
            }
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
                    for (const auto &[filename, node] : _hash)
                    {
                        Json::Value item;
                        item["filename"] = node->_info._filename;
                        item["size"] = static_cast<Json::Int64>(node->_info._size);
                        item["time"] = static_cast<Json::Int64>(node->_info._time);
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
                if (!_file.Clear())
                {
                    LOG_WARN("Storage error, clear file failed: %s", _file.GetFilePath().c_str());
                    continue;
                }
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
        FileUtil _file;                                              // 将来将文件属性持久化存储的资源路径
        std::unordered_map<std::string, DataManagerNode::ptr> _hash; // 通过hash表用文件名快速的访问到文件属性信息
        std::shared_mutex _rwlock;                                   // 读写锁，保证多线程环境下对_hash的安全访问

        bool _is_dirty = false;                         // 标记数据管理器是否需要存储到文件中，若有修改则设置为true
        std::condition_variable_any _file_storage_cond; // 条件变量，异步的文件存储线程在不满足条件时就在该条件变量下等待
        std::thread _file_storage_thread;               // 异步的文件存储线程

        DataManagerList _list;  // LRU中的双向链表
        std::mutex _list_mutex; // 保护双向链表的互斥锁
    };
}

#endif