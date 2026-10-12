#include "run_proxy.h"
#include "ControlApi.h"
#include "ProxyServer.h"
#include "Settings.h"
#include "catalog/Catalog.h"
#include "library/Library.h"
#include "recording/RecordingCoordinator.h"
#include "platform.h"
#include <asio/post.hpp>
#include <asio/signal_set.hpp>
#include <asio/steady_timer.hpp>
#include <csignal>
#include <filesystem>
#include <functional>
#include <mutex>

namespace {
  std::mutex g_stop_mutex;
  std::function<void()> g_stop_function;

  std::function<void()> reset_stop_function(std::function<void()> function = { }) {
    const auto lock = std::lock_guard(g_stop_mutex);
    return std::exchange(g_stop_function, std::move(function));
  }
} // namespace

std::shared_ptr<asio::io_context> proxy_io_context() {
  static auto g_io_context = std::make_shared<asio::io_context>();
  return g_io_context;
}

int run_proxy(const Settings& settings) {
  std::filesystem::create_directories(settings.library_root);
  std::filesystem::create_directories(settings.ca_directory);

  const auto context = proxy_io_context();
  auto library = Library(settings.library_root);
  auto catalog = Catalog(settings.catalog_database, library);
  auto recordings = RecordingCoordinator(settings, library, catalog);
  auto control = ControlApi(library, catalog, recordings);
  auto server = ProxyServer(settings,
    [&recordings](ProxyRequest request) {
      recordings.handle_request(std::move(request));
    },
    [&control](const std::string& path, const std::string& content) {
      return control.handle(path, content);
    },
    [&recordings](const std::string& authority) {
      return recordings.should_intercept(authority);
    });
  auto failure = std::exception_ptr{};
  auto drain = std::function<void()>{};
  auto begin_stop = std::function<void()>{};
  auto expire = std::function<void()>{};
  auto drain_timer = asio::steady_timer(*context);
  auto lease_timer = asio::steady_timer(*context);
  auto signals = asio::signal_set(*context, SIGINT, SIGTERM);

#ifdef SIGBREAK
  signals.add(SIGBREAK);
#endif

  recordings.set_interception_changed([&server] { server.refresh_tunnels(); });

  drain = [&] {
    auto complete = false;
    try {
      complete = recordings.drain();
    }
    catch (...) {
      failure = std::current_exception();
    }
    if (complete) {
      context->stop();
      return;
    }
    drain_timer.expires_after(std::chrono::milliseconds(10));
    drain_timer.async_wait([&drain](const asio::error_code& ec) {
      if (!ec)
        drain();
      });
    };

  begin_stop = [&] {
    asio::post(*context, [&recordings, &drain] {
      if (!recordings.stopping()) {
        recordings.begin_stop();
        drain();
      }
      });
    };

  expire = [&] {
    recordings.expire();
    lease_timer.expires_after(std::chrono::seconds(5));
    lease_timer.async_wait([&](const asio::error_code& ec) {
      if (!ec && !recordings.stopping())
        expire();
      });
    };

  context->restart();
  reset_stop_function(begin_stop);
  try {
    expire();
    signals.async_wait([&begin_stop](const asio::error_code& ec, int) {
      if (!ec)
        begin_stop();
      });
    server.start([&control, &settings](unsigned short port) {
      control.set_proxy_port(port);
      log(Event::accept, "http://", settings.listen_address, ":", port);
      });
    context->run();
  }
  catch (...) {
    reset_stop_function();
    throw;
  }
  reset_stop_function();
  if (failure)
    std::rethrow_exception(failure);
  return 0;
}

void request_proxy_stop() {
  if (auto stop = reset_stop_function())
    stop();
}
