#ifndef CLOUD_UTIL_HPP
#define CLOUD_UTIL_HPP

#include <sys/stat.h>
#include <string>

namespace cloud_backup
{
    class FileUtil
    {
    public:
        FileUtil(const std::string &filename) : _filename(filename) {}
        ~FileUtil() {}
        int64_t GetFileSize()
        {
            struct stat st;
            if (stat(_filename.c_str(), &st) == -1)
                return -1;
            return st.st_size;
        }
        time_t LastModTime()
        {
            return 0;
        }
        time_t LastAccTime()
        {
            return 0;
        }
        std::string GetFileName()
        {
            return "";
        }

    private:
        // 检查传入的文件路径path是否是个正确的路径,要求正确路径中不能有 "//"
        // 如果是正确路径(绝对路径，相对路径均可)就将其补充完善并返回，如果不是正确路径就返回空字符串
        std::string is_path(const std::string &path)
        {
            if (path.size() == 0 || path == "/")
                return path;
            size_t rpos = 0, lpos = 0;
            std::string ret;
            if (path[0] != '/')
                ret += "./";
            else
                lpos = 1;
            while (lpos < path.size())
            {
                rpos = path.find_first_of('/', lpos);
                if (rpos == lpos)
                    return "";
                if (rpos == std::string::npos)
                    break;
                lpos = rpos + 1;
            }
            ret += path;
            if (ret[ret.size() - 1] == '/')
                ret.pop_back();
            return ret;
        }

    private:
        std::string _filename;
    };
}

#endif
