#pragma once

#include <pqxx/pqxx>

#include <chrono>
#include <cstddef>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

class DatabasePool;

class DatabasePoolExhausted : public std::runtime_error {
 public:
  DatabasePoolExhausted() : std::runtime_error("database connection pool exhausted") {}
};

struct DatabasePoolStats {
  std::size_t maximum = 0;
  std::size_t open = 0;
  std::size_t idle = 0;
  std::size_t in_use = 0;
  std::size_t waiting = 0;
};

class PooledConnection {
 public:
  PooledConnection() = default;
  PooledConnection(const PooledConnection&) = delete;
  PooledConnection& operator=(const PooledConnection&) = delete;

  PooledConnection(PooledConnection&& other) noexcept
      : pool_(other.pool_), connection_(std::move(other.connection_)) {
    other.pool_ = nullptr;
  }

  PooledConnection& operator=(PooledConnection&& other) noexcept;
  ~PooledConnection();

  pqxx::connection& get() const { return *connection_; }

 private:
  friend class DatabasePool;
  PooledConnection(DatabasePool* pool, std::unique_ptr<pqxx::connection> connection)
      : pool_(pool), connection_(std::move(connection)) {}

  void release() noexcept;

  DatabasePool* pool_ = nullptr;
  std::unique_ptr<pqxx::connection> connection_;
};

class DatabasePool {
 public:
  DatabasePool(std::string database_url, std::size_t maximum,
               std::chrono::milliseconds wait_timeout)
      : database_url_(std::move(database_url)), maximum_(maximum),
        wait_timeout_(wait_timeout) {
    if (maximum_ == 0) throw std::invalid_argument("database pool size must be positive");
  }

  DatabasePool(const DatabasePool&) = delete;
  DatabasePool& operator=(const DatabasePool&) = delete;

  PooledConnection acquire() {
    const auto deadline = std::chrono::steady_clock::now() + wait_timeout_;
    while (true) {
      std::unique_ptr<pqxx::connection> connection;
      bool create_connection = false;
      {
        std::unique_lock<std::mutex> lock(mutex_);
        while (idle_.empty() && open_ >= maximum_) {
          ++waiting_;
          const bool available = condition_.wait_until(lock, deadline, [this] {
            return !idle_.empty() || open_ < maximum_;
          });
          --waiting_;
          if (!available) throw DatabasePoolExhausted();
        }
        if (!idle_.empty()) {
          connection = std::move(idle_.back());
          idle_.pop_back();
          if (connection && connection->is_open()) {
            return PooledConnection(this, std::move(connection));
          }
          if (open_ > 0) --open_;
          condition_.notify_one();
          continue;
        }
        ++open_;
        create_connection = true;
      }
      if (create_connection) {
        try {
          connection = std::make_unique<pqxx::connection>(database_url_);
          return PooledConnection(this, std::move(connection));
        } catch (...) {
          std::lock_guard<std::mutex> lock(mutex_);
          if (open_ > 0) --open_;
          condition_.notify_one();
          throw;
        }
      }
    }
  }

  DatabasePoolStats stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return {maximum_, open_, idle_.size(), open_ - idle_.size(), waiting_};
  }

 private:
  friend class PooledConnection;

  void release(std::unique_ptr<pqxx::connection> connection) noexcept {
    bool usable = connection && connection->is_open();
    if (usable) {
      /* 归还前必须把残留的打开事务清掉。
         异常路径跳出的 pqxx::work **不会自动回滚**（libpqxx 7.10 的 work 析构
         不发 ROLLBACK）：写事务中途抛异常（校验失败、排序号冲突……）时，连接
         会带着打开的事务与行锁回池——后续对同一行的 FOR UPDATE 将永久等锁。
         已在隔离库实测复现：一次 400 的 PUT 之后，合法 PUT 卡死 20 秒以上，
         pg_stat_activity 显示一个 idle in transaction 持锁、一个 Lock 等待。
         connection::exec 是 private 的，所以用公开的 nontransaction 发 ROLLBACK：
         它不发 BEGIN；连接本就 idle 时 ROLLBACK 只是返回一条 no-in-progress
         警告结果，无副作用。 */
      try {
        pqxx::nontransaction cleanup(*connection);
        cleanup.exec("ROLLBACK");
        cleanup.commit(); /* no-op；显式调用避免析构再走一次 abort 路径 */
      } catch (...) {
        /* 连接状态未知：丢弃而不是复用，避免把坏连接发给下一个请求。 */
        usable = false;
      }
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (usable) {
      idle_.push_back(std::move(connection));
    } else if (open_ > 0) {
      --open_;
    }
    condition_.notify_one();
  }

  std::string database_url_;
  std::size_t maximum_;
  std::chrono::milliseconds wait_timeout_;
  mutable std::mutex mutex_;
  std::condition_variable condition_;
  std::vector<std::unique_ptr<pqxx::connection>> idle_;
  std::size_t open_ = 0;
  std::size_t waiting_ = 0;
};

inline void PooledConnection::release() noexcept {
  if (pool_ != nullptr && connection_) pool_->release(std::move(connection_));
  pool_ = nullptr;
}

inline PooledConnection& PooledConnection::operator=(PooledConnection&& other) noexcept {
  if (this == &other) return *this;
  release();
  pool_ = other.pool_;
  connection_ = std::move(other.connection_);
  other.pool_ = nullptr;
  return *this;
}

inline PooledConnection::~PooledConnection() { release(); }
