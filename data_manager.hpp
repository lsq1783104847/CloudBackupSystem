#ifndef CLOUD_BACKUP_DATA_MANAGER_HPP
#define CLOUD_BACKUP_DATA_MANAGER_HPP

#include "util.hpp"
#include <unordered_map>
#include <pthread.h>

namespace cloud_backup
{
    struct BackupInfo
    {
        bool _compression_flag; // 文件是否压缩存储标志位
        size_t _fsize;          // 文件大小
        time_t _atime;          // 文件最后一次访问时间
        time_t _mtime;          // 文件最后一次修改时间
        std::string _filepath;  // 文件未被压缩时的存储路径
        std::string _packpath;  // 文件被压缩后的存储路径
        std::string _url_path;  // 文件的url访问路径
    };

    class DataManager
    {
    public:
        DataManager()
        {
            
        }

    private:
        std::string _file;                                 // 将来将文件持久化存储的资源路径
        std::unordered_map<std::string, BackupInfo> _hash; // 通过hash表用文件路径名快速的访问到文件属性信息
    };
}

#endif