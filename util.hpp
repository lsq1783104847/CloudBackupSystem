#ifndef CLOUD_BACKUP_UTIL_HPP
#define CLOUD_BACKUP_UTIL_HPP

#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <memory>
#include <string>
#include <cstring>
#include <jsoncpp/json/json.h>
#include <filesystem>
#include <thread>
#include "log.hpp"
#include "error.hpp"

namespace cloud_backup
{
    namespace fs = std::filesystem;
    class FileUtil
    {
    public:
        // 传入文件路径初始化该对象
        FileUtil(const std::string &filepath) : _filepath(filepath)
        {
            _filepath = path_transform(_filepath);
        }
        ~FileUtil() {}
        // 获取文件大小，失败返回-1
        int64_t GetFileSize()
        {
            struct stat st;
            if (stat(_filepath.c_str(), &st) == -1)
            {
                int err = errno;
                LOG_ERROR("stat error:%d  message:%s", err, strerror(err));
                return -1;
            }
            return st.st_size;
        }
        // 获取已经转化成绝对路径的文件路径，若传入的路径有问题则返回空串
        std::string GetFilePath()
        {
            return _filepath;
        }
        // 根据路径返回文件名，如果path为根目录或者路径有问题就返回空串
        std::string GetFileName()
        {
            std::string ret = _filepath;
            if (ret == "" || ret == "/")
            {
                LOG_ERROR("GetFileName error, file_path is error");
                return "";
            }
            if (ret[ret.size() - 1] == '/')
                ret.pop_back();
            size_t pos = ret.find_last_of('/');
            return ret.substr(pos + 1);
        }
        // 以二进制的方式获取文件中指定位置开始的指定长度的数据，成功获取后数据放入传入的buffer中，需要保证文件存在，失败则返回false
        // 若传入的pos+len>=文件大小，那么就获取从pos开始到文件结尾的所有数据，若pos>=文件大小则输出空串
        bool GetContent(std::string *buffer, size_t pos = 0, size_t len = UINT32_MAX)
        {
            std::ifstream ifs;
            ifs.open(_filepath, std::ios::binary);
            if (!ifs.is_open())
            {
                LOG_ERROR("GetContent error, open file failed");
                return false;
            }
            int64_t fsize = GetFileSize();
            if (fsize == -1)
            {
                LOG_ERROR("GetContent error, GetFileSize failed");
                ifs.close();
                return false;
            }
            if (pos >= fsize)
            {
                LOG_INFO("GetContent error, pos more than file size");
                *buffer = "";
                ifs.close();
                return true;
            }
            ifs.seekg(pos, std::ios::beg);
            len = len < fsize - pos ? len : fsize - pos;
            if (len == 0)
            {
                *buffer = "";
                ifs.close();
                return true;
            }
            char buff[len] = {0};
            ifs.read(buff, len);
            if (!ifs.good())
            {
                LOG_ERROR("GetContent error, read file failed");
                ifs.close();
                return false;
            }
            buffer->resize(len);
            for (size_t i = 0; i < len; i++)
                (*buffer)[i] = buff[i];
            ifs.close();
            return true;
        }
        // 追加的向文件中写入内容，如果文件不存在会默认创建新文件，失败返回false
        bool AppendContent(const std::string &buffer)
        {
            std::ofstream ofs;
            ofs.open(_filepath, std::ios::binary | std::ios::app);
            if (!ofs.is_open())
            {
                LOG_ERROR("SetContent error, file open failed");
                return false;
            }
            ofs.write(buffer.c_str(), buffer.size());
            if (!ofs.good())
            {
                LOG_ERROR("SetContent error, write file failed");
                ofs.close();
                return false;
            }
            ofs.close();
            return true;
        }
        // 检测当前文件是否已经存在，若存在则返回true
        bool Exists()
        {
            if (_filepath == "" || access(_filepath.c_str(), F_OK) != 0)
            {
                LOG_INFO("file:\"%s\" not Exist", _filepath.c_str());
                return false;
            }
            return true;
        }
        // 将文件内容清空，需要保证文件是存在的，失败返回false
        bool Clear()
        {
            if (!Exists())
            {
                LOG_INFO("Clear error, file not exist");
                return false;
            }
            std::ofstream ofs;
            ofs.open(_filepath, std::ios::trunc);
            if (!ofs.is_open())
            {
                LOG_ERROR("Clear error, file open failed");
                return false;
            }
            ofs.close();
            return true;
        }
        // 将当前的_filepath视为一个目录的路径，尝试创建该路径上所有不存在的目录，失败返回false
        bool CreateDirectories()
        {
            if (Exists() == true)
            {
                LOG_INFO("file:\"%s\" already Exist", _filepath.c_str());
                return true;
            }
            return fs::create_directories(_filepath);
        }
        // 将当前的_filepath视为一个目录的路径，遍历该目录下的所有普通文件，并将其文件路径通过files参数返回，失败返回false
        bool ScanDirectory(std::vector<FileUtil> *files)
        {
            if (!Exists())
            {
                LOG_ERROR("ScanDirectory error, file not Exist");
                return false;
            }
            if (!fs::is_directory(_filepath))
            {
                LOG_ERROR("ScanDirectory error, file is not a directory");
                return false;
            }
            files->clear();
            for (auto &file : fs::directory_iterator(_filepath))
            {
                if (fs::is_regular_file(file.path()))
                    files->push_back(FileUtil(file.path().string()));
            }
            return true;
        }
        // 根据_filepath将当前文件视为普通文件并尝试删除，失败返回false
        bool RemoveRegularFile()
        {
            if (!Exists())
            {
                LOG_INFO("Remove error, file:%s not Exist", _filepath.c_str());
                return false;
            }
            if (!fs::is_regular_file(_filepath))
            {
                LOG_WARN("Remove error, file:%s is not a regular file", _filepath.c_str());
                return false;
            }
            if (fs::remove(_filepath) == false)
            {
                LOG_WARN("Remove error, file:%s", _filepath.c_str());
                return false;
            }
            return true;
        }

    private:
        // 检查传入的文件路径path是否是个正确的路径,要求正确路径中不能有 "//",如果以"~/"开头就将其转换成家目录
        // 如果是正确路径(绝对路径，相对路径均可)就将其补充完善并返回，如果不是正确路径就返回空字符串
        static std::string is_path(const std::string &path)
        {
            if (path.size() == 0 || path == "/")
                return path;
            size_t rpos = 0, lpos = 0;
            std::string ret;
            size_t pos = 0;
            if (path[0] == '~' && (path.size() == 1 || path[1] == '/'))
            {
                char *p = getenv("HOME");
                if (p == nullptr)
                    return "";
                ret += p;
                pos = 1;
            }
            else if (path[0] == '/')
                lpos = 1;
            else
                ret += "./";
            while (lpos < path.size())
            {
                rpos = path.find_first_of('/', lpos);
                if (rpos == lpos)
                    return "";
                if (rpos == std::string::npos)
                    break;
                lpos = rpos + 1;
            }
            ret += path.substr(pos);
            if (ret[ret.size() - 1] == '/')
                ret.pop_back();
            return ret;
        }
        // 传入相对路径，返回绝对路径，要求传入的相对路径是正确的，如果发生错误则返回空串
        // 如果传入的就是绝对路径，那么就不做转化返回该路径,且返回的绝对路径中不含"."和".."
        static std::string path_transform(const std::string &path)
        {
            std::string ret = is_path(path);
            if (ret == "")
                return "";

            std::string absolute_path = "/";
            size_t lpos = 1, rpos = 1;

            if (ret[0] != '/')
            {
                char *p = getcwd(NULL, 0);
                if (p == nullptr)
                    return "";
                absolute_path = p;
                free(p);
                lpos = 0, rpos = 0;
            }

            while (lpos < ret.size())
            {
                rpos = ret.find_first_of('/', lpos);
                std::string sstr = ret.substr(lpos, (rpos == std::string::npos ? rpos : rpos - lpos));
                lpos = (rpos == std::string::npos ? rpos : rpos + 1);
                if (sstr == ".")
                    continue;
                else if (sstr == "..")
                    absolute_path = file_dir(absolute_path);
                else
                {
                    if (absolute_path[absolute_path.size() - 1] != '/')
                        absolute_path.push_back('/');
                    absolute_path += sstr;
                }
            }

            if (absolute_path[absolute_path.size() - 1] == '/' && absolute_path != "/")
                absolute_path.pop_back();
            return absolute_path;
        }
        // 根据传入的文件的path路径返回该文件所处的目录
        // 如果传入的路径有问题就返回空串,如果传入的是根目录则也返回根目录
        static std::string file_dir(const std::string &path)
        {
            std::string ret = path_transform(path);
            if (ret == "")
                return ret;
            size_t pos = ret.find_last_of('/'); // 经过path_transform()的调用，ret中一定有'/',且除了根目录ret不以'/'结尾
            return ret.substr(0, pos + 1);
        }

    private:
        std::string _filepath;
    };

    class JsonUtil
    {
    public:
        // 通过Json完成序列化，成功返回true
        static bool Serialize(const Json::Value &info, std::string *buff)
        {
            Json::StreamWriterBuilder builder;
            builder["emitUTF8"] = true;
            std::shared_ptr<Json::StreamWriter> writer(builder.newStreamWriter());
            if (write == nullptr)
            {
                LOG_ERROR("Json newStreamWriter error, Serialize fail");
                return false;
            }
            std::stringstream ss;
            if (writer->write(info, &ss) != 0)
            {
                LOG_ERROR("Json write error, Serialize fail");
                return false;
            }
            *buff = ss.str();
            return true;
        }
        // 通过Json完成反序列化，成功返回true
        static bool Deserialize(const std::string &buff, Json::Value *info)
        {
            Json::CharReaderBuilder builder;
            std::shared_ptr<Json::CharReader> reader(builder.newCharReader());
            if (reader == nullptr)
            {
                LOG_ERROR("Json newCharReader error, Deserialize fail");
                return false;
            }
            std::string err;
            bool ret = reader->parse(buff.c_str(), buff.c_str() + buff.size(), info, &err);
            if (ret == false)
            {
                LOG_ERROR("Json parse error, message:%s, Deserialize fail", err.c_str());
                return false;
            }
            return true;
        }
    };

    class NetSocketUtil
    {
    public:
        NetSocketUtil() : _socket(-1) {}
        ~NetSocketUtil()
        {
            if (_socket != -1)
                close(_socket);
        }
        int GetSocketet() { return _socket; }
        void InitSocket()
        {
            int retfd = socket(AF_INET, SOCK_STREAM, 0);
            if (retfd == -1)
            {
                LOG_FATAL("create socket error:%d  message:%s", errno, strerror(errno));
                exit(INIT_SOCKET_ERROR);
            }
            _socket = retfd;
            // 设置地址和port为可复用的，解决服务器挂掉短时间内无法以相同的port重启的问题
            int opt = 1;
            if (setsockopt(_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1)
                LOG_ERROR("setsockopt error:%d  message:%s", errno, strerror(errno));
        }
        void Bind(uint16_t port)
        {
            sockaddr_in info;
            info.sin_family = AF_INET;
            info.sin_addr.s_addr = INADDR_ANY;
            info.sin_port = htons(port);
            if (bind(_socket, (sockaddr *)(&info), sizeof(sockaddr_in)) == -1)
            {
                LOG_FATAL("bind socket error:%d  message:%s", errno, strerror(errno));
                exit(BIND_SOCKET_ERROR);
            }
        }
        void Listen(int listen_queue_size)
        {
            if (listen(_socket, listen_queue_size) == -1)
            {
                LOG_FATAL("listen socket error:%d  message:%s", errno, strerror(errno));
                exit(LISTEN_SOCKET_ERROR);
            }
        }
        int Accept(sockaddr *addr = nullptr, socklen_t *len = nullptr)
        {
            int retfd = accept(_socket, addr, len);
            if (retfd == -1)
                LOG_WARN("accept error:%d  message:%s", errno, strerror(errno));
            return retfd;
        }

    private:
        int _socket;
    };

    class EpollUtil
    {
    public:
        EpollUtil()
        {
            _epollfd = epoll_create1(EPOLL_CLOEXEC);
            if (_epollfd == -1)
                exit(EPOLL_CREATE_ERROR);
        }
        ~EpollUtil() { close(_epollfd); }

        bool EpollAdd(int fd, uint32_t events)
        {
            if (EpollAddOrMod(fd, events, EPOLL_CTL_ADD) == -1)
            {
                LOG_ERROR("EpollAdd error:%d  message:%s", errno, strerror(errno));
                return false;
            }
            return true;
        }
        bool EpollMod(int fd, uint32_t events)
        {
            if (EpollAddOrMod(fd, events, EPOLL_CTL_MOD) == -1)
            {
                LOG_ERROR("EpollMod error:%d  message:%s", errno, strerror(errno));
                return false;
            }
            return true;
        }
        bool EpollDel(int fd)
        {
            if (epoll_ctl(_epollfd, EPOLL_CTL_DEL, fd, nullptr) == -1)
            {
                LOG_ERROR("EpollDel error:%d  message:%s", errno, strerror(errno));
                return false;
            }
            return true;
        }
        int EpollBlockWait(epoll_event *events, int maxevents)
        {
            if (events == nullptr || maxevents <= 0)
            {
                LOG_ERROR("EpollWait error, parameter is error");
                return -1;
            }
            int ret = epoll_wait(_epollfd, events, maxevents, -1);
            if (ret > 0)
                LOG_INFO("%d events are ready", ret);
            else if (ret == 0)
                LOG_INFO("EpollWait timeout");
            else
                LOG_ERROR("EpollWait error:%d  message:%s", errno, strerror(errno));
            return ret;
        }

    private:
        int EpollAddOrMod(int fd, uint32_t events, int op)
        {
            struct epoll_event ee;
            ee.events = events;
            ee.data.fd = fd;
            return epoll_ctl(_epollfd, op, fd, &ee);
        }

    private:
        int _epollfd;
    };

    // 使当前进程守护进程化
    void Daemon(const std::string &work_path = "/")
    {
        // 让进程不是进程组的组长
        if (fork() > 0)
            exit(0);

        // 让进程独立出当前的会话并自成新的会话与进程组，且不与任何终端关联
        pid_t ret = setsid();
        if (ret == -1)
        {
            LOG_FATAL("setsid error:%d  message:%s", errno, strerror(errno));
            exit(DAEMON_ERROR);
        }

        // 忽略一些异常信号
        signal(SIGCHLD, SIG_IGN);
        signal(SIGPIPE, SIG_IGN);

        // 将工作路径改为work_path
        if (chdir(work_path.c_str()) == -1)
        {
            LOG_FATAL("chdir error:%d  message:%s", errno, strerror(errno));
            exit(DAEMON_ERROR);
        }

        // 对0,1,2文件描述符做特殊处理，在守护进程状态下使其无法使用标准输入输出和错误输出
        int wfd = open("/dev/null", O_WRONLY);
        if (wfd == -1)
        {
            LOG_FATAL("open error:%d  message:%s", errno, strerror(errno));
            exit(DAEMON_ERROR);
        }
        dup2(wfd, 1);
        dup2(wfd, 2);
        close(wfd);
        int rfd = open("/dev/null", O_RDONLY);
        if (rfd == -1)
        {
            LOG_FATAL("open error:%d  message:%s", errno, strerror(errno));
            exit(DAEMON_ERROR);
        }
        dup2(rfd, 0);
        close(rfd);
    }
}

#endif
