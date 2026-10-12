#include "Client.h"
#include "run_proxy.h"
#include "common.h"
#include <asio/io_context.hpp>
#include <asio/post.hpp>
#include <curl/curl.h>
#include <array>
#include <deque>
#include <map>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

extern "C" CURL_EXTERN CURLcode curl_easy_impersonate(
    CURL* curl, const char* target, int default_headers);

namespace {
  class CurlErrorCategory final : public std::error_category {
  public:
    const char* name() const noexcept override { return "curl"; }
    std::string message(int value) const override {
      return curl_easy_strerror(static_cast<CURLcode>(value));
    }
  };

  const std::error_category& curl_error_category() {
    static auto category = CurlErrorCategory{};
    return category;
  }

  std::error_code curl_error(CURLcode code) {
    return { static_cast<int>(code), curl_error_category() };
  }

  struct Transfer {
    std::shared_ptr<asio::io_context> io_context;
    std::string url, method, proxy_server, upstream_ca, impersonate;
    Header request_header, response_header;
    ByteVector request_data, response_data;
    std::chrono::seconds timeout;
    Client::HandleResponse handle_response;
    CURL* easy{};
    curl_slist* curl_headers{};
    std::array<char, CURL_ERROR_SIZE> error_buffer{};
  };

  size_t receive_body(char* data, size_t size, size_t count, void* user_data) noexcept {
    const auto bytes = size * count;
    auto& transfer = *static_cast<Transfer*>(user_data);
    try {
      const auto begin = reinterpret_cast<const std::byte*>(data);
      transfer.response_data.insert(transfer.response_data.end(), begin, begin + bytes);
      return bytes;
    }
    catch (...) {
      return 0;
    }
  }

  size_t receive_header(char* data, size_t size, size_t count, void* user_data) noexcept {
    const auto bytes = size * count;
    auto& transfer = *static_cast<Transfer*>(user_data);
    try {
      auto line = trim(std::string_view(data, bytes));
      if (starts_with(line, "HTTP/")) {
        transfer.response_header.clear();
      }
      else if (!line.empty()) {
        const auto colon = line.find(':');
        if (colon != std::string_view::npos) {
          const auto name = trim(line.substr(0, colon));
          const auto value = trim(line.substr(colon + 1));
          if (!name.empty())
            transfer.response_header.emplace(std::string(name), std::string(value));
        }
      }
      return bytes;
    }
    catch (...) {
      return 0;
    }
  }

  bool skip_request_header(std::string_view name) {
    return iequals_any(name, "Host", "Content-Length", "Connection", "Proxy-Connection",
      "Keep-Alive", "TE", "Trailer", "Transfer-Encoding", "Upgrade",
      "Proxy-Authorization", "Proxy-Authenticate");
  }

  CURLcode append_header(curl_slist*& headers, const std::string& value) {
    const auto appended = curl_slist_append(headers, value.c_str());
    if (!appended)
      return CURLE_OUT_OF_MEMORY;
    headers = appended;
    return CURLE_OK;
  }

  template<typename T>
  CURLcode set_option(CURL* easy, CURLoption option, T value) {
    return curl_easy_setopt(easy, option, value);
  }

  CURLcode prepare_transfer(Transfer& transfer) {
    transfer.easy = curl_easy_init();
    if (!transfer.easy)
      return CURLE_FAILED_INIT;

    auto result = curl_easy_impersonate(
      transfer.easy, transfer.impersonate.c_str(), 1);
    if (result != CURLE_OK)
      return result;

    const auto set = [&](CURLoption option, auto value) {
      if (result == CURLE_OK)
        result = set_option(transfer.easy, option, value);
      };

    set(CURLOPT_URL, transfer.url.c_str());
    set(CURLOPT_CUSTOMREQUEST, transfer.method.c_str());
    set(CURLOPT_FOLLOWLOCATION, 0L);
    set(CURLOPT_PROTOCOLS_STR, "http,https");
    set(CURLOPT_NOSIGNAL, 1L);
    set(CURLOPT_CONNECTTIMEOUT_MS,
      static_cast<long>(transfer.timeout.count() * 1000));
    set(CURLOPT_TIMEOUT_MS, static_cast<long>(transfer.timeout.count() * 1000));
    set(CURLOPT_TCP_KEEPALIVE, 1L);
    set(CURLOPT_WRITEFUNCTION, receive_body);
    set(CURLOPT_WRITEDATA, &transfer);
    set(CURLOPT_HEADERFUNCTION, receive_header);
    set(CURLOPT_HEADERDATA, &transfer);
    set(CURLOPT_ERRORBUFFER, transfer.error_buffer.data());

    // Keep Content-Encoding intact so archived bytes continue to match the
    // origin response headers. Curl still removes HTTP transfer framing.
    set(CURLOPT_HTTP_CONTENT_DECODING, 0L);
    set(CURLOPT_HTTP_TRANSFER_DECODING, 1L);

    if (!transfer.proxy_server.empty())
      set(CURLOPT_PROXY, transfer.proxy_server.c_str());
    if (!transfer.upstream_ca.empty())
      set(CURLOPT_CAINFO, transfer.upstream_ca.c_str());
#ifdef _WIN32
    else
      set(CURLOPT_SSL_OPTIONS, CURLSSLOPT_NATIVE_CA);
#endif

    const auto send_body = !transfer.request_data.empty() ||
      iequals_any(transfer.method, "POST", "PUT", "PATCH");
    if (send_body) {
      const auto data = transfer.request_data.empty()
        ? "" : reinterpret_cast<const char*>(transfer.request_data.data());
      set(CURLOPT_POSTFIELDS, data);
      set(CURLOPT_POSTFIELDSIZE_LARGE,
        static_cast<curl_off_t>(transfer.request_data.size()));
    }
    if (iequals(transfer.method, "HEAD"))
      set(CURLOPT_NOBODY, 1L);

    auto has_expect = false;
    for (const auto& [name, value] : transfer.request_header) {
      if (skip_request_header(name))
        continue;
      if (iequals(name, "Expect"))
        has_expect = true;
      auto line = name + (value.empty() ? ";" : ": " + value);
      if ((result = append_header(transfer.curl_headers, line)) != CURLE_OK)
        break;
    }
    if (result == CURLE_OK && send_body && !has_expect)
      result = append_header(transfer.curl_headers, "Expect:");
    if (result == CURLE_OK && transfer.curl_headers)
      result = set_option(transfer.easy, CURLOPT_HTTPHEADER, transfer.curl_headers);
    return result;
  }

  class CurlDispatcher final {
  public:
    static CurlDispatcher& instance() {
      static auto dispatcher = CurlDispatcher{};
      return dispatcher;
    }

    void validate_profile(const std::string& profile) {
      auto easy = curl_easy_init();
      if (!easy)
        throw std::runtime_error("Initializing curl-impersonate failed");
      const auto result = curl_easy_impersonate(easy, profile.c_str(), 1);
      curl_easy_cleanup(easy);
      if (result != CURLE_OK)
        throw std::invalid_argument(
          "Unknown curl-impersonate profile '" + profile + "'");
    }

    void submit(std::shared_ptr<Transfer> transfer) {
      {
        auto lock = std::lock_guard(m_mutex);
        if (m_stopping)
          throw std::runtime_error("curl-impersonate dispatcher is stopping");
        m_pending.push_back(std::move(transfer));
      }
      curl_multi_wakeup(m_multi);
    }

  private:
    CurlDispatcher() {
      const auto result = curl_global_init(CURL_GLOBAL_DEFAULT);
      if (result != CURLE_OK)
        throw std::runtime_error("Initializing curl-impersonate failed");
      m_multi = curl_multi_init();
      if (!m_multi) {
        curl_global_cleanup();
        throw std::runtime_error("Initializing curl-impersonate multi handle failed");
      }
      curl_multi_setopt(m_multi, CURLMOPT_MAX_HOST_CONNECTIONS, 6L);
      curl_multi_setopt(m_multi, CURLMOPT_MAX_TOTAL_CONNECTIONS, 32L);
      curl_multi_setopt(m_multi, CURLMOPT_PIPELINING, CURLPIPE_MULTIPLEX);
      m_worker = std::thread([this] { run(); });
    }

    ~CurlDispatcher() {
      {
        auto lock = std::lock_guard(m_mutex);
        m_stopping = true;
      }
      curl_multi_wakeup(m_multi);
      if (m_worker.joinable())
        m_worker.join();
      curl_multi_cleanup(m_multi);
      curl_global_cleanup();
    }

    void add_pending() {
      auto pending = std::deque<std::shared_ptr<Transfer>>{};
      {
        auto lock = std::lock_guard(m_mutex);
        pending.swap(m_pending);
      }
      for (auto& transfer : pending) {
        const auto result = prepare_transfer(*transfer);
        if (result != CURLE_OK) {
          complete(std::move(transfer), result, false);
          continue;
        }
        if (curl_multi_add_handle(m_multi, transfer->easy) != CURLM_OK) {
          complete(std::move(transfer), CURLE_FAILED_INIT, false);
          continue;
        }
        m_active.emplace(transfer->easy, std::move(transfer));
      }
    }

    void complete(std::shared_ptr<Transfer> transfer, CURLcode result, bool added) {
      auto status = long{};
      if (transfer->easy)
        curl_easy_getinfo(transfer->easy, CURLINFO_RESPONSE_CODE, &status);
      if (added)
        curl_multi_remove_handle(m_multi, transfer->easy);
      if (transfer->easy)
        curl_easy_cleanup(transfer->easy);
      if (transfer->curl_headers)
        curl_slist_free_all(transfer->curl_headers);
      transfer->easy = nullptr;
      transfer->curl_headers = nullptr;

      auto error = std::error_code{};
      if (result != CURLE_OK)
        error = curl_error(result);
      auto error_message = std::string(transfer->error_buffer.data());
      if (error_message.empty() && error)
        error_message = error.message();

      asio::post(*transfer->io_context,
        [handler = std::move(transfer->handle_response), status,
        header = std::move(transfer->response_header), error,
        error_message = std::move(error_message),
        data = std::move(transfer->response_data)]() mutable {
          handler(Client::Response(
            static_cast<StatusCode>(status), std::move(header), error,
            std::move(error_message), std::move(data)));
        });
    }

    bool stopping() {
      auto lock = std::lock_guard(m_mutex);
      return m_stopping && m_pending.empty();
    }

    void run() {
      for (;;) {
        add_pending();

        auto running = 0;
        if (curl_multi_perform(m_multi, &running) != CURLM_OK) {
          for (auto& [_, transfer] : m_active)
            complete(transfer, CURLE_FAILED_INIT, true);
          m_active.clear();
        }

        auto messages = 0;
        while (const auto message = curl_multi_info_read(m_multi, &messages)) {
          if (message->msg != CURLMSG_DONE)
            continue;
          const auto found = m_active.find(message->easy_handle);
          if (found == m_active.end())
            continue;
          auto transfer = std::move(found->second);
          m_active.erase(found);
          complete(std::move(transfer), message->data.result, true);
        }

        if (stopping() && m_active.empty())
          break;

        auto descriptors = 0;
        curl_multi_poll(m_multi, nullptr, 0, 1000, &descriptors);
      }
    }

    CURLM* m_multi{};
    std::thread m_worker;
    std::mutex m_mutex;
    std::deque<std::shared_ptr<Transfer>> m_pending;
    std::map<CURL*, std::shared_ptr<Transfer>> m_active;
    bool m_stopping{};
  };
} // namespace

Client::Response::Response(
  StatusCode status_code, Header header, std::error_code error,
  std::string error_message, ByteVector data)
  : m_status_code(status_code), m_header(std::move(header)), m_error(error),
  m_error_message(std::move(error_message)), m_data(std::move(data)) {}

Client::Response::Response(Response&&) = default;
Client::Response& Client::Response::operator=(Response&&) = default;
Client::Response::~Response() = default;

StatusCode Client::Response::status_code() const {
  return m_status_code;
}

const Header& Client::Response::header() const {
  return m_header;
}

std::error_code Client::Response::error() const {
  return m_error;
}

const std::string& Client::Response::error_message() const {
  return m_error_message;
}

ByteView Client::Response::data() const {
  return m_data;
}

Client::Client(std::string proxy_server, std::string upstream_ca, std::string impersonate)
  : m_io_context(proxy_io_context()), m_proxy_server(std::move(proxy_server)),
  m_upstream_ca(std::move(upstream_ca)), m_impersonate(std::move(impersonate)) {
  CurlDispatcher::instance().validate_profile(m_impersonate);
}

Client::Client(Client&&) = default;
Client& Client::operator=(Client&&) = default;
Client::~Client() = default;

void Client::request(std::string_view url, std::string_view method,
  Header header, ByteView data, std::chrono::seconds timeout,
  HandleResponse handle_response) {
  const auto scheme = get_scheme(url);
  if (scheme != "http" && scheme != "https")
    throw std::runtime_error("invalid scheme");

  auto transfer = std::make_shared<Transfer>();
  transfer->io_context = m_io_context;
  transfer->url = std::string(url);
  transfer->method = std::string(method);
  transfer->proxy_server = m_proxy_server;
  transfer->upstream_ca = m_upstream_ca;
  transfer->impersonate = m_impersonate;
  transfer->request_header = std::move(header);
  transfer->request_data.assign(data.begin(), data.end());
  transfer->timeout = timeout;
  transfer->handle_response = std::move(handle_response);
  CurlDispatcher::instance().submit(std::move(transfer));
}
