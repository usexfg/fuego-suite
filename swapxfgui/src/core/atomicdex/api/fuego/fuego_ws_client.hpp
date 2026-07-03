#pragma once

#include <functional>
#include <memory>
#include <string>

#include <nlohmann/json_fwd.hpp>

namespace boost::beast
{
    template <class Derived>
    class websocket_stream;
} // namespace boost::beast

namespace atomic_dex::fuego
{
    using ws_block_callback   = std::function<void(int64_t height, const std::string& coin, const std::string& hash)>;
    using ws_swap_callback    = std::function<void(const std::string& uuid, const std::string& status, const nlohmann::json& details)>;
    using ws_order_callback   = std::function<void(const std::string& action, const nlohmann::json& order)>;

    struct fuego_ws_config
    {
        std::string url{"ws://127.0.0.1:7784/ws"};
        ws_block_callback  on_block{nullptr};
        ws_swap_callback   on_swap_update{nullptr};
        ws_order_callback  on_order_fill{nullptr};
    };

    class fuego_ws_client
    {
        std::unique_ptr<boost::beast::websocket_stream<boost::beast::tcp_stream>> m_ws;
        std::string  m_url;
        std::thread  m_ws_thread;
        bool         m_running{false};

        ws_block_callback m_on_block;
        ws_swap_callback  m_on_swap_update;
        ws_order_callback m_on_order_fill;

        void connect();
        void read_loop();
        void dispatch_message(const std::string& msg);

      public:
        explicit fuego_ws_client(fuego_ws_config cfg);
        ~fuego_ws_client();

        void start();
        void stop();
        bool is_running() const { return m_running; }
    };
} // namespace atomic_dex::fuego
