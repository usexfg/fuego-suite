#include "atomicdex/api/fuego/fuego_ws_client.hpp"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>

namespace beast     = boost::beast;
namespace websocket = beast::websocket;
namespace net       = boost::asio;
using tcp           = net::ip::tcp;

namespace atomic_dex::fuego
{
    fuego_ws_client::fuego_ws_client(fuego_ws_config cfg)
        : m_url(std::move(cfg.url))
        , m_on_block(std::move(cfg.on_block))
        , m_on_swap_update(std::move(cfg.on_swap_update))
        , m_on_order_fill(std::move(cfg.on_order_fill))
    {
    }

    fuego_ws_client::~fuego_ws_client()
    {
        stop();
    }

    void
    fuego_ws_client::connect()
    {
        try
        {
            auto resolver   = std::make_shared<tcp::resolver>(net::io_context());
            auto ws         = std::make_unique<websocket::stream<beast::tcp_stream>>(net::io_context());

            // Parse URL
            auto host_start = m_url.find("://") + 3;
            auto path_start = m_url.find('/', host_start);
            std::string host = m_url.substr(host_start, path_start - host_start);
            std::string port = "7784";
            std::string path = "/ws";

            auto colon_pos = host.find(':');
            if (colon_pos != std::string::npos)
            {
                port = host.substr(colon_pos + 1);
                host = host.substr(0, colon_pos);
            }
            if (path_start != std::string::npos)
                path = m_url.substr(path_start);

            auto const results = resolver->resolve(host, port);
            auto& beast_stream = beast::get_lowest_layer(*ws);
            beast_stream.connect(results);
            beast_stream.expires_after(std::chrono::seconds(30));

            ws->handshake(host + ":" + port, path);
            m_ws = std::move(ws);
            SPDLOG_INFO("fuego WebSocket connected to {}", m_url);
        }
        catch (const std::exception& ex)
        {
            SPDLOG_ERROR("fuego WebSocket connect failed: {}", ex.what());
            m_ws.reset();
        }
    }

    void
    fuego_ws_client::dispatch_message(const std::string& msg)
    {
        try
        {
            auto j = nlohmann::json::parse(msg);
            auto type = j.value("type", std::string{});

            if (type == "block" && m_on_block)
            {
                m_on_block(
                    j.at("height").get<int64_t>(),
                    j.value("coin", "XFG"),
                    j.value("hash", ""));
            }
            else if (type == "swap_update" && m_on_swap_update)
            {
                m_on_swap_update(
                    j.at("uuid").get<std::string>(),
                    j.at("status").get<std::string>(),
                    j.contains("details") ? j.at("details") : nlohmann::json::object());
            }
            else if (type == "order_fill" && m_on_order_fill)
            {
                m_on_order_fill(
                    j.at("action").get<std::string>(),
                    j.contains("order") ? j.at("order") : nlohmann::json::object());
            }
        }
        catch (const std::exception& ex)
        {
            SPDLOG_ERROR("fuego WS dispatch error: {}", ex.what());
        }
    }

    void
    fuego_ws_client::read_loop()
    {
        beast::flat_buffer buffer;
        while (m_running && m_ws)
        {
            try
            {
                buffer.consume(buffer.size());
                m_ws->read(buffer);
                auto msg = beast::buffers_to_string(buffer.data());
                dispatch_message(msg);
            }
            catch (const beast::system_error& se)
            {
                if (se.code() == websocket::error::closed)
                {
                    SPDLOG_INFO("fuego WebSocket closed");
                    break;
                }
                SPDLOG_ERROR("fuego WS read error: {}", se.what());
                // Reconnect after delay
                if (m_running)
                {
                    std::this_thread::sleep_for(std::chrono::seconds(5));
                    connect();
                }
            }
        }
    }

    void
    fuego_ws_client::start()
    {
        if (m_running)
            return;
        m_running = true;
        connect();
        m_ws_thread = std::thread([this]() { read_loop(); });
    }

    void
    fuego_ws_client::stop()
    {
        m_running = false;
        if (m_ws)
        {
            try { m_ws->close(websocket::close_code::normal); }
            catch (...) { }
            m_ws.reset();
        }
        if (m_ws_thread.joinable())
            m_ws_thread.join();
    }
} // namespace atomic_dex::fuego
