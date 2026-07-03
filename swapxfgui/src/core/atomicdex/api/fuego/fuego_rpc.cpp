#include "atomicdex/api/fuego/fuego_rpc.hpp"

#include <nlohmann/json.hpp>

namespace atomic_dex::fuego
{
    void to_json(nlohmann::json& j, const fuego_balance_request& req)
    {
        j["coin"] = req.coin;
    }

    void from_json(const nlohmann::json& j, fuego_balance_result& res)
    {
        j.at("coin").get_to(res.coin);
        j.at("balance").get_to(res.balance);
        if (j.contains("unconfirmed_balance"))
            j.at("unconfirmed_balance").get_to(res.unconfirmed_balance);
        if (j.contains("stake"))
            j.at("stake").get_to(res.stake);
        if (j.contains("unconfirmed_stake"))
            j.at("unconfirmed_stake").get_to(res.unconfirmed_stake);
    }

    void to_json(nlohmann::json& j, const fuego_send_request& req)
    {
        j["coin"]   = req.coin;
        j["to"]     = req.to;
        j["amount"] = req.amount;
        if (req.memo.has_value())
            j["memo"] = req.memo.value();
    }

    void from_json(const nlohmann::json& j, fuego_send_result& res)
    {
        j.at("tx_hash").get_to(res.tx_hash);
        if (j.contains("tx_hex"))
            j.at("tx_hex").get_to(res.tx_hex);
    }

    void to_json(nlohmann::json& j, const fuego_burn2mint_request& req)
    {
        j["coin"]   = req.coin;
        j["amount"] = req.amount;
        if (req.destination_address.has_value())
            j["destination_address"] = req.destination_address.value();
        if (req.proof.has_value())
            j["proof"] = req.proof.value();
    }

    void from_json(const nlohmann::json& j, fuego_burn2mint_result& res)
    {
        j.at("tx_hash").get_to(res.tx_hash);
        j.at("burn_amount").get_to(res.burn_amount);
        j.at("mint_amount").get_to(res.mint_amount);
        j.at("mint_coin").get_to(res.mint_coin);
        j.at("destination").get_to(res.destination);
    }

    void to_json(nlohmann::json& j, const fuego_alias_request& req)
    {
        if (req.name.has_value())
            j["name"] = req.name.value();
        if (req.address.has_value())
            j["address"] = req.address.value();
    }

    void from_json(const nlohmann::json& j, fuego_alias_result& res)
    {
        j.at("name").get_to(res.name);
        j.at("address").get_to(res.address);
        j.at("owner").get_to(res.owner);
        if (j.contains("expires_at"))
            j.at("expires_at").get_to(res.expires_at.emplace());
        if (j.contains("metadata"))
            j.at("metadata").get_to(res.metadata.emplace());
    }

    void to_json(nlohmann::json& j, const fuego_activity_request& req)
    {
        j["coin"] = req.coin;
        if (req.limit.has_value())
            j["limit"] = req.limit.value();
        if (req.offset.has_value())
            j["offset"] = req.offset.value();
    }

    void from_json(const nlohmann::json& j, fuego_activity_entry& e)
    {
        j.at("tx_hash").get_to(e.tx_hash);
        j.at("type").get_to(e.type);
        j.at("amount").get_to(e.amount);
        j.at("fee").get_to(e.fee);
        j.at("timestamp").get_to(e.timestamp);
        j.at("height").get_to(e.height);
        j.at("other_addr").get_to(e.other_addr);
        if (j.contains("memo"))
            j.at("memo").get_to(e.memo);
    }

    void from_json(const nlohmann::json& j, fuego_activity_result& res)
    {
        j.at("coin").get_to(res.coin);
        j.at("entries").get_to(res.entries);
        j.at("total").get_to(res.total);
    }

    void from_json(const nlohmann::json& j, fuego_blockheight_result& res)
    {
        j.at("coin").get_to(res.coin);
        j.at("height").get_to(res.height);
        if (j.contains("hash"))
            j.at("hash").get_to(res.hash);
    }
} // namespace atomic_dex::fuego
