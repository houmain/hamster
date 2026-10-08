#include "proxy/run_proxy.h"
#include "Settings.h"
#include "platform.h"
#include "native_host.h"

extern void tests();

int run(int argc, const char* argv[]) noexcept try {

  if (is_native_invocation(argc, argv))
    return run_native_host(argc, argv);

#ifndef NDEBUG
  if (argc == 2 && std::string_view(argv[1]) == "--self-test") {
    tests();
    return 0;
  }
  tests();
#endif

  auto settings = Settings{};
  if (!interpret_commandline(settings, argc, argv)) {
    print_help_message(argv[0]);
    return 1;
  }
  auto instance = ProxyInstanceLock(settings.port);
  if (!instance.try_lock()) {
    log(Event::fatal, "Another Hamster proxy is already using port ", settings.port);
    return 1;
  }
  return run_proxy(settings);
} 
catch (const std::exception& ex) {
  log(Event::fatal, ex.what());
  return 1;
}
