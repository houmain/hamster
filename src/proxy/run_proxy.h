#pragma once

#include <memory>

struct Settings;
namespace asio { class io_context; }

int run_proxy(const Settings& settings);
void request_proxy_stop();

std::shared_ptr<asio::io_context> proxy_io_context();
