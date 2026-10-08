#include "ProxyRequest.h"
#include <cassert>
#include <sstream>
#include <utility>
#include <vector>

bool is_success(StatusCode status_code) {
  return (static_cast<int>(status_code) / 100 == 2);
}

bool is_redirect(StatusCode status_code) {
  return static_cast<int>(status_code) / 100 == 3 &&
    status_code != StatusCode::redirection_not_modified;
}

void strip_proxy_headers(Header& headers) {
  auto nominated = std::vector<std::string>{};
  auto range = headers.equal_range("Connection");
  for (auto it = range.first; it != range.second; ++it) {
    auto in = std::istringstream(it->second);
    auto token = std::string{};
    while (std::getline(in, token, ','))
      nominated.emplace_back(trim(token));
  }
  for (const auto& name : nominated)
    headers.erase(name);
  for (const auto* name :
    { "Connection", "Proxy-Connection", "Keep-Alive", "TE", "Trailer", "Transfer-Encoding",
      "Upgrade", "Proxy-Authorization", "Proxy-Authenticate" })
    headers.erase(name);
}

ProxyRequest::ProxyRequest(std::string method, std::string path, std::string query, Header header,
  ByteVector data, Send send, std::string target_url)
  : m_received_at(std::chrono::high_resolution_clock::now()), m_method(std::move(method)),
  m_path(std::move(path)), m_query(std::move(query)), m_target_url(std::move(target_url)),
  m_header(std::move(header)), m_request_data(std::move(data)), m_send(std::move(send)) {}

const std::string& ProxyRequest::target_url() const {
  return m_target_url;
}

ProxyRequest::ProxyRequest(ProxyRequest&&) = default;

ProxyRequest& ProxyRequest::operator=(ProxyRequest&&) = default;

ProxyRequest::~ProxyRequest() = default;

std::chrono::milliseconds ProxyRequest::age() const {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::high_resolution_clock::now() - m_received_at);
}

const std::string& ProxyRequest::method() const {
  return m_method;
}

const std::string& ProxyRequest::path() const {
  return m_path;
}

const std::string& ProxyRequest::query() const {
  return m_query;
}

const Header& ProxyRequest::header() const {
  return m_header;
}

ByteView ProxyRequest::data() const {
  return m_request_data;
}

void ProxyRequest::hold_lifetime(std::shared_ptr<void> lifetime) {
  if (m_lifetime)
    m_lifetime = std::make_shared<std::pair<std::shared_ptr<void>, std::shared_ptr<void>>>(
      std::move(m_lifetime), std::move(lifetime));
  else
    m_lifetime = std::move(lifetime);
}

void ProxyRequest::observe_response(ResponseObserver observer) {
  m_response_observer = std::move(observer);
}

void ProxyRequest::transform_response(ResponseTransformer transformer) {
  m_response_transformer = std::move(transformer);
}

void ProxyRequest::transform_response_headers(ResponseHeaderTransformer transformer) {
  m_response_header_transformer = std::move(transformer);
}

void ProxyRequest::send_response(
  StatusCode status_code, const Header& header, ByteView data, std::function<void()> on_sent) {
  assert(!response_sent());
  if (m_response_observer)
    m_response_observer(status_code, header, data);
  auto transformed = std::shared_ptr<TransformedResponse>{};
  if (m_response_transformer)
    if (auto value = m_response_transformer(status_code, header, data)) {
      transformed = std::make_shared<TransformedResponse>(std::move(*value));
      hold_lifetime(transformed);
      data = transformed->data;
    }
  auto transformed_header = std::shared_ptr<Header>{};
  if (m_response_header_transformer) {
    if (transformed)
      m_response_header_transformer(transformed->header);
    else {
      transformed_header = std::make_shared<Header>(header);
      m_response_header_transformer(*transformed_header);
      hold_lifetime(transformed_header);
    }
  }
  if (m_send) {
    auto send = std::exchange(m_send, {});
    send(status_code, transformed ? transformed->header
      : (transformed_header ? *transformed_header : header),
      data, m_lifetime, std::move(on_sent));
  }
}

bool ProxyRequest::response_sent() const {
  return !m_send;
}
