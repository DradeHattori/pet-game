#pragma once

#include <boost/log/core.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/utility/setup/console.hpp>
#include <boost/json.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/utility/manipulators/add_value.hpp>
#include <boost/beast/core.hpp>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <optional>

namespace logging = boost::log;
namespace json = boost::json;
namespace expr = boost::log::expressions;


void InitLogging();
// Функция для получения текущего времени в формате ISO 8601

// Функция для логирования начала работы сервера
void LogServerStarted(int port, const std::string& address);

// Логирование остановки сервера
void LogServerStopped(int signal, const std::optional<std::string>& exception = std::nullopt);

// Логирование получения запроса
void LogRequestReceived(const std::string& url, const std::string& method, const std::string& ip);

// Логгирование заданных параметров при запуске
void LogParamInfo(const std::string& param_name, const std::string& message);

// Логгирование доп. информации
void LogEventInfo(const std::string& event, const std::string& message);

// Логирование формирования ответа
void LogRequestSent(const std::string& ip, int response_time, int code, const std::string& content_type);

// Логирование ошибки
void LogError(const boost::beast::error_code& ec, const std::string_view where);
void LogError(const std::exception& ex, const std::string_view where);

