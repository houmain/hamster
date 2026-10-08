#pragma once

#include "ControlApi.h"
#include "ProxyRequest.h"
#include <functional>
#include <memory>
#include <string>

struct Settings;

class ProxyServer final {
public:
  using RequestHandler = std::function<void(ProxyRequest)>;
  using ControlHandler = std::function<ControlResponse(const std::string&, const std::string&)>;
  using ShouldIntercept = std::function<bool(const std::string&)>;

  ProxyServer(const Settings& settings, RequestHandler request_handler,
    ControlHandler control_handler, ShouldIntercept should_intercept);
  ~ProxyServer();

  ProxyServer(const ProxyServer&) = delete;
  ProxyServer& operator=(const ProxyServer&) = delete;

  void start(std::function<void(unsigned short)> on_started);
  void refresh_tunnels();

private:
  class Listener;
  std::unique_ptr<Listener> m_listener;
};
