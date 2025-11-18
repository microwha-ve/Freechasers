#include "fc/lavalink/client.hpp"

#include <dpp/json.h>

#include <sstream>
#include <future>
#include <map>

namespace fc::lavalink {

node::node(dpp::cluster& cluster, const node_config& cfg)
    : m_cluster(cluster)
    , m_cfg(cfg)
    , m_session_id(cfg.session_id)
{
    std::ostringstream oss;
    oss << "Initialising Lavalink node at "
        << (m_cfg.https ? "https://" : "http://")
        << m_cfg.host << ":" << m_cfg.port
        << " with session_id='" << m_session_id << "'";
    m_cluster.log(dpp::ll_info, oss.str());
}

void node::handle_voice_state_update(const dpp::voice_state_update_t& ev)
{
    // Only cache our own bot's voice state
    if (ev.state.user_id != m_cluster.me.id) {
        return;
    }

    std::lock_guard<std::mutex> lock(m_voice_mutex);
    auto& vs = m_voice_states[ev.state.guild_id];
    vs.session_id = ev.state.session_id;

    std::ostringstream oss;
    oss << "Cached voice_state for guild " << ev.state.guild_id
        << " session_id=" << vs.session_id;
    m_cluster.log(dpp::ll_debug, oss.str());
}

void node::handle_voice_server_update(const dpp::voice_server_update_t& ev)
{
    std::lock_guard<std::mutex> lock(m_voice_mutex);
    auto& vs = m_voice_states[ev.guild_id];
    vs.token    = ev.token;
    vs.endpoint = ev.endpoint;

    std::ostringstream oss;
    oss << "Cached voice_server for guild " << ev.guild_id
        << " token=" << (!vs.token.empty() ? "<set>" : "<empty>")
        << " endpoint=" << vs.endpoint;
    m_cluster.log(dpp::ll_debug, oss.str());
}

std::optional<node::voice_state> node::get_voice_state_locked(dpp::snowflake guild_id) const
{
    auto it = m_voice_states.find(guild_id);
    if (it == m_voice_states.end()) {
        return std::nullopt;
    }
    const auto& vs = it->second;
    if (vs.token.empty() || vs.endpoint.empty() || vs.session_id.empty()) {
        return std::nullopt;
    }
    return vs;
}

std::string node::http_request(const std::string& method,
                               const std::string& urlpath,
                               const std::string& body_json) const
{
    dpp::http_method http_method_enum = dpp::m_get;
    if (method == "POST") {
        http_method_enum = dpp::m_post;
    } else if (method == "PATCH") {
        http_method_enum = dpp::m_patch;
    } else if (method == "DELETE") {
        http_method_enum = dpp::m_delete;
    } else if (method == "PUT") {
        http_method_enum = dpp::m_put;
    }

    const std::string scheme   = m_cfg.https ? "https://" : "http://";
    const std::string full_url = scheme + m_cfg.host + ":" + std::to_string(m_cfg.port) + urlpath;

    std::multimap<std::string, std::string> headers;
    headers.emplace("Authorization", m_cfg.password);
    headers.emplace("User-Id",       m_cluster.me.id.str());
    headers.emplace("Client-Name",   "FreechasersBot");

    auto prom = std::make_shared<std::promise<dpp::http_request_completion_t>>();
    auto fut  = prom->get_future();

    m_cluster.log(
        dpp::ll_debug,
        "Lavalink HTTP request: " + method + " " + urlpath +
        " (URL=" + full_url + ", body=" +
        (body_json.empty() ? "empty" : std::to_string(body_json.size()) + " bytes") + ")"
    );

    m_cluster.request(
        full_url,
        http_method_enum,
        [this, method, urlpath, full_url, prom](const dpp::http_request_completion_t& cc) {
            std::ostringstream oss;
            oss << "Lavalink HTTP " << cc.status
                << " on " << method << " " << urlpath
                << " (URL=" << full_url
                << ", response length=" << cc.body.size() << ")";

            if (cc.status == 0) {
                m_cluster.log(dpp::ll_warning, oss.str() + " (request failed)");
            } else if (cc.status >= 400) {
                m_cluster.log(dpp::ll_warning, oss.str() + " response: " + cc.body);
            } else {
                m_cluster.log(dpp::ll_debug, oss.str());
            }

            prom->set_value(cc);
        },
        body_json,
        body_json.empty() ? "" : "application/json",
        headers
    );

    const auto cc = fut.get();
    if (cc.status == 0 || cc.body.empty()) {
        return {};
    }
    return cc.body;
}

load_result node::load_tracks(const std::string& identifier) const
{
    load_result res;

    m_cluster.log(
        dpp::ll_debug,
        "Requesting /v4/loadtracks for identifier: " + identifier
    );

    std::string path = "/v4/loadtracks?identifier=" + dpp::url_encode(identifier);
    std::string body = http_request("GET", path, {});

    if (body.empty()) {
        m_cluster.log(
            dpp::ll_warning,
            "Empty response from Lavalink /loadtracks for identifier: " + identifier
        );
        res.type = load_type::empty;
        return res;
    }

    using json = nlohmann::json_abi_v3_11_2::json;

    json j;
    try {
        j = json::parse(body);
    } catch (const std::exception& e) {
        m_cluster.log(
            dpp::ll_warning,
            std::string("Failed to parse Lavalink /loadtracks response: ") + e.what()
        );
        res.type = load_type::error;
        res.error_message = "Failed to parse Lavalink response";
        return res;
    }

    const std::string load_type_str = j.value("loadType", "");

    if (load_type_str == "track") {
        res.type = load_type::track;
    } else if (load_type_str == "search") {
        res.type = load_type::search;
    } else if (load_type_str == "playlist") {
        res.type = load_type::playlist;
    } else if (load_type_str == "empty") {
        res.type = load_type::empty;
    } else if (load_type_str == "error") {
        res.type = load_type::error;
        if (j.contains("data") && j["data"].is_object()) {
            const auto& data = j["data"];
            res.error_message = data.value("message", "Unknown Lavalink error");
        } else {
            res.error_message = "Unknown Lavalink error (no data field)";
        }
    } else {
        res.type = load_type::error;
        res.error_message = "Unknown loadType: " + load_type_str;
    }

    if (res.type == load_type::track ||
        res.type == load_type::search ||
        res.type == load_type::playlist)
    {
        const auto& data = j["data"];
        if (data.is_array()) {
            for (const auto& el : data) {
                track t;
                t.encoded = el.value("encoded", "");

                if (el.contains("info") && el["info"].is_object()) {
                    const auto& info = el["info"];
                    t.title      = info.value("title", "");
                    t.author     = info.value("author", "");
                    t.length_ms  = info.value("length", 0);
                    t.uri        = info.value("uri", "");
                }

                res.tracks.push_back(std::move(t));
            }
        }
    }

    if (res.type == load_type::error) {
        m_cluster.log(
            dpp::ll_warning,
            "Lavalink /loadtracks error for identifier '" + identifier +
            "': " + res.error_message
        );
    } else {
        std::ostringstream oss;
        oss << "Loaded " << res.tracks.size()
            << " track(s) from Lavalink for identifier: " << identifier;
        m_cluster.log(dpp::ll_info, oss.str());
    }

    return res;
}

bool node::send_player_update(dpp::snowflake guild_id,
                              const std::string& body_json,
                              bool log_payload)
{
    if (m_session_id.empty()) {
        m_cluster.log(
            dpp::ll_warning,
            "Cannot send player update: session id is empty"
        );
        return false;
    }

    std::string path = "/v4/sessions/" + m_session_id +
                       "/players/" + guild_id.str();

    if (log_payload) {
        std::ostringstream oss;
        oss << "Sending player update to Lavalink for guild "
            << guild_id << ": " << body_json;
        m_cluster.log(dpp::ll_debug, oss.str());
    }

    std::string resp = http_request("PATCH", path, body_json);
    // http_request already logged status; treat non-empty body as "sent"
    return !resp.empty();
}

bool node::play(dpp::snowflake guild_id,
                const std::string& encoded_track,
                bool no_replace,
                std::optional<long int> start_position_ms,
                std::optional<int> volume)
{
    using json = nlohmann::json_abi_v3_11_2::json;

    json payload;
    payload["encodedTrack"] = encoded_track;
    payload["noReplace"]    = no_replace;

    if (start_position_ms.has_value()) {
        payload["position"] = *start_position_ms;
    }
    if (volume.has_value()) {
        payload["volume"] = *volume;
    }

    std::optional<voice_state> vs;
    {
        std::lock_guard<std::mutex> lock(m_voice_mutex);
        vs = get_voice_state_locked(guild_id);
    }

    if (vs.has_value()) {
        payload["voice"]["token"]     = vs->token;
        payload["voice"]["endpoint"]  = vs->endpoint;
        payload["voice"]["sessionId"] = vs->session_id;
    }

    std::ostringstream oss;
    oss << "Sending play to Lavalink for guild " << guild_id
        << " (noReplace=" << std::boolalpha << no_replace << ")";
    m_cluster.log(dpp::ll_info, oss.str());

    return send_player_update(guild_id, payload.dump(), /*log_payload=*/true);
}

bool node::stop(dpp::snowflake guild_id)
{
    using json = nlohmann::json_abi_v3_11_2::json;

    json payload;
    payload["encodedTrack"] = nullptr;

    m_cluster.log(
        dpp::ll_info,
        "Sending stop to Lavalink for guild " + guild_id.str()
    );

    return send_player_update(guild_id, payload.dump(), /*log_payload=*/true);
}

bool node::pause(dpp::snowflake guild_id, bool pause_flag)
{
    using json = nlohmann::json_abi_v3_11_2::json;

    json payload;
    payload["pause"] = pause_flag;

    std::ostringstream oss;
    oss << "Sending pause=" << std::boolalpha << pause_flag
        << " to Lavalink for guild " << guild_id;
    m_cluster.log(dpp::ll_info, oss.str());

    return send_player_update(guild_id, payload.dump(), /*log_payload=*/true);
}

bool node::set_volume(dpp::snowflake guild_id, int volume_percent)
{
    using json = nlohmann::json_abi_v3_11_2::json;

    json payload;
    payload["volume"] = volume_percent;

    std::ostringstream oss;
    oss << "Sending volume=" << volume_percent
        << " to Lavalink for guild " << guild_id;
    m_cluster.log(dpp::ll_info, oss.str());

    return send_player_update(guild_id, payload.dump(), /*log_payload=*/true);
}

} // namespace fc::lavalink
