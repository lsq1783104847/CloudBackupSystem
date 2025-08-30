#ifndef CLOUD_BACKUP_HTTP_CONNECTION_HPP
#define CLOUD_BACKUP_HTTP_CONNECTION_HPP

#include <functional>
#include <algorithm>
#include <atomic>
#include "data_manager.hpp"
#include "ThreadPool.hpp"

namespace cloud_backup
{
    using fun_t = std::function<void()>;
    using TaskThreadPool = ThreadPool<fun_t>;
    const std::string SEP = "/r/n";

    class HTTPConnection
    {
    public:
        using ptr = std::shared_ptr<HTTPConnection>;
        HTTPConnection(const std::string &net_fd_identifier, int write_pipe_fd, const std::string &client_ip, uint16_t client_port)
            : _net_fd_identifier(net_fd_identifier), _write_pipe_fd(write_pipe_fd), _client_ip(client_ip), _client_port(client_port)
        {
            llhttp_settings_init(&_settings);
            _settings.on_message_begin = static_on_message_begin;
            _settings.on_method = static_on_method;
            _settings.on_url = static_on_url;
            _settings.on_url_complete = static_on_url_complete;
            _settings.on_version = static_on_version;
            _settings.on_header_field = static_on_header_field;
            _settings.on_header_value = static_on_header_value;
            _settings.on_header_value_complete = static_on_header_value_complete;
            _settings.on_headers_complete = static_on_headers_complete;
            _settings.on_body = static_on_body;
            _settings.on_message_complete = static_on_message_complete;

            _parser.data = this;
            llhttp_init(&_parser, HTTP_REQUEST, &_settings);
        }
        ~HTTPConnection() {}

    public:
        std::atomic<bool> _is_closed = false; // 当前连接是否已关闭
        const std::string _net_fd_identifier; // 当前连接的唯一标识符，由"fd_usetime"组成
        const int _write_pipe_fd;             // pipe_fd用于通知主线程当前连接有哪些事件发生需要主线程处理
        const std::string _client_ip;         // 客户端IP地址
        const uint16_t _client_port;          // 客户端端口号
        bool _is_processing = false;          // 是否正在处理当前连接读取上来的数据，与_request_buffer共用一把锁保证线程安全
        std::string _request_buffer;          // 存放当前连接读取上来的数据，与_is_processing共用一把锁保证线程安全
        std::mutex _request_mutex;            // 保护_is_processing和_request_buffer的互斥锁
        std::string _response_buffer;         // 存放当前连接要发送给客户端的数据
        std::mutex _response_mutex;           // 保护_response_buffer的互斥锁

    private:
        struct HTTPMessageInfo
        {
            ~HTTPMessageInfo()
            {
                if (_cur_upload_file != "" && !DataManager::GetInstance()->Deregister(_cur_upload_file))
                    LOG_ERROR("~HTTPMessageInfo ERROR, deregister file fail, filename:%s", _cur_upload_file.c_str());
            }
            // Request Info
            std::string _request_method;
            std::string _request_url;
            std::string _request_url_prefix;
            std::string _request_url_path;
            std::string _request_version;
            std::string _request_cur_header_field;
            std::string _request_cur_header_value;
            std::unordered_map<std::string, std::string> _request_headers;
            std::string _request_body;

            // upload Info
            std::string _body_boundary;
            std::string _cur_upload_file;
            std::vector<std::string> _upload_success_files;
            std::vector<std::string> _upload_fail_files;

            // download Info
            std::string _cur_download_file;

            // delete Info
            std::string _cur_delete_file;

            // Response Info
            std::string _response_version;
            std::string _response_status;
            std::string _response_status_describe;
            std::unordered_map<std::string, std::string> _response_headers;
            std::string _response_body;

            void clear()
            {
                // request Info clear
                _request_method.clear();
                _request_url.clear();
                _request_url_prefix.clear();
                _request_url_path.clear();
                _request_version.clear();
                _request_cur_header_field.clear();
                _request_cur_header_value.clear();
                _request_headers.clear();
                _request_body.clear();

                // upload Info clear
                _body_boundary.clear();
                _cur_upload_file.clear();
                _upload_success_files.clear();
                _upload_fail_files.clear();

                // download Info clear
                _cur_download_file.clear();

                // delete Info clear
                _cur_delete_file.clear();

                // Response Info clear
                _response_version.clear();
                _response_status.clear();
                _response_status_describe.clear();
                _response_headers.clear();
                _response_body.clear();
            }
            std::string response_seralize()
            {
                std::string response;
                response += _response_version + ' ' + _response_status + ' ' + _response_status_describe + SEP;
                for (auto &header : _response_headers)
                    response += header.first + ": " + header.second + SEP;
                response += SEP;
                response += _response_body;
                return response;
            }
        };
        llhttp_settings_t _settings;
        llhttp_t _parser;
        HTTPMessageInfo _head_info;
        fun_t _sub_task;

    private:
        static int static_on_message_begin(llhttp_t *parser)
        {
            HTTPConnection *connect = static_cast<HTTPConnection *>(parser->data);
            return connect->on_message_begin(parser);
        }
        static int static_on_method(llhttp_t *parser, const char *at, size_t length)
        {
            HTTPConnection *connect = static_cast<HTTPConnection *>(parser->data);
            return connect->on_method(parser, at, length);
        }
        static int static_on_url(llhttp_t *parser, const char *at, size_t length)
        {
            HTTPConnection *connect = static_cast<HTTPConnection *>(parser->data);
            return connect->on_url(parser, at, length);
        }
        static int static_on_url_complete(llhttp_t *parser)
        {
            HTTPConnection *connect = static_cast<HTTPConnection *>(parser->data);
            return connect->on_url_complete(parser);
        }
        static int static_on_version(llhttp_t *parser, const char *at, size_t length)
        {
            HTTPConnection *connect = static_cast<HTTPConnection *>(parser->data);
            return connect->on_version(parser, at, length);
        }
        static int static_on_header_field(llhttp_t *parser, const char *at, size_t length)
        {
            HTTPConnection *connect = static_cast<HTTPConnection *>(parser->data);
            return connect->on_header_field(parser, at, length);
        }
        static int static_on_header_value(llhttp_t *parser, const char *at, size_t length)
        {
            HTTPConnection *connect = static_cast<HTTPConnection *>(parser->data);
            return connect->on_header_value(parser, at, length);
        }
        static int static_on_header_value_complete(llhttp_t *parser)
        {
            HTTPConnection *connect = static_cast<HTTPConnection *>(parser->data);
            return connect->on_header_value_complete(parser);
        }
        static int static_on_headers_complete(llhttp_t *parser)
        {
            HTTPConnection *connect = static_cast<HTTPConnection *>(parser->data);
            return connect->on_headers_complete(parser);
        }
        static int static_on_body(llhttp_t *parser, const char *at, size_t length)
        {
            HTTPConnection *connect = static_cast<HTTPConnection *>(parser->data);
            return connect->on_body(parser, at, length);
        }
        static int static_on_message_complete(llhttp_t *parser)
        {
            HTTPConnection *connect = static_cast<HTTPConnection *>(parser->data);
            return connect->on_message_complete(parser);
        }

        int on_message_begin(llhttp_t *parser)
        {
            _head_info.clear();
            return 0;
        }
        int on_method(llhttp_t *parser, const char *at, size_t length)
        {
            std::string tmp_method(at, length);
            for (auto &e : tmp_method)
                if (e >= 'a' && e <= 'z')
                    e -= 'a' - 'A';
            _head_info._request_method += tmp_method;
            return 0;
        }
        int on_url(llhttp_t *parser, const char *at, size_t length)
        {
            _head_info._request_url += std::string(at, length);
            return 0;
        }
        int on_url_complete(llhttp_t *parser)
        {
            int pos = _head_info._request_url.find_first_of('/', 1);
            _head_info._request_url_prefix = _head_info._request_url.substr(0, pos);
            if (pos < _head_info._request_url.size())
                _head_info._request_url_path = _head_info._request_url.substr(pos);
            return 0;
        }
        int on_version(llhttp_t *parser, const char *at, size_t length)
        {
            _head_info._request_version += std::string(at, length);
            return 0;
        }
        int on_header_field(llhttp_t *parser, const char *at, size_t length)
        {
            std::string tmp_header_field(at, length);
            for (auto &e : tmp_header_field)
                if (e >= 'A' && e <= 'Z')
                    e += 'a' - 'A';
            _head_info._request_cur_header_field += tmp_header_field;
            return 0;
        }
        int on_header_value(llhttp_t *parser, const char *at, size_t length)
        {
            std::string tmp_header_value(at, length);
            for (auto &e : tmp_header_value)
                if (e >= 'A' && e <= 'Z')
                    e += 'a' - 'A';
            _head_info._request_cur_header_value += tmp_header_value;
            return 0;
        }
        int on_header_value_complete(llhttp_t *parser)
        {
            _head_info._request_headers[_head_info._request_cur_header_field] = _head_info._request_cur_header_value;
            _head_info._request_cur_header_field.clear();
            _head_info._request_cur_header_value.clear();
            return 0;
        }
        int on_headers_complete(llhttp_t *parser)
        {
            _head_info._response_version = _head_info._request_version;
            if (_head_info._request_method == "GET" && _head_info._request_url_prefix == "/download")
            {
                _head_info._cur_download_file = _head_info._request_url_path.substr(1);
                if (!FileUtil::check_filename(_head_info._cur_download_file))
                {
                    LOG_WARN("process download Request fail, download filename is invalid");
                    _head_info._response_status = "404";
                    _head_info._response_status_describe = "Not Found";
                }
            }
            else if (_head_info._request_method == "DELETE" && _head_info._request_url_prefix == "/delete")
            {
                _head_info._cur_delete_file = _head_info._request_url_path.substr(1);
                if (!FileUtil::check_filename(_head_info._cur_delete_file))
                {
                    LOG_WARN("process delete Request fail, delete filename is invalid");
                    _head_info._response_status = "404";
                    _head_info._response_status_describe = "Not Found";
                }
            }
            else if (_head_info._request_method == "POST" && _head_info._request_url_prefix == "/upload")
            {
                auto it = _head_info._request_headers.find("content-type");
                std::string boundary_key = "boundary=";
                if (it != _head_info._request_headers.end() && it->second.find_first_of(boundary_key) != std::string::npos)
                    _head_info._body_boundary = "--" + it->second.substr(it->second.find_first_of(boundary_key) + boundary_key.size());
                else
                {
                    LOG_WARN("process upload Request fail, content-type is invalid");
                    _head_info._response_status = "400";
                    _head_info._response_status_describe = "Bad Request";
                }
            }
            return 0;
        }
        int on_body(llhttp_t *parser, const char *at, size_t length)
        {
            if (_head_info._response_status != "")
                return 0;
            if (_head_info._request_method == "POST" && _head_info._request_url_prefix == "/upload")
            {
                _head_info._request_body += std::string(at, length);
                if (process_upload_body() == false)
                {
                    LOG_WARN("process upload Request fail, process_upload_body error");
                    _head_info._response_status = "400";
                    _head_info._response_status_describe = "Bad Request";
                    return 0;
                }
            }
            return 0;
        }
        int on_message_complete(llhttp_t *parser)
        {
            if (_head_info._response_status == "")
            {
                if (_head_info._request_method == "GET" && (_head_info._request_url_prefix == "/" || _head_info._request_url_prefix == "/showlist"))
                    process_showlist_request();
                else if (_head_info._request_method == "GET" && _head_info._request_url_prefix == "/download")
                    process_download_request();
                else if (_head_info._request_method == "DELETE" && _head_info._request_url_prefix == "/delete")
                    process_delete_request();
                else if (_head_info._request_method == "POST" && _head_info._request_url_prefix == "/upload")
                    process_upload_request();
                else if (_head_info._request_method == "GET" && _head_info._request_url_prefix == "/api")
                    process_api_request();
                else
                {
                    LOG_WARN("process Request fail, url is invalid");
                    _head_info._response_status = "404";
                    _head_info._response_status_describe = "Not Found";
                }
            }
            std::string response = _head_info.response_seralize();
            {
                std::unique_lock<std::mutex> response_lock(_response_mutex);
                _response_buffer += response;
            }
            notify_new_message_need_send();
            return HPE_PAUSED;
        }

        bool process_upload_body()
        {
            while (_head_info._request_body.size() > _head_info._body_boundary.size())
            {
                int pos = _head_info._request_body.find_first_of(_head_info._body_boundary);
                if (_head_info._cur_upload_file == "")
                {
                    if (pos == std::string::npos)
                    {
                        if (_head_info._request_body.size() > _head_info._body_boundary.size())
                            _head_info._request_body.erase(0, _head_info._request_body.size() - _head_info._body_boundary.size());
                    }
                    else
                    {
                        if (pos > 0)
                            _head_info._request_body.erase(0, pos);
                        int head_end_pos = _head_info._request_body.find_first_of(SEP + SEP);
                        if (head_end_pos == std::string::npos)
                            break;
                        std::string filename_key = "filename=\"";
                        int filename_pos = _head_info._request_body.find_first_of(filename_key);
                        if (filename_pos == std::string::npos || filename_pos > head_end_pos)
                            return false;
                        filename_pos += filename_key.size();
                        int filename_end_pos = _head_info._request_body.find_first_of('"', filename_pos);
                        if (filename_end_pos == std::string::npos || filename_end_pos > head_end_pos)
                            return false;
                        _head_info._cur_upload_file = _head_info._request_body.substr(filename_pos, filename_end_pos - filename_pos);
                        _head_info._request_body.erase(0, head_end_pos + SEP.size() * 2);
                        if (!FileUtil::check_filename(_head_info._cur_upload_file) ||
                            !DataManager::GetInstance()->Register(_head_info._cur_upload_file))
                        {
                            _head_info._upload_fail_files.push_back(_head_info._cur_upload_file);
                            _head_info._cur_upload_file.clear();
                            continue;
                        }
                    }
                }
                else
                {
                    std::string file_content;
                    if (pos == std::string::npos)
                    {
                        if (_head_info._request_body.size() > _head_info._body_boundary.size())
                            file_content = _head_info._request_body.substr(0, _head_info._request_body.size() - _head_info._body_boundary.size());
                    }
                    else
                        file_content = _head_info._request_body.substr(0, pos);
                    _head_info._request_body.erase(0, file_content.size());
                    if (file_content.empty())
                        continue;
                    std::string target_file_dir = Config::GetInstance()->GetBackupFileDir();
                    if (target_file_dir.back() != '/')
                        target_file_dir += '/';
                    FileUtil target_file(target_file_dir + _head_info._cur_upload_file);
                    if (!target_file.AppendContent(file_content))
                    {
                        if (!DataManager::GetInstance()->Deregister(_head_info._cur_upload_file))
                            LOG_ERROR("process_upload_body error, Deregister fail, filename:%s", _head_info._cur_upload_file.c_str());
                        _head_info._upload_fail_files.push_back(_head_info._cur_upload_file);
                        _head_info._cur_upload_file.clear();
                        continue;
                    }
                    if (pos != std::string::npos)
                    {
                        _head_info._upload_success_files.push_back(_head_info._cur_upload_file);
                        DataManager::GetInstance()->Insert(_head_info._cur_upload_file, target_file.GetFileSize());
                        _head_info._cur_upload_file.clear();
                        continue;
                    }
                }
            }
            return true;
        }

        void process_showlist_request()
        {
            FileUtil default_file("./wwwroot/default.html");
            std::string file_content;
            if (!default_file.GetContent(&file_content))
            {
                LOG_ERROR("client_ip:%s client_port:%d Get File Content error, filename:default.html",
                          _client_ip.c_str(), _client_port);
                notify_close_curent_connection();
                return;
            }
            _head_info._response_status = "200";
            _head_info._response_status_describe = "OK";
            _head_info._response_headers["Content-Type"] = "text/html";
            _head_info._response_headers["Content-Length"] = std::to_string(file_content.size());
            _head_info._response_body = file_content;
        }
        void process_download_request()
        {
            auto data_manager = DataManager::GetInstance();
            DataManagerNode::ptr file_info_node = data_manager->GetFileInfoNode(_head_info._cur_download_file);
            if (file_info_node == nullptr)
            {
                LOG_WARN("process download Request fail, filename not found, filename:%s", _head_info._cur_download_file.c_str());
                _head_info._response_status = "404";
                _head_info._response_status_describe = "Not Found";
                return;
            }
            std::string ETag = file_info_node->_info._filename + '-' + std::to_string(file_info_node->_info._time) + '-' + std::to_string(file_info_node->_info._size);
            _head_info._response_status = "200";
            _head_info._response_status_describe = "OK";
            _head_info._response_headers["Content-Type"] = "application/octet-stream";
            _head_info._response_headers["Accept-Ranges"] = "bytes";
            _head_info._response_headers["ETag"] = ETag;
            _head_info._response_headers["Content-Length"] = std::to_string(file_info_node->_info._size);
            _head_info._response_headers["Content-Disposition"] = "attachment; filename=\"" + file_info_node->_info._filename + '"';
            long long start_pos = 0;
            long long end_pos = file_info_node->_info._size;
            if (_head_info._request_headers.find("if-range") != _head_info._request_headers.end() &&
                _head_info._request_headers.find("range") != _head_info._request_headers.end())
            {
                if (_head_info._request_headers["if-range"] == ETag && _head_info._request_headers["range"].find_first_of("bytes=") == 0)
                {
                    std::string range_value = _head_info._request_headers["range"].substr(6);
                    int dash_pos = range_value.find_first_of('-');
                    start_pos = std::stoll(range_value.substr(0, dash_pos));
                    if (dash_pos + 1 < range_value.size())
                        end_pos = std::min(std::stoll(range_value.substr(dash_pos + 1)), end_pos);
                }
                _head_info._response_status = "206";
                _head_info._response_status_describe = "Partial Content";
            }
            _sub_task = std::bind(&HTTPConnection::sendFile, HTTPConnection::ptr(this), file_info_node, start_pos, end_pos);
        }
        void process_delete_request()
        {
            auto data_manager = DataManager::GetInstance();
            if (data_manager->Delete(_head_info._cur_delete_file))
            {
                _head_info._response_status = "200";
                _head_info._response_status_describe = "OK";
            }
            else
            {
                LOG_WARN("process delete Request fail, filename not found, filename:%s", _head_info._cur_delete_file.c_str());
                _head_info._response_status = "404";
                _head_info._response_status_describe = "Not Found";
            }
        }
        void process_upload_request()
        {
            if (_head_info._upload_fail_files.empty())
            {
                _head_info._response_status = "200";
                _head_info._response_status_describe = "OK";
            }
            else if (_head_info._upload_success_files.empty())
            {
                _head_info._response_status = "400";
                _head_info._response_status_describe = "Bad Request";
            }
            else
            {
                _head_info._response_status = "207";
                _head_info._response_status_describe = "Multi-Status";
            }
            Json::Value root;
            root["success_count"] = (Json::Int64)_head_info._upload_success_files.size();
            root["fail_count"] = (Json::Int64)_head_info._upload_fail_files.size();
            root["total_count"] = (Json::Int64)(_head_info._upload_success_files.size() + _head_info._upload_fail_files.size());
            for (auto &file : _head_info._upload_success_files)
                root["success_files"].append(file);
            for (auto &file : _head_info._upload_fail_files)
                root["fail_files"].append(file);
            std::string response_body;
            if (!JsonUtil::Serialize(root, &response_body))
            {
                LOG_ERROR("process upload Request fail, JsonUtil::Serialize error");
                response_body.clear();
                return;
            }
            _head_info._response_headers["Content-Type"] = "application/json";
            _head_info._response_headers["Content-Length"] = std::to_string(response_body.size());
            _head_info._response_body = response_body;
        }
        void process_api_request()
        {
            if (_head_info._request_url_path == "/GetBackupFiles")
            {
                auto data_manager = DataManager::GetInstance();
                std::vector<BackupInfoNode> all_files;
                data_manager->GetAllBackupInfo(&all_files);
                Json::Value root;
                for (auto &file_info : all_files)
                {
                    Json::Value file_node;
                    file_node["filename"] = file_info._filename;
                    file_node["size"] = Json::Int64(file_info._size);
                    file_node["time"] = Json::Int64(file_info._time);
                    root["files"].append(file_node);
                }
                std::string response_body;
                if (!JsonUtil::Serialize(root, &response_body))
                {
                    LOG_ERROR("process api Request fail, JsonUtil::Serialize error");
                    _head_info._response_status = "404";
                    _head_info._response_status_describe = "Not Found";
                    return;
                }
                _head_info._response_status = "200";
                _head_info._response_status_describe = "OK";
                _head_info._response_headers["Content-Type"] = "application/json";
                _head_info._response_headers["Content-Length"] = std::to_string(response_body.size());
                _head_info._response_body = response_body;
            }
            else
            {
                LOG_WARN("process api Request fail, url is invalid");
                _head_info._response_status = "404";
                _head_info._response_status_describe = "Not Found";
            }
        }

        void notify_close_curent_connection()
        {
            std::string notify_message = 'c' + _net_fd_identifier + ',';
            write(_write_pipe_fd, notify_message.c_str(), notify_message.size());
        }
        void notify_new_message_need_send()
        {
            std::string notify_message = 'w' + _net_fd_identifier + ',';
            write(_write_pipe_fd, notify_message.c_str(), notify_message.size());
        }

    public:
        // 连接数据的处理函数，在数据处理时出现任何异常和错误都直接通知主进程关闭当前连接
        static void handler(HTTPConnection::ptr object)
        {
            if (object->_is_closed)
                return;
            object->_sub_task = fun_t();
            size_t handle_size = Config::GetInstance()->GetPerHandleRequestSize();
            std::string cur_handle_request;
            {
                std::unique_lock<std::mutex> request_lock(object->_request_mutex);
                handle_size = std::min(object->_request_buffer.size(), handle_size);
                cur_handle_request = object->_request_buffer.substr(0, handle_size);
            }

            int err = llhttp_execute(&object->_parser, cur_handle_request.c_str(), cur_handle_request.size());
            if (err != HPE_OK && err != HPE_PAUSED)
            {
                LOG_ERROR("process Request fail, will close current connection, llhttp err: %s", llhttp_errno_name((llhttp_errno)err));
                object->notify_close_curent_connection();
                return;
            }
            if (err == HPE_PAUSED)
                handle_size = llhttp_get_error_pos(&object->_parser) - cur_handle_request.c_str();
            {
                std::unique_lock<std::mutex> request_lock(object->_request_mutex);
                object->_request_buffer.erase(0, handle_size);
                if (object->_sub_task == nullptr)
                {
                    if (object->_request_buffer.empty())
                        object->_is_processing = false;
                    else
                        object->_sub_task = std::bind(&HTTPConnection::handler, object);
                }
            }
            if (object->_sub_task)
            {
                if (!TaskThreadPool::GetInstance()->try_push(object->_sub_task))
                    object->_sub_task();
            }
        }
        // 将文件内容分段的写入到发送缓冲区中(默认之前已经构建好了HTTP响应报头并已经放入其中，现在放入的是HTTP响应的body)
        // 如果出现任何异常和错误都直接通知主进程关闭当前连接
        static void sendFile(HTTPConnection::ptr object, DataManagerNode::ptr file_info_node, long long start_pos, long long end_pos)
        {
            if (object->_is_closed)
                return;
            object->_sub_task = fun_t();

            auto data_manager = DataManager::GetInstance();
            if (file_info_node == nullptr)
            {
                object->notify_close_curent_connection();
                return;
            }
            if (start_pos <= end_pos)
                LOG_INFO("read position more than file:%s tail", file_info_node->_info._filename.size());
            else
            {
                std::string file_content;
                if (start_pos == 0)
                    file_content = data_manager->GetFilePreContent(file_info_node->_info._filename);
                if (file_content == "")
                {
                    long long read_size = Config::GetInstance()->GetMaxFileReadSize();
                    read_size = std::min(read_size, end_pos - start_pos);
                    std::string target_file_dir = Config::GetInstance()->GetBackupFileDir();
                    if (target_file_dir.back() != '/')
                        target_file_dir += '/';
                    FileUtil target_file(target_file_dir + file_info_node->_info._filename);
                    {
                        std::shared_lock<std::shared_mutex> file_read_lock(file_info_node->_rwlock);
                        if (!target_file.GetContent(&file_content, start_pos, read_size))
                        {
                            LOG_ERROR("client_ip:%s client_port:%d Get File Content error, filename:%s",
                                      object->_client_ip.c_str(), object->_client_port, file_info_node->_info._filename.c_str());
                            object->notify_close_curent_connection();
                            return;
                        }
                    }
                    if (start_pos == 0 && !data_manager->PutFilePreContent(file_info_node->_info._filename, file_content))
                    {
                        LOG_ERROR("client_ip:%s client_port:%d Put File TO LRU error, filename:%s",
                                  object->_client_ip.c_str(), object->_client_port, file_info_node->_info._filename.c_str());
                        object->notify_close_curent_connection();
                        return;
                    }
                }
                {
                    std::unique_lock<std::mutex> response_lock(object->_response_mutex);
                    object->_response_buffer += file_content;
                }
                object->notify_new_message_need_send();
                start_pos += file_content.size();
                if (start_pos < end_pos)
                    object->_sub_task = std::bind(&HTTPConnection::sendFile, object, file_info_node, start_pos, end_pos);
            }
            if (object->_sub_task == nullptr)
            {
                std::unique_lock<std::mutex> request_lock(object->_request_mutex);
                if (object->_request_buffer.empty())
                    object->_is_processing = false;
                else
                    object->_sub_task = std::bind(&HTTPConnection::handler, object);
            }
            if (object->_sub_task)
            {
                if (!TaskThreadPool::GetInstance()->try_push(object->_sub_task))
                    object->_sub_task();
            }
        }
    };
}

#endif