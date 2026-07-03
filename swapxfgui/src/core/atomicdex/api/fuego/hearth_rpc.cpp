#include "atomicdex/api/fuego/hearth_rpc.hpp"

#include <nlohmann/json.hpp>

namespace atomic_dex::fuego
{
    void to_json(nlohmann::json& j, const hearth_pool_stats_request& req)
    {
        j["coin_a"] = req.coin_a;
        j["coin_b"] = req.coin_b;
    }

    void from_json(const nlohmann::json& j, hearth_pool_stats_result& res)
    {
        j.at("pool_id").get_to(res.pool_id);
        j.at("coin_a").get_to(res.coin_a);
        j.at("coin_b").get_to(res.coin_b);
        j.at("reserve_a").get_to(res.reserve_a);
        j.at("reserve_b").get_to(res.reserve_b);
        j.at("lp_token").get_to(res.lp_token);
        j.at("total_lp_supply").get_to(res.total_lp_supply);
        j.at("tvl").get_to(res.tvl);
        if (j.contains("volume_24h"))
            j.at("volume_24h").get_to(res.volume_24h);
        if (j.contains("fees_24h"))
            j.at("fees_24h").get_to(res.fees_24h);
        if (j.contains("apr"))
            j.at("apr").get_to(res.apr);
    }

    void to_json(nlohmann::json& j, const hearth_quote_request& req)
    {
        j["coin_a"]     = req.coin_a;
        j["coin_b"]     = req.coin_b;
        j["amount"]     = req.amount;
        j["exact_input"] = req.exact_input;
    }

    void from_json(const nlohmann::json& j, hearth_quote_result& res)
    {
        j.at("pool_id").get_to(res.pool_id);
        j.at("input_coin").get_to(res.input_coin);
        j.at("input_amount").get_to(res.input_amount);
        j.at("output_coin").get_to(res.output_coin);
        j.at("output_amount").get_to(res.output_amount);
        j.at("price").get_to(res.price);
        j.at("price_impact").get_to(res.price_impact);
        j.at("fee").get_to(res.fee);
        if (j.contains("slippage"))
            j.at("slippage").get_to(res.slippage);
    }

    void to_json(nlohmann::json& j, const hearth_swap_execute_request& req)
    {
        j["coin_a"]      = req.coin_a;
        j["coin_b"]      = req.coin_b;
        j["amount"]      = req.amount;
        j["min_received"] = req.min_received;
        j["deadline"]    = req.deadline;
    }

    void from_json(const nlohmann::json& j, hearth_swap_execute_result& res)
    {
        j.at("tx_hash").get_to(res.tx_hash);
        j.at("input_coin").get_to(res.input_coin);
        j.at("input_amount").get_to(res.input_amount);
        j.at("output_coin").get_to(res.output_coin);
        j.at("output_amount").get_to(res.output_amount);
        j.at("fee").get_to(res.fee);
    }

    void to_json(nlohmann::json& j, const hearth_add_liquidity_request& req)
    {
        j["coin_a"]       = req.coin_a;
        j["coin_b"]       = req.coin_b;
        j["amount_a"]     = req.amount_a;
        j["amount_b"]     = req.amount_b;
        j["min_lp_tokens"] = req.min_lp_tokens;
    }

    void from_json(const nlohmann::json& j, hearth_add_liquidity_result& res)
    {
        j.at("tx_hash").get_to(res.tx_hash);
        j.at("coin_a").get_to(res.coin_a);
        j.at("coin_b").get_to(res.coin_b);
        j.at("amount_a").get_to(res.amount_a);
        j.at("amount_b").get_to(res.amount_b);
        j.at("lp_tokens").get_to(res.lp_tokens);
        j.at("total_lp_supply").get_to(res.total_lp_supply);
    }

    void to_json(nlohmann::json& j, const hearth_remove_liquidity_request& req)
    {
        j["lp_coin"]     = req.lp_coin;
        j["lp_amount"]   = req.lp_amount;
        j["min_coin_a"]  = req.min_coin_a;
        j["min_coin_b"]  = req.min_coin_b;
    }

    void from_json(const nlohmann::json& j, hearth_remove_liquidity_result& res)
    {
        j.at("tx_hash").get_to(res.tx_hash);
        j.at("coin_a").get_to(res.coin_a);
        j.at("coin_b").get_to(res.coin_b);
        j.at("amount_a").get_to(res.amount_a);
        j.at("amount_b").get_to(res.amount_b);
        j.at("lp_burned").get_to(res.lp_burned);
    }

    void to_json(nlohmann::json& j, const hearth_position_request& req)
    {
        if (req.coin_a.has_value())
            j["coin_a"] = req.coin_a.value();
        if (req.coin_b.has_value())
            j["coin_b"] = req.coin_b.value();
        if (req.lp_coin.has_value())
            j["lp_coin"] = req.lp_coin.value();
    }

    void from_json(const nlohmann::json& j, hearth_position_result& res)
    {
        j.at("positions").get_to(res.positions);
    }

    void from_json(const nlohmann::json& j, hearth_pools_result& res)
    {
        j.at("pools").get_to(res.pools);
    }
} // namespace atomic_dex::fuego
