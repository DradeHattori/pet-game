#pragma once
#include "sdk.h"

#define BOOST_BEAST_USE_STD_STRING_VIEW

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include <iostream>

namespace http_server
{
    namespace net = boost::asio;
    using tcp = net::ip::tcp;
    namespace beast = boost::beast;
    namespace http = beast::http;

    void ReportError(beast::error_code ec_, std::string_view what_);

    class SessionBase {
    public:
        void Run();
        SessionBase(const SessionBase&) = delete;
        SessionBase& operator=(const SessionBase&) = delete;

    protected:
        using HttpRequest = http::request<http::string_body>;

        explicit SessionBase(tcp::socket&& socket_) : stream(std::move(socket_)) {}

        template <typename Body, typename Fields>
        void Write(http::response<Body, Fields>&& response_) {
            auto safe_response = std::make_shared<http::response<Body, Fields>>(std::move(response_));

            auto self = GetSharedThis();
            http::async_write(stream, *safe_response, [safe_response, self](beast::error_code ec, std::size_t bytes_written) {
                self->OnWrite(safe_response->need_eof(), ec, bytes_written);
                });
        }

        tcp::socket& GetSocketFromStream() {
            return stream.socket();
        }

        ~SessionBase() = default;

    private:
        void Close();

        virtual void HandleRequest(HttpRequest&& request_) = 0;
        void OnRead(beast::error_code ec_, [[maybe_unused]] std::size_t bytes_read_);
        void Read();
        void OnWrite(bool close_, beast::error_code ec_, [[maybe_unused]] std::size_t bytes_written_);

        virtual std::shared_ptr<SessionBase> GetSharedThis() = 0;

        beast::flat_buffer buffer;
        HttpRequest request;
        beast::tcp_stream stream;
    };

    template <typename RequestHandler>
    class Session : public SessionBase, public std::enable_shared_from_this<Session<RequestHandler>> {
    public:
        template <typename Handler>
        Session(tcp::socket&& socket_, Handler&& request_handler_)
            : SessionBase(std::move(socket_)), request_handler(std::forward<Handler>(request_handler_)) {}

    private:
        void HandleRequest(HttpRequest&& request_) override {
            // Вызываем request_handler с callback, который отправит ответ
            request_handler(std::move(request_), [self = this->shared_from_this()](auto&& response) {
                self->Write(std::move(response));
                }, this->GetSocketFromStream());
        }

        std::shared_ptr<SessionBase> GetSharedThis() override {
            return this->shared_from_this();
        }

        RequestHandler request_handler;
    };

    template <typename RequestHandler>
    class Listener : public std::enable_shared_from_this<Listener<RequestHandler>> {
    public:
        Listener(net::io_context& ioc_, tcp::endpoint endpoint_, RequestHandler handler_)
            : ioc(ioc_), acceptor(net::make_strand(ioc_)), request_handler(std::move(handler_)) {
            acceptor.open(endpoint_.protocol());
            acceptor.set_option(net::socket_base::reuse_address(true));
            acceptor.bind(endpoint_);
            acceptor.listen(net::socket_base::max_listen_connections);
        }

        void Run() {
            DoAccept();
        }

    private:
        void DoAccept() {
            acceptor.async_accept(net::make_strand(ioc), beast::bind_front_handler(&Listener::OnAccept, this->shared_from_this()));
        }

        void OnAccept(beast::error_code ec_, tcp::socket socket_) {
            using namespace std::literals;

            if (ec_) {
                ReportError(ec_, "accept"sv);
            }
            else {
                std::make_shared<Session<RequestHandler>>(std::move(socket_), request_handler)->Run();
            }
            DoAccept();
        }

        net::io_context& ioc;
        tcp::acceptor acceptor;
        RequestHandler request_handler;
    };

    template <typename RequestHandler>
    void ServeHttp(net::io_context& ioc_, const tcp::endpoint& endpoint_, RequestHandler&& handler_) {
        using MyListener = Listener<std::decay_t<RequestHandler>>;
        std::make_shared<MyListener>(ioc_, endpoint_, std::forward<RequestHandler>(handler_))->Run();
    }

}  // namespace http_server