#ifndef CLOUD_BACKUP_HTTP_CONNECTION_HPP
#define CLOUD_BACKUP_HTTP_CONNECTION_HPP

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
        enum State
        {
            Waiting,
            Processing,
            Stop,
            Closed
        };
        HTTPConnection(int net_fd, int write_pipe_fd, const std::string &client_ip, uint16_t client_port)
            : _net_fd(net_fd), _write_pipe_fd(write_pipe_fd), _client_ip(client_ip), _client_port(client_port)
        {
            llhttp_settings_init(&_settings);
            _settings.on_message_begin = static_on_message_begin;
            _settings.on_method = static_on_method;
            _settings.on_url = static_on_url;
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
        State _connect_state = Waiting;
        const int _net_fd;
        const int _write_pipe_fd;
        std::string _client_ip;
        uint16_t _client_port;
        std::string _request_buffer;
        std::mutex _request_mutex;
        std::string _response_buffer;
        std::mutex _response_mutex;

    private:
        struct HeaderInfo
        {
            // Requset Info
            std::string _requset_method;
            std::string _requset_url;
            std::string _requset_version;
            std::string _requset_cur_header_field;
            std::string _requset_cur_header_value;
            std::unordered_map<std::string, std::string> _requset_headers;

            // Response Info
            std::string _response_version;
            std::string _response_status;
            std::string _response_status_describe;
            std::unordered_map<std::string, std::string> _response_headers;

            void clear()
            {
                // Requset clear
                _requset_method.clear();
                _requset_url.clear();
                _requset_version.clear();
                _requset_cur_header_field.clear();
                _requset_cur_header_value.clear();
                _requset_headers.clear();

                // Response clear
                _response_version.clear();
                _response_status.clear();
                _response_status_describe.clear();
                _response_headers.clear();
            }
        };
        llhttp_settings_t _settings;
        llhttp_t _parser;
        HeaderInfo _head_info;

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
            _head_info._requset_method += std::string(at, length);
            return 0;
        }
        int on_url(llhttp_t *parser, const char *at, size_t length)
        {
            _head_info._requset_url += std::string(at, length);
            return 0;
        }
        int on_version(llhttp_t *parser, const char *at, size_t length)
        {
            _head_info._requset_version += std::string(at, length);
            return 0;
        }
        int on_header_field(llhttp_t *parser, const char *at, size_t length)
        {
            _head_info._requset_cur_header_field += std::string(at, length);
            return 0;
        }
        int on_header_value(llhttp_t *parser, const char *at, size_t length)
        {
            _head_info._requset_cur_header_value += std::string(at, length);
            return 0;
        }
        int on_header_value_complete(llhttp_t *parser)
        {
            _head_info._requset_headers[_head_info._requset_cur_header_field] = _head_info._requset_cur_header_value;
            _head_info._requset_cur_header_field.clear();
            _head_info._requset_cur_header_value.clear();
            return 0;
        }
        int on_headers_complete(llhttp_t *parser)
        {
            // TODO
        }
        int on_body(llhttp_t *parser, const char *at, size_t length)
        {
            // TODO
        }
        int on_message_complete(llhttp_t *parser)
        {
            // TODO
        }

    public:
        static void handler(HTTPConnection::ptr object)
        {
            int handle_size = Config::GetInstance()->GetPerHandleRequsetSize();
            {
                std::unique_lock<std::shared_mutex> request_lock(object->_request_mutex);
            }
            llhttp_execute(&object->_parser, );
        }
        // static void handler(HTTPConnection::ptr object)
        // {
        //     std::string tmp_request_buffer;
        //     {
        //         std::unique_lock<std::mutex> request_lock(object->_request_mutex);
        //         tmp_request_buffer.swap(object->_request_buffer);
        //         object->_is_ready_handle = false;
        //         object->_request_buffer.clear();
        //     }
        //     {
        //         std::unique_lock<std::mutex> response_lock(object->_response_mutex);
        //         object->_response_buffer += tmp_request_buffer;
        //     }
        //     if (object->_net_fd != -1)
        //     {
        //         std::string tmp = to_string(object->_net_fd) + ",";
        //         write(object->_write_pipe_fd, tmp.c_str(), tmp.size());
        //     }
        // }
    };
}

#endif