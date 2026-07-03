#pragma once

#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

#include "atomicdex/api/mm2/rpc.hpp"

namespace atomic_dex::fuego
{
    struct hearth_pool_stats_request
    {
        std::string coin_a;
        std::string coin_b;
    };

    struct hearth_pool_stats_result
    {
        std::string pool_id;
        std::string coin_a;
        std::string coin_b;
        std::string reserve_a;
        std::string reserve_b;
        std::string lp_token;
        std::string total_lp_supply;
        std::string tvl;
        std::string volume_24h;
        std::string fees_24h;
        double      apr;
    };

    struct hearth_pool_stats_rpc
    {
        static constexpr auto endpoint = "hearth::pool_stats";
        static constexpr bool is_v2    = true;

        using expected_request_type = hearth_pool_stats_request;
        using expected_result_type  = hearth_pool_stats_result;
        using expected_error_type   = mm2::rpc_basic_error_type;

        expected_request_type               request;
        std::optional<expected_result_type> result;
        std::optional<expected_error_type>  error;
        std::string                         raw_result;
    };

    void to_json(nlohmann::json& j, const hearth_pool_stats_request& req);
    void from_json(const nlohmann::json& j, hearth_pool_stats_result& res);

    struct hearth_quote_request
    {
        std::string coin_a;
        std::string coin_b;
        std::string amount;
        bool        exact_input;
    };

    struct hearth_quote_result
    {
        std::string pool_id;
        std::string input_coin;
        std::string input_amount;
        std::string output_coin;
        std::string output_amount;
        std::string price;
        std::string price_impact;
        std::string fee;
        double      slippage;
    };

    struct hearth_quote_rpc
    {
        static constexpr auto endpoint = "hearth::quote";
        static constexpr bool is_v2    = true;

        using expected_request_type = hearth_quote_request;
        using expected_result_type  = hearth_quote_result;
        using expected_error_type   = mm2::rpc_basic_error_type;

        expected_request_type               request;
        std::optional<expected_result_type> result;
        std::optional<expected_error_type>  error;
        std::string                         raw_result;
    };

    void to_json(nlohmann::json& j, const hearth_quote_request& req);
    void from_json(const nlohmann::json& j, hearth_quote_result& res);

    struct hearth_swap_execute_request
    {
        std::string coin_a;
        std::string coin_b;
        std::string amount;
        std::string min_received;
        std::string deadline;
    };

    struct hearth_swap_execute_result
    {
        std::string tx_hash;
        std::string input_coin;
        std::string input_amount;
        std::string output_coin;
        std::string output_amount;
        std::string fee;
    };

    struct hearth_swap_execute_rpc
    {
        static constexpr auto endpoint = "hearth::swap_execute";
        static constexpr bool is_v2    = true;

        using expected_request_type = hearth_swap_execute_request;
        using expected_result_type  = hearth_swap_execute_result;
        using expected_error_type   = mm2::rpc_basic_error_type;

        expected_request_type               request;
        std::optional<expected_result_type> result;
        std::optional<expected_error_type>  error;
        std::string                         raw_result;
    };

    void to_json(nlohmann::json& j, const hearth_swap_execute_request& req);
    void from_json(const nlohmann::json& j, hearth_swap_execute_result& res);

    struct hearth_add_liquidity_request
    {
        std::string coin_a;
        std::string coin_b;
        std::string amount_a;
        std::string amount_b;
        std::string min_lp_tokens;
    };

    struct hearth_add_liquidity_result
    {
        std::string tx_hash;
        std::string coin_a;
        std::string coin_b;
        std::string amount_a;
        std::string amount_b;
        std::string lp_tokens;
        std::string total_lp_supply;
    };

    struct hearth_add_liquidity_rpc
    {
        static constexpr auto endpoint = "hearth::add_liquidity";
        static constexpr bool is_v2    = true;

        using expected_request_type = hearth_add_liquidity_request;
        using expected_result_type  = hearth_add_liquidity_result;
        using expected_error_type   = mm2::rpc_basic_error_type;

        expected_request_type               request;
        std::optional<expected_result_type> result;
        std::optional<expected_error_type>  error;
        std::string                         raw_result;
    };

    void to_json(nlohmann::json& j, const hearth_add_liquidity_request& req);
    void from_json(const nlohmann::json& j, hearth_add_liquidity_result& res);

    struct hearth_remove_liquidity_request
    {
        std::string lp_coin;
        std::string lp_amount;
        std::string min_coin_a;
        std::string min_coin_b;
    };

    struct hearth_remove_liquidity_result
    {
        std::string tx_hash;
        std::string coin_a;
        std::string coin_b;
        std::string amount_a;
        std::string amount_b;
        std::string lp_burned;
    };

    struct hearth_remove_liquidity_rpc
    {
        static constexpr auto endpoint = "hearth::remove_liquidity";
        static constexpr bool is_v2    = true;

        using expected_request_type = hearth_remove_liquidity_request;
        using expected_result_type  = hearth_remove_liquidity_result;
        using expected_error_type   = mm2::rpc_basic_error_type;

        expected_request_type               request;
        std::optional<expected_result_type> result;
        std::optional<expected_error_type>  error;
        std::string                         raw_result;
    };

    void to_json(nlohmann::json& j, const hearth_remove_liquidity_request& req);
    void from_json(const nlohmann::json& j, hearth_remove_liquidity_result& res);

    struct hearth_position_request
    {
        std::optional<std::string> coin_a;
        std::optional<std::string> coin_b;
        std::optional<std::string> lp_coin;
    };

    struct hearth_position_result
    {
        std::vector<hearth_pool_stats_result> positions;
    };

    struct hearth_position_rpc
    {
        static constexpr auto endpoint = "hearth::position";
        static constexpr bool is_v2    = true;

        using expected_request_type = hearth_position_request;
        using expected_result_type  = hearth_position_result;
        using expected_error_type   = mm2::rpc_basic_error_type;

        expected_request_type               request;
        std::optional<expected_result_type> result;
        std::optional<expected_error_type>  error;
        std::string                         raw_result;
    };

    void to_json(nlohmann::json& j, const hearth_position_request& req);
    void from_json(const nlohmann::json& j, hearth_position_result& res);

    struct hearth_pools_result
    {
        std::vector<hearth_pool_stats_result> pools;
    };

    struct hearth_pools_rpc
    {
        static constexpr auto endpoint = "hearth::pools";
        static constexpr bool is_v2    = true;

        using expected_request_type = nlohmann::json;
        using expected_result_type  = hearth_pools_result;
        using expected_error_type   = mm2::rpc_basic_error_type;

        nlohmann::json                          request;
        std::optional<expected_result_type> result;
        std::optional<expected_error_type>  error;
        std::string                         raw_result;
    };

    void from_json(const nlohmann::json& j, hearth_pools_result& res);
} // namespace atomic_dex::fuego
