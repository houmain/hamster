#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <type_traits>
#include <utility>

class BackgroundWorker {
public:
  BackgroundWorker() : m_thread([this] { work(); }) {}

  ~BackgroundWorker() {
    {
      auto lock = std::lock_guard(m_mutex);
      m_stop = true;
    }
    m_signal.notify_one();
    m_thread.join();
  }

  template <typename F>
  void execute(F&& task) {
    using Task = std::decay_t<F>;
    static_assert(std::is_nothrow_invocable_v<Task&>,
      "BackgroundWorker tasks must be noexcept");
    {
      auto lock = std::lock_guard(m_mutex);
      m_queue.emplace_back(std::forward<F>(task));
      ++m_pending;
    }
    m_signal.notify_one();
  }

  bool busy() const { return m_pending != 0; }

private:
  void work() noexcept {
    for (;;) {
      auto lock = std::unique_lock(m_mutex);
      m_signal.wait(lock, [this] { return m_stop || !m_queue.empty(); });
      if (m_queue.empty())
        return;
      auto task = std::move(m_queue.front());
      m_queue.pop_front();
      lock.unlock();
      task();
      --m_pending;
    }
  }

  std::mutex m_mutex;
  std::condition_variable m_signal;
  std::deque<std::function<void()>> m_queue;
  bool m_stop{};
  std::atomic<size_t> m_pending{};
  std::thread m_thread;
};
