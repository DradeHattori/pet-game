﻿#pragma once

#include <mutex>
#include <vector>
#include <condition_variable>
#include <string>

#include <pqxx/pqxx>
#include <pqxx/connection>
#include <pqxx/transaction>
#include <pqxx/zview.hxx>


using namespace std::literals;
using pqxx::operator"" _zv;

struct PlayerRecord {
    std::string name;
    int score;
    double playTime;
};

namespace postgres {
    class ConnectionPool {
        using PoolType = ConnectionPool;
        using ConnectionPtr = std::shared_ptr<pqxx::connection>;

    public:
        class ConnectionWrapper {
        public:
            ConnectionWrapper(std::shared_ptr<pqxx::connection>&& conn, PoolType& pool) noexcept
                : conn_{ std::move(conn) }
                , pool_{ &pool } {
            }

            ConnectionWrapper(const ConnectionWrapper&) = delete;
            ConnectionWrapper& operator=(const ConnectionWrapper&) = delete;

            ConnectionWrapper(ConnectionWrapper&&) = default;
            ConnectionWrapper& operator=(ConnectionWrapper&&) = default;

            pqxx::connection& operator*() const& noexcept {
                return *conn_;
            }
            pqxx::connection& operator*() const&& = delete;

            pqxx::connection* operator->() const& noexcept {
                return conn_.get();
            }

            ~ConnectionWrapper() {
                if (conn_) {
                    pool_->ReturnConnection(std::move(conn_));
                }
            }

        private:
            std::shared_ptr<pqxx::connection> conn_;
            PoolType* pool_;
        };

        // ConnectionFactory is a functional object returning std::shared_ptr<pqxx::connection>
        template <typename ConnectionFactory>
        ConnectionPool(size_t capacity, ConnectionFactory&& connection_factory) {
            pool_.reserve(capacity);
            for (size_t i = 0; i < capacity; ++i) {
                pool_.emplace_back(connection_factory());
            }
        }

        ConnectionWrapper GetConnection() {
            std::unique_lock lock{ mutex_ };
            cond_var_.wait(lock, [this] {
                return used_connections_ < pool_.size();
                });

            if (used_connections_ >= pool_.size()) {
                throw std::runtime_error("No available connections in the pool");
            }

            return { std::move(pool_[used_connections_++]), *this };
        }
    private:
        void ReturnConnection(ConnectionPtr&& conn) {
            if (!conn) {
                throw std::invalid_argument("Invalid connection");
            }

            {
                std::lock_guard lock{ mutex_ };
                assert(used_connections_ != 0);
                pool_[--used_connections_] = std::move(conn);
            }

            cond_var_.notify_one();
        }

        std::mutex mutex_;
        std::condition_variable cond_var_;
        std::vector<ConnectionPtr> pool_;
        size_t used_connections_ = 0;
    };

    class Database {
    public:
        static void Init(std::shared_ptr<ConnectionPool> pool);
        static std::vector< PlayerRecord > GetPlayersRecords(std::shared_ptr<ConnectionPool> pool, int start, int maxItems);
        static void SaveRecord(std::shared_ptr<ConnectionPool> pool, PlayerRecord record);
    };
}///namespace postgres

using ConnectionPoolPtr = std::shared_ptr< postgres::ConnectionPool >;