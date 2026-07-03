#pragma once

#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

#include "atomicdex/api/mm2/rpc.hpp"

namespace atomic_dex::fuego
{
    struct cd_info
    {
        std::string  cd_id;
        std::string  owner;
        std::string  coin;
        std::string  amount;
        std::string  interest_rate;
        std::string  maturity_height;
        std::string  deposit_height;
        std::string  accrued_interest;
        std::string  total_value;
        int64_t      blocks_to_maturity;
        bool         matured;
        bool         for_sale;
    };

    struct cd_list_result
    {
        std::vector<cd_info> cds;
    };

    struct cd_list_rpc
    {
        static constexpr auto endpoint = "cd::list";
        static constexpr bool is_v2    = true;

        using expected_request_type = nlohmann::json;
        using expected_result_type  = cd_list_result;
        using expected_error_type   = mm2::rpc_basic_error_type;

        nlohmann::json                          request;
        std::optional<expected_result_type> result;
        std::optional<expected_error_type>  error;
        std::string                         raw_result;
    };

    void from_json(const nlohmann::json& j, cd_info& info);
    void from_json(const nlohmann::json& j, cd_list_result& res);

    struct cd_create_request
    {
        std::string coin;
        std::string amount;
        std::optional<int64_t> duration_blocks;
    };

    struct cd_create_result
    {
        std::string cd_id;
        std::string tx_hash;
        std::string coin;
        std::string amount;
        std::string maturity_at;
    };

    struct cd_create_rpc
    {
        static constexpr auto endpoint = "cd::create";
        static constexpr bool is_v2    = true;

        using expected_request_type = cd_create_request;
        using expected_result_type  = cd_create_result;
        using expected_error_type   = mm2::rpc_basic_error_type;

        expected_request_type               request;
        std::optional<expected_result_type> result;
        std::optional<expected_error_type>  error;
        std::string                         raw_result;
    };

    void to_json(nlohmann::json& j, const cd_create_request& req);
    void from_json(const nlohmann::json& j, cd_create_result& res);

    struct cd_claim_request
    {
        std::string cd_id;
    };

    struct cd_claim_result
    {
        std::string cd_id;
        std::string tx_hash;
        std::string coin;
        std::string principal;
        std::string interest;
        std::string total;
    };

    struct cd_claim_rpc
    {
        static constexpr auto endpoint = "cd::claim";
        static constexpr bool is_v2    = true;

        using expected_request_type = cd_claim_request;
        using expected_result_type  = cd_claim_result;
        using expected_error_type   = mm2::rpc_basic_error_type;

        expected_request_type               request;
        std::optional<expected_result_type> result;
        std::optional<expected_error_type>  error;
        std::string                         raw_result;
    };

    void to_json(nlohmann::json& j, const cd_claim_request& req);
    void from_json(const nlohmann::json& j, cd_claim_result& res);

    struct cd_market_listing
    {
        std::string  listing_id;
        std::string  cd_id;
        std::string  seller;
        std::string  coin;
        std::string  amount;
        std::string  price;
        std::string  interest_rate;
        int64_t      blocks_remaining;
    };

    struct cd_market_list_result
    {
        std::vector<cd_market_listing> listings;
    };

    struct cd_market_list_rpc
    {
        static constexpr auto endpoint = "cd::market_list";
        static constexpr bool is_v2    = true;

        using expected_request_type = nlohmann::json;
        using expected_result_type  = cd_market_list_result;
        using expected_error_type   = mm2::rpc_basic_error_type;

        nlohmann::json                          request;
        std::optional<expected_result_type> result;
        std::optional<expected_error_type>  error;
        std::string                         raw_result;
    };

    void from_json(const nlohmann::json& j, cd_market_listing& listing);
    void from_json(const nlohmann::json& j, cd_market_list_result& res);

    struct cd_sell_request
    {
        std::string cd_id;
        std::string price;
    };

    struct cd_sell_result
    {
        std::string listing_id;
        std::string cd_id;
        std::string tx_hash;
    };

    struct cd_sell_rpc
    {
        static constexpr auto endpoint = "cd::sell";
        static constexpr bool is_v2    = true;

        using expected_request_type = cd_sell_request;
        using expected_result_type  = cd_sell_result;
        using expected_error_type   = mm2::rpc_basic_error_type;

        expected_request_type               request;
        std::optional<expected_result_type> result;
        std::optional<expected_error_type>  error;
        std::string                         raw_result;
    };

    void to_json(nlohmann::json& j, const cd_sell_request& req);
    void from_json(const nlohmann::json& j, cd_sell_result& res);

    struct cd_buy_request
    {
        std::string listing_id;
    };

    struct cd_buy_result
    {
        std::string listing_id;
        std::string cd_id;
        std::string tx_hash;
        std::string coin;
        std::string amount;
        std::string price_paid;
    };

    struct cd_buy_rpc
    {
        static constexpr auto endpoint = "cd::buy";
        static constexpr bool is_v2    = true;

        using expected_request_type = cd_buy_request;
        using expected_result_type  = cd_buy_result;
        using expected_error_type   = mm2::rpc_basic_error_type;

        expected_request_type               request;
        std::optional<expected_result_type> result;
        std::optional<expected_error_type>  error;
        std::string                         raw_result;
    };

    void to_json(nlohmann::json& j, const cd_buy_request& req);
    void from_json(const nlohmann::json& j, cd_buy_result& res);

    struct cd_cancel_listing_request
    {
        std::string listing_id;
    };

    struct cd_cancel_listing_result
    {
        std::string listing_id;
        std::string tx_hash;
    };

    struct cd_cancel_listing_rpc
    {
        static constexpr auto endpoint = "cd::cancel_listing";
        static constexpr bool is_v2    = true;

        using expected_request_type = cd_cancel_listing_request;
        using expected_result_type  = cd_cancel_listing_result;
        using expected_error_type   = mm2::rpc_basic_error_type;

        expected_request_type               request;
        std::optional<expected_result_type> result;
        std::optional<expected_error_type>  error;
        std::string                         raw_result;
    };

    void to_json(nlohmann::json& j, const cd_cancel_listing_request& req);
    void from_json(const nlohmann::json& j, cd_cancel_listing_result& res);

    struct cd_apy_result
    {
        std::string coin;
        double      current_apy;
        double      average_apy;
        int64_t     epoch;
    };

    struct cd_apy_rpc
    {
        static constexpr auto endpoint = "cd::apy";
        static constexpr bool is_v2    = true;

        using expected_request_type = nlohmann::json;
        using expected_result_type  = cd_apy_result;
        using expected_error_type   = mm2::rpc_basic_error_type;

        nlohmann::json                          request;
        std::optional<expected_result_type> result;
        std::optional<expected_error_type>  error;
        std::string                         raw_result;
    };

    void from_json(const nlohmann::json& j, cd_apy_result& res);
} // namespace atomic_dex::fuego
