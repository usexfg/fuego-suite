#include "atomicdex/api/fuego/cd_rpc.hpp"

#include <nlohmann/json.hpp>

namespace atomic_dex::fuego
{
    void from_json(const nlohmann::json& j, cd_info& info)
    {
        j.at("cd_id").get_to(info.cd_id);
        j.at("owner").get_to(info.owner);
        j.at("coin").get_to(info.coin);
        j.at("amount").get_to(info.amount);
        j.at("interest_rate").get_to(info.interest_rate);
        j.at("maturity_height").get_to(info.maturity_height);
        j.at("deposit_height").get_to(info.deposit_height);
        j.at("accrued_interest").get_to(info.accrued_interest);
        j.at("total_value").get_to(info.total_value);
        j.at("blocks_to_maturity").get_to(info.blocks_to_maturity);
        j.at("matured").get_to(info.matured);
        if (j.contains("for_sale"))
            j.at("for_sale").get_to(info.for_sale);
    }

    void from_json(const nlohmann::json& j, cd_list_result& res)
    {
        j.at("cds").get_to(res.cds);
    }

    void to_json(nlohmann::json& j, const cd_create_request& req)
    {
        j["coin"]   = req.coin;
        j["amount"] = req.amount;
        if (req.duration_blocks.has_value())
            j["duration_blocks"] = req.duration_blocks.value();
    }

    void from_json(const nlohmann::json& j, cd_create_result& res)
    {
        j.at("cd_id").get_to(res.cd_id);
        j.at("tx_hash").get_to(res.tx_hash);
        j.at("coin").get_to(res.coin);
        j.at("amount").get_to(res.amount);
        j.at("maturity_at").get_to(res.maturity_at);
    }

    void to_json(nlohmann::json& j, const cd_claim_request& req)
    {
        j["cd_id"] = req.cd_id;
    }

    void from_json(const nlohmann::json& j, cd_claim_result& res)
    {
        j.at("cd_id").get_to(res.cd_id);
        j.at("tx_hash").get_to(res.tx_hash);
        j.at("coin").get_to(res.coin);
        j.at("principal").get_to(res.principal);
        j.at("interest").get_to(res.interest);
        j.at("total").get_to(res.total);
    }

    void from_json(const nlohmann::json& j, cd_market_listing& listing)
    {
        j.at("listing_id").get_to(listing.listing_id);
        j.at("cd_id").get_to(listing.cd_id);
        j.at("seller").get_to(listing.seller);
        j.at("coin").get_to(listing.coin);
        j.at("amount").get_to(listing.amount);
        j.at("price").get_to(listing.price);
        j.at("interest_rate").get_to(listing.interest_rate);
        j.at("blocks_remaining").get_to(listing.blocks_remaining);
    }

    void from_json(const nlohmann::json& j, cd_market_list_result& res)
    {
        j.at("listings").get_to(res.listings);
    }

    void to_json(nlohmann::json& j, const cd_sell_request& req)
    {
        j["cd_id"] = req.cd_id;
        j["price"] = req.price;
    }

    void from_json(const nlohmann::json& j, cd_sell_result& res)
    {
        j.at("listing_id").get_to(res.listing_id);
        j.at("cd_id").get_to(res.cd_id);
        j.at("tx_hash").get_to(res.tx_hash);
    }

    void to_json(nlohmann::json& j, const cd_buy_request& req)
    {
        j["listing_id"] = req.listing_id;
    }

    void from_json(const nlohmann::json& j, cd_buy_result& res)
    {
        j.at("listing_id").get_to(res.listing_id);
        j.at("cd_id").get_to(res.cd_id);
        j.at("tx_hash").get_to(res.tx_hash);
        j.at("coin").get_to(res.coin);
        j.at("amount").get_to(res.amount);
        j.at("price_paid").get_to(res.price_paid);
    }

    void to_json(nlohmann::json& j, const cd_cancel_listing_request& req)
    {
        j["listing_id"] = req.listing_id;
    }

    void from_json(const nlohmann::json& j, cd_cancel_listing_result& res)
    {
        j.at("listing_id").get_to(res.listing_id);
        j.at("tx_hash").get_to(res.tx_hash);
    }

    void from_json(const nlohmann::json& j, cd_apy_result& res)
    {
        j.at("coin").get_to(res.coin);
        j.at("current_apy").get_to(res.current_apy);
        j.at("average_apy").get_to(res.average_apy);
        if (j.contains("epoch"))
            j.at("epoch").get_to(res.epoch);
    }
} // namespace atomic_dex::fuego
