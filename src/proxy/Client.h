#pragma once

#include "common.h"
#include "libs/SimpleWeb/utility.hpp"
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <system_error>

namespace asio { class io_context; }

using StatusCode = SimpleWeb::StatusCode;
using Header = SimpleWeb::CaseInsensitiveMultimap;

class Client final {
public:
  class Response final {
  public:
    Response(StatusCode status_code, Header header, std::error_code error, ByteVector data);
    Response(Response&&);
    Response& operator=(Response&&);
    ~Response();

    std::error_code error() const;
    StatusCode status_code() const;
    const Header& header() const;
    ByteView data() const;

  private:
    StatusCode m_status_code{ StatusCode::unknown };
    Header m_header;
    std::error_code m_error;
    ByteVector m_data;
  };

  using HandleResponse = std::function<void(Response)>;

  Client(std::string proxy_server, std::string upstream_ca,
    std::string impersonate);
  Client(Client&&);
  Client& operator=(Client&&);
  ~Client();

  void request(std::string_view url, std::string_view method,
    Header header, ByteView data, std::chrono::seconds timeout,
    HandleResponse handle_response);

private:
  std::shared_ptr<asio::io_context> m_io_context;
  std::string m_proxy_server;
  std::string m_upstream_ca;
  std::string m_impersonate;
};
