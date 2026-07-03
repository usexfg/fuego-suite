#pragma once

#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

#include "atomicdex/api/mm2/rpc.hpp"

namespace atomic_dex::fuego
{
    struct fuego_balance_request
    {
        std::string coin;
    };

    struct fuego_balance_result
    {
        std::string coin;
        std::string balance;
        std::string unconfirmed_balance;
        std::string stake;
        std::string unconfirmed_stake;
    };

    struct fuego_balance_rpc
    {
        static constexpr auto endpoint = "fuego::balance";
        static constexpr bool is_v2    = true;

        using expected_request_type = fuego_balance_request;
        using expected_result_type  = fuego_balance_result;
        using expected_error_type   = mm2::rpc_basic_error_type;

        expected_request_type               request;
        std::optional<expected_result_type> result;
        std::optional<expected_error_type>  error;
        std::string                         raw_result;
    };

    void to_json(nlohmann::json& j, const fuego_balance_request& req);
    void from_json(const nlohmann::json& j, fuego_balance_result& res);

    struct fuego_send_request
    {
        std::string coin;
        std::string to;
        std::string amount;
        std::optional<std::string> memo;
    };

    struct fuego_send_result
    {
        std::string tx_hash;
        std::string tx_hex;
    };

    struct fuego_send_rpc
    {
        static constexpr auto endpoint = "fuego::send";
        static constexpr bool is_v2    = true;

        using expected_request_type = fuego_send_request;
        using expected_result_type  = fuego_send_result;
        using expected_error_type   = mm2::rpc_basic_error_type;

        expected_request_type               request;
        std::optional<expected_result_type> result;
        std::optional<expected_error_type>  error;
        std::string                         raw_result;
    };

    void to_json(nlohmann::json& j, const fuego_send_request& req);
    void from_json(const nlohmann::json& j, fuego_send_result& res);

    struct fuego_burn2mint_request
    {
        std::string coin;
        std::string amount;
        std::optional<std::string> destination_address;
        std::optional<std::string> proof;
    };

    struct fuego_burn2mint_result
    {
        std::string tx_hash;
        std::string burn_amount;
        std::string mint_amount;
        std::string mint_coin;
        std::string destination;
    };

    struct fuego_burn2mint_rpc
    {
        static constexpr auto endpoint = "fuego::burn2mint";
        static constexpr bool is_v2    = true;

        using expected_request_type = fuego_burn2mint_request;
        using expected_result_type  = fuego_burn2mint_result;
        using expected_error_type   = mm2::rpc_basic_error_type;

        expected_request_type               request;
        std::optional<expected_result_type> result;
        std::optional<expected_error_type>  error;
        std::string                         raw_result;
    };

    void to_json(nlohmann::json& j, const fuego_burn2mint_request& req);
    void from_json(const nlohmann::json& j, fuego_burn2mint_result& res);

    struct fuego_alias_request
    {
        std::optional<std::string> name;
        std::optional<std::string> address;
    };

    struct fuego_alias_result
    {
        std::string                         name;
        std::string                         address;
        std::string                         owner;
        std::optional<std::string>          expires_at;
        std::optional<nlohmann::json>       metadata;
    };

    struct fuego_alias_rpc
    {
        static constexpr auto endpoint = "fuego::alias";
        static constexpr bool is_v2    = true;

        using expected_request_type = fuego_alias_request;
        using expected_result_type  = fuego_alias_result;
        using expected_error_type   = mm2::rpc_basic_error_type;

        expected_request_type               request;
        std::optional<expected_result_type> result;
        std::optional<expected_error_type>  error;
        std::string                         raw_result;
    };

    void to_json(nlohmann::json& j, const fuego_alias_request& req);
    void from_json(const nlohmann::json& j, fuego_alias_result& res);

    struct fuego_activity_request
    {
        std::string  coin;
        std::optional<std::size_t> limit;
        std::optional<std::size_t> offset;
    };

    struct fuego_activity_entry
    {
        std::string tx_hash;
        std::string type;
        std::string amount;
        std::string fee;
        std::string timestamp;
        int64_t     height;
        std::string other_addr;
        std::string memo;
    };

    struct fuego_activity_result
    {
        std::string                        coin;
        std::vector<fuego_activity_entry>  entries;
        std::size_t                        total;
    };

    struct fuego_activity_rpc
    {
        static constexpr auto endpoint = "fuego::activity";
        static constexpr bool is_v2    = true;

        using expected_request_type = fuego_activity_request;
        using expected_result_type  = fuego_activity_result;
        using expected_error_type   = mm2::rpc_basic_error_type;

        expected_request_type               request;
        std::optional<expected_result_type> result;
        std::optional<expected_error_type>  error;
        std::string                         raw_result;
    };

    void to_json(nlohmann::json& j, const fuego_activity_request& req);
    void from_json(const nlohmann::json& j, fuego_activity_result& res);
    void from_json(const nlohmann::json& j, fuego_activity_entry& e);

    struct fuego_blockheight_result
    {
        std::string coin;
        int64_t     height;
        std::string hash;
    };

    struct fuego_blockheight_rpc
    {
        static constexpr auto endpoint = "fuego::blockheight";
        static constexpr bool is_v2    = true;

        using expected_request_type = nlohmann::json;
        using expected_result_type  = fuego_blockheight_result;
        using expected_error_type   = mm2::rpc_basic_error_type;

        nlohmann::json                          request;
        std::optional<expected_result_type> result;
        std::optional<expected_error_type>  error;
        std::string                         raw_result;
    };

    void from_json(const nlohmann::json& j, fuego_blockheight_result& res);
} // namespace atomic_dex::fuego
