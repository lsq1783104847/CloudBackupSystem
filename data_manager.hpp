#ifndef CLOUD_BACKUP_DATA_MANAGER_HPP
#define CLOUD_BACKUP_DATA_MANAGER_HPP

#include <shared_mutex>
#include <condition_variable>
#include <unordered_set>
#include "util.hpp"
#include "config.hpp"

namespace cloud_backup
{
    struct BackupInfoNode
    {
        std::string _filename; // 文件名(仅文件名，不含路径)
        int64_t _size;         // 文件大小(单位:字节)
        time_t _time;          // 文件上传完成的时间
    };
    // 数据管理类的节点，包含文件备份信息和LRU结构的相关属性，二者共用该节点
    struct DataManagerNode
    {
        using ptr = std::shared_ptr<DataManagerNode>;
        DataManagerNode() {}
        ~DataManagerNode() {}

        BackupInfoNode _info;      // 文件备份信息
        std::shared_mutex _rwlock; // 读写锁，保证多线程环境下对当前文件安全访问

        std::string _file_pre_content;    // 文件起始的部分内容，作为LRU缓存中的Value值(用于快速响应下载的需求)
        DataManagerNode *_next = nullptr; // 链表指针，指向下一个节点
        DataManagerNode *_prev = nullptr; // 链表指针，指向上一个节点
    };

    // DataManager类是用于记录所有存储到云端的备份文件属性信息的单例类
    // 文件的属性信息会由DataManager管理并单独存放到文件中保存下来，后序如果有文件要压缩存储也可以在不解压的状态下获取其属性信息
    class DataManager
    {
    private:
        // LRU结构所需要使用的双向链表类
        struct DataManagerList
        {
            size_t _size = 0;                                               // 链表内节点数量
            size_t _capacity = Config::GetInstance()->GetLRUFileCapacity(); // 链表内节点容量
            DataManagerNode *_guard = new DataManagerNode();                // 哨兵头节点

            DataManagerList()
            {
                if (_guard == nullptr)
                    LOG_ERROR("create DataManagerList failed");
                else
                {
                    _guard->_prev = _guard;
                    _guard->_next = _guard;
                }
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
                if (node == nullptr)
                    return false;
                if (node->_next == nullptr || node->_prev == nullptr)
                    return true;
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
                if (_guard->_prev == _guard)
                    return false;
                return Remove(_guard->_prev);
            }
        };

    public:
        using ptr = std::shared_ptr<DataManager>;
        ~DataManager() { _file_storage_thread.join(); }
        // 向文件管理对象中注册一个将要上传的文件，如果文件不存在会在磁盘的对应目录下创建该文件，如果文件存在会清空文件，后序不允许同名文件的注册
        bool Register(const std::string &filename)
        {
            std::unique_lock<std::shared_mutex> write_lock(_rwlock);
            if (_hash.find(filename) != _hash.end())
            {
                LOG_INFO("Register error, file already register: %s", filename.c_str());
                return false;
            }
            if (!FileUtil::check_filename(filename))
                return false;
            std::string target_file_dir = Config::GetInstance()->GetBackupFileDir();
            if (target_file_dir.back() != '/')
                target_file_dir += '/';
            FileUtil target_file(target_file_dir + filename);
            if (target_file.Exists() && target_file.Clear() == false)
            {
                LOG_WARN("Register error, file already exists, clear file fail: %s", filename.c_str());
                return false;
            }
            else if (!target_file.Exists() && !target_file.AppendContent(""))
            {
                LOG_WARN("Register error, create target file failed: %s", filename.c_str());
                return false;
            }
            _hash[filename] = nullptr;
            return true;
        }
        // 注销一个文件备份信息，文件名必须是之前已经注册过并且未上传成功的，如果文件此时依然存在于磁盘上会同步将磁盘上的文件删除
        bool Deregister(const std::string &filename)
        {
            std::unique_lock<std::shared_mutex> write_lock(_rwlock);
            if (_hash.find(filename) == _hash.end())
                LOG_WARN("Deregister error, file not found: %s", filename.c_str());
            else
                _hash.erase(filename);
            std::string target_file_dir = Config::GetInstance()->GetBackupFileDir();
            if (target_file_dir.back() != '/')
                target_file_dir += '/';
            FileUtil target_file(target_file_dir + filename);
            if (target_file.Exists() && target_file.RemoveRegularFile() == false)
            {
                LOG_ERROR("Deregister error, file:%s RemoveRegularFile failed", filename.c_str());
                return false;
            }
            return true;
        }
        // 将上传成功的文件加入DataManager中管理，文件必须在之前已经通过Register注册过
        bool Insert(const std::string &filename, int64_t filesize)
        {
            std::unique_lock<std::shared_mutex> write_lock(_rwlock);
            if (_hash.find(filename) == _hash.end())
            {
                LOG_WARN("Insert error, file not registered: %s", filename.c_str());
                return false;
            }
            if (_hash[filename] != nullptr)
            {
                LOG_WARN("Insert error, file is exist: %s", filename.c_str());
                return false;
            }
            DataManagerNode::ptr new_node(new DataManagerNode);
            if (new_node == nullptr)
            {
                LOG_FATAL("Register error, create DataManagerNode failed");
                return false;
            }
            new_node->_info._filename = filename;
            new_node->_info._size = filesize;
            new_node->_info._time = time(nullptr);
            _hash[filename] = new_node;
            _is_dirty = true;
            _file_storage_cond.notify_all();
            return true;
        }
        // 从DataManager中删除文件备份信息的记录，并同步清除LRU中的数据，如果文件此时依然存在于磁盘上会同步将磁盘上的文件删除，文件必须是之前已经Insert过上传成功的
        bool Delete(const std::string &filename)
        {
            std::unique_lock<std::shared_mutex> write_lock(_rwlock);
            if (IsValidFile(filename) == false)
            {
                LOG_WARN("Delete error, file not valid: %s", filename.c_str());
                return false;
            }
            std::unique_lock<std::mutex> list_lock(_list_mutex);
            if (_list.Remove(_hash[filename].get()) == false)
            {
                LOG_ERROR("Delete error, file: %s Remove failed From LRU", filename.c_str());
                return false;
            }
            std::string target_file_dir = Config::GetInstance()->GetBackupFileDir();
            if (target_file_dir.back() != '/')
                target_file_dir += '/';
            bool ret_value = true;
            FileUtil target_file(target_file_dir + filename);
            {
                std::unique_lock<std::shared_mutex> file_write_lock(_hash[filename]->_rwlock);
                if (target_file.Exists() && target_file.RemoveRegularFile() == false)
                {
                    LOG_ERROR("delete target file:%s failed, RemoveRegularFile failed", filename.c_str());
                    ret_value = false;
                }
            }
            _hash.erase(filename);
            _is_dirty = true;
            _file_storage_cond.notify_all();
            return ret_value;
        }
        // 根据文件名获取文件管理信息节点，失败返回nullptr，因为返回的是智能指针所以即使DataManager内部的数据被清理掉了也不会导致非法访问
        DataManagerNode::ptr GetFileInfoNode(const std::string &filename)
        {
            std::shared_lock<std::shared_mutex> read_lock(_rwlock);
            if (IsValidFile(filename) == false)
            {
                LOG_WARN("GetFileReadPermit error, file not valid: %s", filename.c_str());
                return nullptr;
            }
            return _hash[filename];
        }
        // 从数据管理器中获取所有的文件备份属性信息
        bool GetAllBackupInfo(std::vector<BackupInfoNode> *infos)
        {
            infos->clear();
            std::shared_lock<std::shared_mutex> read_lock(_rwlock);
            infos->reserve(_hash.size());
            for (auto &[filename, node] : _hash)
            {
                if (node != nullptr)
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
        // 快速获取指定文件的的大小
        uint64_t GetFileSize(const std::string &filename)
        {
            std::shared_lock<std::shared_mutex> read_lock(_rwlock);
            if (IsValidFile(filename) == false)
            {
                LOG_WARN("GetFileSize error, file not valid: %s", filename.c_str());
                return -1;
            }
            return _hash[filename]->_info._size;
        }
        // 尝试从LRU中获取文件起始的部分内容，失败返回空串
        std::string GetFilePreContent(const std::string &filename)
        {
            std::shared_lock<std::shared_mutex> read_lock(_rwlock);
            if (IsValidFile(filename) == false)
            {
                LOG_WARN("GetFilePreContent error, file not valid: %s", filename.c_str());
                return "";
            }
            std::unique_lock<std::mutex> list_lock(_list_mutex);
            if (_hash[filename]->_next == nullptr || _hash[filename]->_prev == nullptr)
            {
                LOG_INFO("GetFilePreContent error, file not in LRU list: %s", filename.c_str());
                return "";
            }
            if (_list.MoveToHead(_hash[filename].get()) == false)
            {
                LOG_ERROR("GetFilePreContent error, MoveToHead failed for file: %s", filename.c_str());
                return "";
            }
            return _hash[filename]->_file_pre_content;
        }
        // 将文件起始的部分内容放入LRU中缓存，如果已经存在则将其更新为最近一次访问的数据
        bool PutFilePreContent(const std::string &filename, std::string file_pre_content)
        {
            if (file_pre_content.size() > Config::GetInstance()->GetLRUFileContentSize())
                file_pre_content = file_pre_content.substr(0, Config::GetInstance()->GetLRUFileContentSize());
            std::shared_lock<std::shared_mutex> read_lock(_rwlock);
            if (IsValidFile(filename) == false)
            {
                LOG_WARN("PutFilePreContent error, file not valid: %s", filename.c_str());
                return false;
            }
            std::unique_lock<std::mutex> list_lock(_list_mutex);
            if (_hash[filename]->_next == nullptr || _hash[filename]->_prev == nullptr)
            {
                if (_list.PushToHead(_hash[filename].get(), file_pre_content) == false)
                {
                    LOG_ERROR("PutFilePreContent error, PushToHead failed for file: %s", filename.c_str());
                    return false;
                }
            }
            else
            {
                if (_list.MoveToHead(_hash[filename].get()) == false)
                {
                    LOG_ERROR("GetFilePreContent error, MoveToHead failed for file: %s", filename.c_str());
                    return false;
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
            LOG_INFO("DataManager initialized successfully, loaded %zu file", _hash.size());
        }

        // 从文件中加载数据管理器备份的文件属性信息，初始化时调用
        void LoadFromFile()
        {
            std::string content;
            if (_file.GetContent(&content) == false)
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
                    if (node == nullptr)
                    {
                        LOG_ERROR("DataManager initialization error, create DataManagerNode failed");
                        continue;
                    }
                    node->_info._filename = info._filename;
                    node->_info._size = info._size;
                    node->_info._time = info._time;
                    _hash[info._filename] = node;
                }
            }
            LOG_INFO("DataManager LoadFromFile Succeed");
        }
        //  验证文件的合法性，确保程序在上次退出前保存的文件信息与磁盘上存储的文件都是合法的
        void VerifyFileLegality()
        {
            std::string backup_file_dir = Config::GetInstance()->GetBackupFileDir();
            std::vector<FileUtil> files;
            if (FileUtil(backup_file_dir).ScanDirectory(&files) == false)
            {
                LOG_ERROR("DataManager initialization error, ScanDirectory failed");
                return;
            }
            std::unordered_set<std::string> backup_files;
            for (auto &file : files)
            {
                std::string filename = file.GetFileName();
                if (_hash.find(filename) == _hash.end())
                {
                    LOG_WARN("DataManager file verification error, file not found in DataManager: %s", filename.c_str());
                    if (file.RemoveRegularFile() == false) // 如果文件不在DataManager中管理，则删除该文件
                        LOG_WARN("DataManager file verification error, file:%s RemoveRegularFile failed", filename.c_str());
                }
                else
                    backup_files.insert(filename);
            }
            // 删除不存在的文件的记录
            std::vector<std::string> not_backeup_files;
            for (auto &[filename, node] : _hash)
            {
                if (backup_files.find(filename) == backup_files.end())
                {
                    LOG_WARN("DataManager file verification error, file not found in backup directory: %s", filename.c_str());
                    not_backeup_files.push_back(filename);
                }
            }
            for (auto &filename : not_backeup_files)
            {
                _hash.erase(filename);
                _is_dirty = true;
            }
            LOG_INFO("DataManager VerifyFileLegality Succeed");
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
                        if (IsValidFile(filename) == true)
                        {
                            Json::Value item;
                            item["filename"] = node->_info._filename;
                            item["size"] = static_cast<Json::Int64>(node->_info._size);
                            item["time"] = static_cast<Json::Int64>(node->_info._time);
                            root.append(item);
                        }
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
                if (!_file.AppendContent(infos_str))
                {
                    LOG_WARN("Storage error, write to file failed: %s", _file.GetFilePath().c_str());
                    continue;
                }
            }
        }
        // 检查文件是否有效，若文件在DataManager中注册过且已上传成功则返回true，否则返回false
        bool IsValidFile(const std::string &filename)
        {
            if (_hash.find(filename) == _hash.end())
            {
                LOG_WARN("<%s> is not valid file, file not found", filename.c_str());
                return false;
            }
            if (_hash[filename] == nullptr)
            {
                LOG_WARN("<%s> is not valid file, file not uploaded yet", filename.c_str());
                return false;
            }
            return true;
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
        FileUtil _file;                                              // 将文件属性持久化存储的文件
        std::unordered_map<std::string, DataManagerNode::ptr> _hash; // hash表，用于通过文件名快速的访问到文件属性信息，同时也是LRU中的hash
        std::shared_mutex _rwlock;                                   // 读写锁，保证多线程环境下对_hash的安全访问

        bool _is_dirty = false;                         // 标记数据管理器中的文件备份信息是否需要存储到文件中，若有修改则设置为true
        std::condition_variable_any _file_storage_cond; // 条件变量，异步的文件存储线程在不满足条件时就在该条件变量下等待
        std::thread _file_storage_thread;               // 异步的文件存储线程

        DataManagerList _list;  // LRU中的双向链表
        std::mutex _list_mutex; // 保护链表线程安全的互斥锁
    };
}

#endif