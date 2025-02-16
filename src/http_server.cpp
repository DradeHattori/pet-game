#include "http_server.h"
#include "logger.h"

namespace http_server 
{
    void ReportError(beast::error_code ec_, std::string_view what_) {
        using namespace std::literals;
        LogError(ec_, what_);
    }

    void SessionBase::Run() {
        net::dispatch(stream.get_executor(), beast::bind_front_handler(&SessionBase::Read, GetSharedThis()));
    }

    void SessionBase::Close() {
        stream.socket().shutdown(tcp::socket::shutdown_send);
    }

    void SessionBase::OnRead(beast::error_code ec_, [[maybe_unused]] std::size_t bytes_read_) {
        using namespace std::literals;

        if (ec_ == http::error::end_of_stream)
        {
            return Close();
        }
        if (ec_)
        {
            return ReportError(ec_, "read"sv);
        }

        HandleRequest(std::move(request));
    }

    void SessionBase::Read() {
        using namespace std::literals;

        request = {};
        stream.expires_after(30s);

        http::async_read(stream, buffer, request, beast::bind_front_handler(&SessionBase::OnRead, GetSharedThis()));
    }

    void SessionBase::OnWrite(bool close_, beast::error_code ec_, [[maybe_unused]] std::size_t bytes_written_) {
        using namespace std::literals;
        if (ec_) {
            return ReportError(ec_, "write"sv);
        }
        if (close_) {
            return Close();
        }
        Read();
    }



}  // end namespace http_server