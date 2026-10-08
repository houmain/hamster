#pragma once

#include "libs/SimpleWeb/utility.hpp"
#include "common.h"
#include <functional>
#include <chrono>
#include <memory>
#include <optional>

using StatusCode = SimpleWeb::StatusCode;
using Header = SimpleWeb::CaseInsensitiveMultimap;

class ProxyRequest final {
public:
  struct TransformedResponse {
    Header header;
    ByteVector data;
  };
  using Send = std::function<void(
    StatusCode, const Header&, ByteView, std::shared_ptr<void>, std::function<void()>)>;
  using ResponseObserver = std::function<void(StatusCode, const Header&, ByteView)>;
  using ResponseTransformer =
    std::function<std::optional<TransformedResponse>(StatusCode, const Header&, ByteView)>;
  using ResponseHeaderTransformer = std::function<void(Header&)>;
  ProxyRequest(std::string method, std::string path, std::string query, Header header,
    ByteVector data, Send send, std::string target_url = {});
  const std::string& target_url() const;
  ProxyRequest(ProxyRequest&&);
  ProxyRequest& operator=(ProxyRequest&&);
  ~ProxyRequest();

  std::chrono::milliseconds age() const;
  const std::string& method() const;
  const std::string& path() const;
  const std::string& query() const;
  const Header& header() const;
  ByteView data() const;

  // Retain session ownership through request handling and response transmission.
  void hold_lifetime(std::shared_ptr<void> lifetime);
  void observe_response(ResponseObserver observer);
  void transform_response(ResponseTransformer transformer);
  void transform_response_headers(ResponseHeaderTransformer transformer);
  void send_response(StatusCode status_code, const Header& header, ByteView data,
    std::function<void()> on_sent = {});
  bool response_sent() const;

private:
  std::shared_ptr<void> m_lifetime;
  std::chrono::high_resolution_clock::time_point m_received_at;
  std::string m_method, m_path, m_query, m_target_url;
  Header m_header;
  ByteVector m_request_data;
  ResponseObserver m_response_observer;
  ResponseTransformer m_response_transformer;
  ResponseHeaderTransformer m_response_header_transformer;
  Send m_send;
};

bool is_success(StatusCode status_code);
bool is_redirect(StatusCode status_code);
void strip_proxy_headers(Header& headers);
