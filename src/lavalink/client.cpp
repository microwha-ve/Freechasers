#include "fc/lavalink/client.hpp"

#include <sstream>
#include <future>

namespace fc::lavalink {

using json = dpp::json;

node::node(dpp::cluster& cluster, const node_config& cfg)
    : m_cluster(cluster)
    , m_cfg(cfg)
    , m_session_id(cfg.session_id)
{
    m_base_url = (m_cfg.https ? "https://" : "http://")
               + m_cfg.host + ":" + std::to_string(m_cfg.port);

    std::ostringstream oss;
    oss << "[Lavalink] Using host=" << m_base_url
        << " session_id=" << m_session_id;
    m_cluster.log(dpp::ll_info, oss.str());

    // IMPORTANT: do NOT call ensure_session() here.
    // It uses cluster.request() + std::future::get(), which must not run
    // before the bot is actually started. We call it from on_ready instead.
}

void node::handle_voice_state_update(const dpp::voice_state_update_t& ev) {
    if (ev.state.user_id != m_cluster.me.id) {
        return; // Only care about our own bot's voice state
    }

    std::lock_guard<std::mutex> lock(m_voice_mutex);
    auto& vs = m_voice_states[ev.state.guild_id];
    vs.guild_id   = ev.state.guild_id;
    vs.session_id = ev.state.session_id;

    std::ostringstream oss;
    oss << "Cached voice_state for guild " << ev.state.guild_id
        << " session_id=" << ev.state.session_id;
    m_cluster.log(dpp::ll_debug, oss.str());
}

void node::handle_voice_server_update(const dpp::voice_server_update_t& ev) {
    std::lock_guard<std::mutex> lock(m_voice_mutex);
    auto& vs = m_voice_states[ev.guild_id];
    vs.guild_id = ev.guild_id;
    vs.token    = ev.token;
    vs.endpoint = ev.endpoint;

    std::ostringstream oss;
    oss << "Cached voice_server for guild " << ev.guild_id
        << " token=<set> endpoint=" << ev.endpoint;
    m_cluster.log(dpp::ll_debug, oss.str());
}

std::optional<node::voice_state> node::get_voice_state_locked(dpp::snowflake guild_id) const {
    auto it = m_voice_states.find(guild_id);
    if (it == m_voice_states.end()) {
        return std::nullopt;
    }
    if (it->second.session_id.empty() || it->second.token.empty() || it->second.endpoint.empty()) {
        return std::nullopt;
    }
    return it->second;
}

std::string node::http_request(const std::string& method,
                               const std::string& urlpath,
                               const std::string& body_json) const
{
    // Map method string → dpp::http_method
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

    const std::string full_url = m_base_url + urlpath;

    dpp::http_headers headers{
        { "Authorization", m_cfg.password }
    };

    {
        std::ostringstream oss;
        oss << "Lavalink HTTP request: " << method << " " << urlpath
            << " (URL=" << full_url
            << ", body=" << (body_json.empty() ? "empty" : std::to_string(body_json.size()) + " bytes") << ")";
        m_cluster.log(dpp::ll_debug, oss.str());
    }

    std::promise<dpp::http_request_completion_t> prom;
    auto fut = prom.get_future();

    m_cluster.request(
        full_url,
        http_method_enum,
        [&, method, urlpath, full_url](const dpp::http_request_completion_t& cc) {
            std::ostringstream oss;
            oss << "Lavalink HTTP " << cc.status
                << " on " << method << " " << urlpath
                << " (URL=" << full_url
                << ", response length=" << cc.body.size() << ")";
            m_cluster.log(dpp::ll_debug, oss.str());
            try {
                prom.set_value(cc);
            } catch (...) {
                // ignore double set in weird edge cases
            }
        },
        body_json,
        body_json.empty() ? "" : "application/json",
        headers
    );

    dpp::http_request_completion_t cc{};
    try {
        cc = fut.get();
    } catch (const std::exception& e) {
        m_cluster.log(dpp::ll_warning,
                      std::string("Lavalink HTTP: exception waiting for response: ") + e.what());
        return {};
    }

    if (cc.status < 200 || cc.status >= 300) {
        std::ostringstream oss;
        oss << "Lavalink HTTP status " << cc.status
            << " (request failed) on " << method << " " << urlpath;
        m_cluster.log(dpp::ll_warning, oss.str());
        if (!cc.body.empty()) {
            m_cluster.log(dpp::ll_warning,
                          "Lavalink response: " + cc.body);
        }
        return {};
    }

    return cc.body;
}

void node::ensure_session() {
    json payload;
    payload["resuming"] = true;
    payload["timeout"]  = 60;

    std::string path = "/v4/sessions/" + m_session_id;

    m_cluster.log(dpp::ll_debug,
                  "[Lavalink] Ensuring session '" + m_session_id + "' via PATCH " + path);

    std::string body = http_request("PATCH", path, payload.dump());
    if (body.empty()) {
        m_cluster.log(dpp::ll_warning,
                      "[Lavalink] Failed to ensure session (empty response)");
        return;
    }

    m_cluster.log(dpp::ll_info,
                  "[Lavalink] Session '" + m_session_id + "' ensured/created");
}

load_result node::load_tracks(const std::string& identifier) const {
    load_result result;

    // URL-encode via DPP utility
    std::string encoded = dpp::utility::url_encode(identifier);
    std::string path = "/v4/loadtracks?identifier=" + encoded;

    m_cluster.log(dpp::ll_debug,
                  "Requesting /v4/loadtracks for identifier: " + identifier);

    std::string body = http_request("GET", path);
    if (body.empty()) {
        m_cluster.log(dpp::ll_warning,
                      "Empty response from Lavalink /loadtracks for identifier: " + identifier);
        return result;
    }

    json j;
    try {
        j = json::parse(body);
    } catch (const std::exception& e) {
        m_cluster.log(dpp::ll_warning,
                      std::string("Failed to parse Lavalink /loadtracks JSON: ") + e.what());
        return result;
    }

    if (!j.contains("loadType")) {
        m_cluster.log(dpp::ll_warning,
                      "Lavalink /loadtracks response missing 'loadType'");
        return result;
    }

    result.load_type = j.value("loadType", "");

    if (result.load_type == "empty" || result.load_type == "error") {
        m_cluster.log(dpp::ll_warning,
                      "Lavalink /loadtracks returned type=" + result.load_type);
        return result;
    }

    if (!j.contains("data")) {
        m_cluster.log(dpp::ll_warning,
                      "Lavalink /loadtracks response missing 'data'");
        return result;
    }

    const auto& data = j["data"];
    if (!data.is_array()) {
        m_cluster.log(dpp::ll_warning,
                      "Lavalink /loadtracks 'data' is not an array");
        return result;
    }

    for (const auto& item : data) {
        track t;
        if (item.contains("encoded") && item["encoded"].is_string()) {
            t.encoded = item["encoded"].get<std::string>();
        }
        if (item.contains("info") && item["info"].is_object()) {
            const auto& info = item["info"];
            t.title     = info.value("title", "");
            t.uri       = info.value("uri", "");
            t.author    = info.value("author", "");
            t.length_ms = info.value("length", 0LL);
        }
        result.tracks.push_back(std::move(t));
    }

    std::ostringstream oss;
    oss << "Loaded " << result.tracks.size()
        << " track(s) from Lavalink for identifier: " << identifier;
    m_cluster.log(dpp::ll_info, oss.str());

    return result;
}

bool node::send_player_update(dpp::snowflake guild_id,
                              const json& payload,
                              bool log_body)
{
    if (m_session_id.empty()) {
        m_cluster.log(dpp::ll_warning,
                      "[Lavalink] Cannot send player update: session_id is empty");
        return false;
    }

    std::string path = "/v4/sessions/" + m_session_id +
                       "/players/" + guild_id.str();

    if (log_body) {
        std::ostringstream oss;
        oss << "Sending player update to Lavalink for guild " << guild_id
            << ": " << payload.dump();
        m_cluster.log(dpp::ll_debug, oss.str());
    }

    std::string response_body = http_request("PATCH", path, payload.dump());
    if (response_body.empty()) {
        m_cluster.log(dpp::ll_warning,
                      "[Lavalink] Empty response for player update PATCH " + path);
        return false;
    }

    return true;
}

bool node::play(dpp::snowflake guild_id,
                const std::string& encoded_track,
                bool no_replace,
                std::optional<long int> start_ms,
                std::optional<int> volume_percent)
{
    json payload;
    payload["encodedTrack"] = encoded_track;
    payload["noReplace"]    = no_replace;

    if (start_ms.has_value()) {
        payload["position"] = *start_ms;
    }
    if (volume_percent.has_value()) {
        payload["volume"] = *volume_percent;
    }

    // Include voice data if we have it
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
        << " (noReplace=" << (no_replace ? "true" : "false") << ")";
    m_cluster.log(dpp::ll_info, oss.str());

    return send_player_update(guild_id, payload, true);
}

bool node::stop(dpp::snowflake guild_id) {
    json payload;
    payload["encodedTrack"] = nullptr; // stop track

    std::ostringstream oss;
    oss << "Sending stop to Lavalink for guild " << guild_id;
    m_cluster.log(dpp::ll_info, oss.str());

    return send_player_update(guild_id, payload, true);
}

bool node::pause(dpp::snowflake guild_id, bool pause_flag) {
    json payload;
    payload["pause"] = pause_flag;

    std::ostringstream oss;
    oss << "Sending pause=" << (pause_flag ? "true" : "false")
        << " to Lavalink for guild " << guild_id;
    m_cluster.log(dpp::ll_info, oss.str());

    return send_player_update(guild_id, payload, true);
}

bool node::set_volume(dpp::snowflake guild_id, int volume_percent) {
    json payload;
    payload["volume"] = volume_percent;

    std::ostringstream oss;
    oss << "Sending volume=" << volume_percent
        << " to Lavalink for guild " << guild_id;
    m_cluster.log(dpp::ll_info, oss.str());

    return send_player_update(guild_id, payload, true);
}

} // namespace fc::lavalink
