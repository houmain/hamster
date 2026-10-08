#include "ProxyServer.h"
#include "run_proxy.h"
#include "CertificateAuthority.h"
#include "Settings.h"
#include "common.h"
#include "platform.h"
#include "libs/SimpleWeb/server_https.hpp"
#include <algorithm>
#include <array>
#include <map>
#include <regex>
#include <sstream>
#include <utility>
#include <vector>

namespace {
  constexpr auto body_limit = size_t{ 32 * 1024 * 1024 };
  constexpr auto proxy_home_page = R"HTML(<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <title>Hamster</title>
</head>
<body>
  <h1>Hamster proxy</h1>
  <p>Use this computer's address and this port as your HTTP and HTTPS proxy.</p>
  <p><a href="/ca.crt" download>Download the CA certificate</a></p>
  <p>Import it into your browser's certificate authorities and trust it to identify websites.</p>
  <p>The private key stays on this computer.</p>
</body>
</html>
)HTML";
  using Http = SimpleWeb::Server<SimpleWeb::HTTP>;
  using Https = SimpleWeb::ServerBase<SimpleWeb::HTTPS>;

  bool browser_ended_tls_handshake(const asio::error_code& error) {
    return error == asio::error::eof ||
      error == asio::error::operation_aborted ||
      error == asio::error::connection_aborted ||
      error == asio::error::connection_reset ||
      error == asio::ssl::error::stream_truncated;
  }

  bool authority_valid(const std::string& authority, bool require_port) {
    static const auto pattern =
      std::regex("^([A-Za-z0-9](?:[A-Za-z0-9.-]{0,251}[A-Za-z0-9])?)(?::([0-9]{1,5}))?$");
    auto match = std::smatch{};
    if (!std::regex_match(authority, match, pattern))
      return false;
    if (!match[2].matched)
      return !require_port;
    const auto port = std::stoi(match[2]);
    return port > 0 && port <= 65535;
  }

  std::string without_default_port(std::string authority, std::string_view port) {
    if (authority.size() >= port.size() &&
      authority.compare(authority.size() - port.size(), port.size(), port) == 0)
      authority.erase(authority.size() - port.size());
    return authority;
  }

  template <class Response, class Request>
  void dispatch(const std::shared_ptr<Response>& response, const std::shared_ptr<Request>& native,
    const std::string& tunnel, const ProxyServer::RequestHandler& route) {
    response->close_connection_after_response = true;
    try {
      auto url = std::string{};
      if (tunnel.empty()) {
        if (!starts_with(native->path, "http://"))
          throw std::runtime_error("Expected absolute HTTP URL");
        auto end = native->path.find('/', 7);
        const auto authority = native->path.substr(7, end == std::string::npos ? end : end - 7);
        if (!authority_valid(authority, false))
          throw std::runtime_error("Invalid target authority");
        url = "http://" + without_default_port(authority, ":80") +
          (end == std::string::npos ? "/" : native->path.substr(end));
      }
      else {
        if (native->path.empty() || native->path.front() != '/' || starts_with(native->path, "//"))
          throw std::runtime_error("Expected origin-form HTTPS request");
        url = "https://" + without_default_port(tunnel, ":443") + native->path;
      }
      if (!native->query_string.empty())
        url += "?" + native->query_string;
      if (url.find('#') != std::string::npos || native->header.count("Upgrade"))
        throw std::runtime_error("Unsupported request target or protocol upgrade");
      auto headers = native->header;
      strip_proxy_headers(headers);
      auto data = ByteVector(native->content.size());
      native->content.read(reinterpret_cast<char*>(data.data()), data.size());
      const auto head = native->method == "HEAD";
      route(ProxyRequest(native->method, native->path, native->query_string, std::move(headers),
        std::move(data),
        [response, head](StatusCode status, const Header& header, ByteView body,
          std::shared_ptr<void> lifetime, std::function<void()> on_sent) {
            auto headers = header;
            strip_proxy_headers(headers);
            headers.emplace("Connection", "close");
            if (head || status == StatusCode::success_no_content ||
              status == StatusCode::redirection_not_modified)
              response->write(status, headers);
            else
              response->write(status, as_string_view(body), headers);
            response->send([lifetime, on_sent](const SimpleWeb::error_code&) {
              if (on_sent)
                on_sent();
              });
        },
        std::move(url)));
    }
    catch (const std::exception& ex) {
      response->write(StatusCode::client_error_bad_request, ex.what(), { {"Connection", "close"} });
    }
  }

  class RawTunnel final : public std::enable_shared_from_this<RawTunnel> {
    asio::ip::tcp::socket m_browser;
    asio::ip::tcp::socket m_origin;
    asio::ip::tcp::resolver m_resolver;
    std::array<char, 32 * 1024> m_to_origin{}, m_to_browser{};
    std::vector<char> m_initial;
    bool m_closed{};

    void close() {
      if (m_closed)
        return;
      m_closed = true;
      auto ec = asio::error_code{};
      m_browser.close(ec);
      m_origin.close(ec);
      m_resolver.cancel();
    }

    void fail(const std::string& authority, const asio::error_code& ec) {
      log(Event::error, "Direct tunnel for ", authority, ": ", ec.message());
      auto self = shared_from_this();
      auto reply = std::make_shared<std::string>(
        "HTTP/1.1 502 Bad Gateway\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
      asio::async_write(m_browser, asio::buffer(*reply),
        [self, reply](const asio::error_code&, size_t) { self->close(); });
    }

    void read_browser() {
      auto self = shared_from_this();
      m_browser.async_read_some(asio::buffer(m_to_origin),
        [self](const asio::error_code& ec, size_t size) {
          if (ec)
            return self->close();
          asio::async_write(self->m_origin, asio::buffer(self->m_to_origin.data(), size),
            [self](const asio::error_code& ec, size_t) {
              if (ec)
                return self->close();
              self->read_browser();
            });
        });
    }

    void read_origin() {
      auto self = shared_from_this();
      m_origin.async_read_some(asio::buffer(m_to_browser),
        [self](const asio::error_code& ec, size_t size) {
          if (ec)
            return self->close();
          asio::async_write(self->m_browser, asio::buffer(self->m_to_browser.data(), size),
            [self](const asio::error_code& ec, size_t) {
              if (ec)
                return self->close();
              self->read_origin();
            });
        });
    }

    void connected() {
      auto self = shared_from_this();
      auto reply = std::make_shared<std::string>("HTTP/1.1 200 Connection Established\r\n\r\n");
      asio::async_write(m_browser, asio::buffer(*reply),
        [self, reply](const asio::error_code& ec, size_t) {
          if (ec)
            return self->close();
          const auto start = [self] {
            self->read_browser();
            self->read_origin();
            };
          if (self->m_initial.empty())
            return start();
          asio::async_write(self->m_origin, asio::buffer(self->m_initial),
            [self, start](const asio::error_code& ec, size_t) {
              if (ec)
                return self->close();
              self->m_initial.clear();
              start();
            });
        });
    }

  public:
    RawTunnel(asio::ip::tcp::socket browser, std::shared_ptr<asio::streambuf> remaining,
      asio::io_context& context)
      : m_browser(std::move(browser)), m_origin(context), m_resolver(context) {
      m_initial.assign(asio::buffers_begin(remaining->data()), asio::buffers_end(remaining->data()));
    }

    void stop() { close(); }

    void start(const std::string& authority) {
      const auto colon = authority.rfind(':');
      const auto host = authority.substr(0, colon);
      const auto port = authority.substr(colon + 1);
      auto self = shared_from_this();
      SimpleWeb::async_resolve(m_resolver, { host, port },
        [self, authority](const asio::error_code& ec, SimpleWeb::resolver_results results) {
          if (ec)
            return self->fail(authority, ec);
          asio::async_connect(self->m_origin, results,
            [self, authority](const asio::error_code& ec, SimpleWeb::async_connect_endpoint) {
              if (ec)
                return self->fail(authority, ec);
              self->connected();
            });
        });
    }
  };

  class TlsListener : public Https {
    CertificateAuthority m_ca;
    asio::ssl::context m_initial{ asio::ssl::context::tls_server };

    struct Tunnel {
      std::weak_ptr<Connection> connection;
      std::string authority;
    };

    std::map<asio::ip::tcp::endpoint, Tunnel> m_tunnels;

    void accept() override {}

  public:
    TlsListener(const Settings& settings, ProxyServer::RequestHandler route)
      : Https(0), m_ca(settings.ca_directory) {
      io_service = proxy_io_context();
      config.max_request_streambuf_size = body_limit;
      SSL_CTX_set_min_proto_version(m_initial.native_handle(), TLS1_2_VERSION);
      for (auto method : { "GET", "HEAD", "POST", "PUT", "DELETE", "PATCH", "OPTIONS" })
        default_resource[method] = [this, route](std::shared_ptr<Response> response,
          std::shared_ptr<Request> request) {
            dispatch(response, request, m_tunnels.at(request->remote_endpoint()).authority, route);
        };
      on_error = [](std::shared_ptr<Request>, const SimpleWeb::error_code& ec) {
        if (ec != asio::error::eof && ec != asio::error::operation_aborted)
          log(Event::error, ec.message());
        };
    }

    std::string public_certificate_pem() const {
      return m_ca.public_certificate_pem();
    }

    void adopt(asio::ip::tcp::socket socket, const std::string& authority,
      std::shared_ptr<asio::streambuf> remaining) {
      auto connection = create_connection(*io_service, m_initial);
      connection->socket->next_layer() = std::move(socket);
      auto context = m_ca.context(authority.substr(0, authority.rfind(':')));
      if (!SSL_set_SSL_CTX(connection->socket->native_handle(), context->native_handle()))
        throw std::runtime_error("Cannot select interception certificate");
      for (auto it = m_tunnels.begin(); it != m_tunnels.end();)
        if (it->second.connection.expired())
          it = m_tunnels.erase(it);
        else
          ++it;
      m_tunnels[connection->socket->lowest_layer().remote_endpoint()] = { connection, authority };
      connection->set_timeout(10);
      auto reply = std::make_shared<std::string>("HTTP/1.1 200 Connection Established\r\n\r\n");
      asio::async_write(connection->socket->next_layer(), asio::buffer(*reply),
        [this, connection, context, reply, remaining, authority](
          const asio::error_code& ec, size_t) {
            if (ec)
              return;
            connection->socket->async_handshake(asio::ssl::stream_base::server, remaining->data(),
              [this, connection, context, remaining, authority](const asio::error_code& ec, size_t) {
                connection->cancel_timeout();
                if (!ec)
                  read(std::make_shared<Session>(config.max_request_streambuf_size, connection));
                else if (browser_ended_tls_handshake(ec))
                  log(Event::info, "Browser ended TLS handshake for ", authority);
                else
                  log(Event::error, "Browser TLS handshake for ", authority,
                    " failed: ", ec.message());
              });
        });
    }
  };
} // namespace

class ProxyServer::Listener final : public Http {
  TlsListener m_tls;
  ProxyServer::ShouldIntercept m_should_intercept;
  std::vector<std::pair<std::string, std::weak_ptr<RawTunnel>>> m_raw_tunnels;

  void accept() override {
    auto connection = create_connection(*io_service);
    acceptor->async_accept(*connection->socket, [this, connection](const asio::error_code& ec) {
      if (ec != asio::error::operation_aborted)
        accept();
      if (ec)
        return;
      connection->set_timeout(10);
      auto buffer =
        std::make_shared<std::unique_ptr<asio::streambuf>>(new asio::streambuf(body_limit));
      asio::async_read_until(*connection->socket, **buffer, "\r\n\r\n",
        [this, connection, buffer](const asio::error_code& ec, size_t bytes) {
          if (ec)
            return;
          try {
            if (bytes > 16384)
              throw std::runtime_error("Proxy header too large");
            auto header = std::string(asio::buffers_begin((*buffer)->data()),
              asio::buffers_begin((*buffer)->data()) + bytes);
            auto in = std::istringstream(header);
            auto method = std::string{};
            auto authority = std::string{};
            auto query = std::string{};
            auto version = std::string{};
            auto headers = Header{};
            if (!SimpleWeb::RequestMessage::parse(in, method, authority, query, version, headers))
              throw std::runtime_error("Malformed proxy header");
            connection->cancel_timeout();
            if (method != "CONNECT") {
              read(std::make_shared<Session>(
                config.max_request_streambuf_size, connection, std::move(*buffer)));
              return;
            }
            if (!authority_valid(authority, true) || !query.empty() ||
              (version != "1.0" && version != "1.1") || headers.count("Transfer-Encoding") ||
              (headers.count("Content-Length") && headers.find("Content-Length")->second != "0"))
              throw std::runtime_error("Invalid CONNECT authority or framing");
            (*buffer)->consume(bytes);
            auto remaining = std::shared_ptr<asio::streambuf>(std::move(*buffer));
            if (!m_should_intercept(authority)) {
              auto tunnel = std::make_shared<RawTunnel>(
                std::move(*connection->socket), remaining, *io_service);
              m_raw_tunnels.emplace_back(authority, tunnel);
              tunnel->start(authority);
              return;
            }
            m_tls.adopt(std::move(*connection->socket), authority, remaining);
          }
          catch (const std::exception& ex) {
            log(Event::error, ex.what());
            auto reply = std::make_shared<std::string>(
              "HTTP/1.1 400 Bad Request\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
            asio::async_write(*connection->socket, asio::buffer(*reply),
              [connection, reply](const asio::error_code&, size_t) { connection->close(); });
          }
        });
      });
  }

public:
  Listener(const Settings& settings, ProxyServer::RequestHandler route,
    ProxyServer::ControlHandler control, ProxyServer::ShouldIntercept should_intercept)
    : m_tls(settings, route), m_should_intercept(std::move(should_intercept)) {
    io_service = proxy_io_context();
    config.address = settings.listen_address;
    config.port = static_cast<unsigned short>(settings.port);
    config.max_request_streambuf_size = body_limit;
    const auto public_certificate = m_tls.public_certificate_pem();
    for (auto method : { "GET", "HEAD", "POST", "PUT", "DELETE", "PATCH", "OPTIONS" })
      default_resource[method] = [route, control, public_certificate](
        std::shared_ptr<Response> response,
        std::shared_ptr<Request> request) {
          if (!request->path.empty() && request->path.front() == '/') {
            response->close_connection_after_response = true;
            if (control && request->method == "POST") {
              const auto result = control(request->path, request->content.string());
              if (result.body.empty())
                response->write(result.status, result.headers);
              else
                response->write(result.status, result.body, result.headers);
              return;
            }
            if (request->method != "GET" && request->method != "HEAD") {
              response->write(StatusCode::client_error_method_not_allowed);
              return;
            }
            auto body = std::string{};
            auto headers = Header{ {"Cache-Control", "no-store"},
              {"X-Content-Type-Options", "nosniff"} };
            if (request->path == "/ca.crt") {
              body = public_certificate;
              headers.emplace("Content-Type", "application/x-x509-ca-cert");
              headers.emplace("Content-Disposition", "attachment; filename=hamster-ca.crt");
            }
            else if (request->path == "/") {
              body = proxy_home_page;
              headers.emplace("Content-Type", "text/html; charset=utf-8");
              headers.emplace(
                "Content-Security-Policy", "default-src 'none'; frame-ancestors 'none'");
            }
            else {
              response->write(StatusCode::client_error_not_found);
              return;
            }
            headers.emplace("Content-Length", std::to_string(body.size()));
            if (request->method == "HEAD")
              response->write(StatusCode::success_ok, headers);
            else
              response->write(StatusCode::success_ok, body, headers);
            return;
          }
          dispatch(response, request, "", route);
      };
  }

  void refresh_tunnels() {
    for (auto it = m_raw_tunnels.begin(); it != m_raw_tunnels.end();) {
      const auto tunnel = it->second.lock();
      if (!tunnel) {
        it = m_raw_tunnels.erase(it);
        continue;
      }
      if (m_should_intercept(it->first)) {
        tunnel->stop();
        it = m_raw_tunnels.erase(it);
        continue;
      }
      ++it;
    }
  }
};

//-------------------------------------------------------------------------

ProxyServer::ProxyServer(const Settings& settings, RequestHandler request_handler,
  ControlHandler control_handler, ShouldIntercept should_intercept)
  : m_listener(std::make_unique<Listener>(settings, std::move(request_handler),
    std::move(control_handler), std::move(should_intercept))) {}

ProxyServer::~ProxyServer() = default;

void ProxyServer::start(std::function<void(unsigned short)> on_started) {
  m_listener->start(std::move(on_started));
}

void ProxyServer::refresh_tunnels() {
  m_listener->refresh_tunnels();
}
