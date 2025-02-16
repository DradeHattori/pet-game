#include "logger.h"
#include <boost/log/core.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/utility/setup/file.hpp>
#include <boost/log/utility/setup/console.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <boost/log/utility/manipulators/add_value.hpp>
#include <boost/log/attributes/scoped_attribute.hpp>
#include <boost/log/support/date_time.hpp>
#include <boost/json.hpp>
#include <boost/beast/core.hpp>
#include <iostream>


using namespace std::literals;
namespace logging = boost::log;
namespace sinks = boost::log::sinks;
namespace keywords = boost::log::keywords;
namespace expr = boost::log::expressions;
namespace attrs = boost::log::attributes;

BOOST_LOG_ATTRIBUTE_KEYWORD(additional_data, "AdditionalData", json::value);

void InitLogging()
{
    logging::add_common_attributes();
    logging::core::get()->add_global_attribute("TimeStamp", boost::log::attributes::local_clock());

    auto log_formatter = [](logging::record_view const& rec, logging::formatting_ostream& strm) {
        json::object log_entry;

        // TimeStamp
        auto ts = rec.attribute_values()["TimeStamp"].extract<boost::posix_time::ptime>();
        if (ts) {
            std::ostringstream timestamp;
            timestamp << *ts;
            log_entry["timestamp"] = timestamp.str();
        }

        // AdditionalData
        auto additional = rec.attribute_values()[additional_data.get_name()].extract<json::value>();
        if (additional) {
            log_entry["data"] = *additional;
        }
        else {
            log_entry["data"] = nullptr;
        }

        // Message
        auto message = rec.attribute_values()["Message"].extract<std::string>();
        if (message) {
            log_entry["message"] = *message;
        }

        strm << json::serialize(log_entry) << std::endl;
        };

    logging::add_console_log(std::cout, keywords::format = log_formatter, logging::keywords::auto_flush = true);
}


void LogServerStarted(int port, const std::string& address) {
    json::object log_data;
    log_data["port"] = port;
    log_data["address"] = address;

    // Проверка, что данные логируются как JSON
    BOOST_LOG_TRIVIAL(info) << logging::add_value(additional_data, log_data) << "server started";

}

void LogServerStopped(int signal, const std::optional<std::string>& exception) {
    json::object log_data;
    log_data["code"] = signal;

    if (exception) {
        log_data["exception"] = *exception;
    }
    BOOST_LOG_TRIVIAL(info) << logging::add_value(additional_data, log_data)
        << "server exited";
}

void LogRequestReceived(const std::string& url, const std::string& method, const std::string& ip) {
    json::object log_data;
    log_data["ip"] = ip;                                  // IP клиента
    log_data["URI"] = url;          // Преобразование URI в строку
    log_data["method"] = method; // Метод HTTP-запроса (GET, POST и т.д.)

    BOOST_LOG_TRIVIAL(info) << logging::add_value(additional_data, log_data)
        << "request received";
}

void LogParamInfo(const std::string& param_name, const std::string& message) {
    json::object log_data;
    log_data["param"] = param_name;
    log_data["info"] = message;
    BOOST_LOG_TRIVIAL(info) << log_data;
}

void LogEventInfo(const std::string& event, const std::string& message) {
    json::object log_data;
    log_data["event"] = event;
    log_data["info"] = message;
    BOOST_LOG_TRIVIAL(info) << log_data;
}

// Логирование ответа
void LogRequestSent(const std::string& ip, int response_time, int code, const std::string& content_type) {
    json::object log_data;
    log_data["ip"] = ip;
    log_data["response_time"] = response_time;
    log_data["code"] = code;
    log_data["content_type"] = content_type;

    BOOST_LOG_TRIVIAL(info) << logging::add_value(additional_data, log_data)
        << "response sent";
}

void LogError(const boost::beast::error_code& ec, const std::string_view where) {
    json::object log_data;
    log_data["code"] = ec.value();          
    log_data["text"] = ec.message();        
    log_data["where"] = std::string(where);


    BOOST_LOG_TRIVIAL(info) << logging::add_value(additional_data, log_data)
        << "error";
}

void LogError(const std::exception& ex, const std::string_view where) {
    json::object log_data;
    log_data["type"] = "exception";          
    log_data["message"] = ex.what();         
    log_data["where"] = std::string(where);  

    BOOST_LOG_TRIVIAL(error) << logging::add_value(additional_data, log_data)
        << "exception occurred";
}

