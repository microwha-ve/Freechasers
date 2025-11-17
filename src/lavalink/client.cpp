include "fc/lavalink/client.hpp"

#include <dpp/utility.h>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <sstream>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace fc::lavalink {

node::node(dpp::cluster& cluster, const node_config& cfg)
    : m_cluster(cluster)
    , m_cfg(cfg)
    , m_session_id(cfg.session_id)
{
    std::ostringstream oss;
    oss << "Initialising Lavalink node: host=" << m_cfg.host
        << " port=" << m_cfg.port
        << " https=" << (m_cfg.https ? "true" : "false")
        << " session_id='" << m_session_id << "'";
    m_cluster.log(dpp::ll_info, oss.str());
}

// Voice state from Discord → cache it for later player updates
void node::handle_voice_state_update(const dpp::voice_state_update_t& ev) {
    // Only care about our own bot's voice state
    if (ev.state.user_id != m_cluster.me.id) {
        return;
    }

    std::lock_guard<std::mutex> lock(m_voice_mutex);
    auto& vs = m_voice_states[ev.state.guild_id];
    vs.session_id = ev.state.session_id;

    std::ostringstream oss;
    oss << "Cached voice_state for guild " << ev.state.guild_id
        << " (sessionId=" << vs.session_id << ")";
    m_cluster.log(dpp::ll_debug, oss.str());
}

void node::handle_voice_server_update(const dpp::voice_server_update_t& ev) {
    std::lock_guard<std::mutex> lock(m_voice_mutex);
    auto& vs = m_voice_states[ev.guild_id];
    vs.token    = ev.token;
    vs.endpoint = ev.endpoint;

    std::ostringstream oss;
    oss << "Cached voice_server for guild " << ev.guild_id
        << " (endpoint=" << vs.endpoint << ")";
    m_cluster.log(dpp::ll_debug, oss.str());
}

std::optional<node::voice_state> node::get_voice_state_locked(dpp::snowflake guild_id) const {
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

// Synchronous HTTP helper using dpp::cluster::request
std::string node::http_request(const std::string& method,
                               const std::string& urlpath,
                               const std::string& body_json) const
{
    dpp::http_method http_method_enum = dpp::m_get;
    if (method == "POST")
        http_method_enum = dpp::m_post;
    else if (method == "PATCH")
        http_method_enum = dpp::m_patch;
    else if (method == "DELETE")
        http_method_enum = dpp::m_delete;
    else if (method == "PUT")
        http_method_enum = dpp::m_put;

    const std::string scheme   = m_cfg.https ? "https://" : "http://";
    const std::string hostport = m_cfg.host + ":" + std::to_string(m_cfg.port);
    const std::string full_url = scheme + hostport + urlpath;

    dpp::http_headers headers{
        {"Authorization", m_cfg.password},
        {"Accept",       "application/json"},
    };

    std::mutex              m;
    std::condition_variable cv;
    bool                    done   = false;
    std::uint16_t           status = 0;
    std::string             response_body;

    {
        std::ostringstream oss;
        oss << "Lavalink HTTP request: " << method << " " << urlpath
            << " (URL=" << full_url << ", body="
            << (body_json.empty() ? "empty" : std::to_string(body_json.size()) + " bytes")
            << ")";
        m_cluster.log(dpp::ll_debug, oss.str());
    }

    m_cluster.request(
        full_url,
        http_method_enum,
        [&, method, urlpath, full_url](const dpp::http_request_completion_t& cc) {
            {
                std::lock_guard<std::mutex> lock(m);
                status        = cc.status;
                response_body = cc.body;
                done          = true;
            }

            std::ostringstream oss;
            oss << "Lavalink HTTP " << cc.status
                << " on " << method << " " << urlpath
                << " (URL=" << full_url
                << ", response length=" << cc.body.size() << ")";
            m_cluster.log(
                (cc.status >= 200 && cc.status < 300) ? dpp::ll_debug : dpp::ll_warning,
                oss.str()
            );

            cv.notify_one();
        },
        body_json,
        body_json.empty() ? "" : "application/json",
        headers
    );

    std::unique_lock<std::mutex> lock(m);
    if (!cv.wait_for(lock, std::chrono::seconds(10), [&] { return done; })) {
        std::ostringstream oss;
        oss << "Lavalink HTTP request to " << full_url << " timed out";
        m_cluster.log(dpp::ll_warning, oss.str());
        return {};
    }

    if (status == 0) {
        std::ostringstream oss;
        oss << "Lavalink HTTP status 0 (request failed) on " << method << " " << urlpath;
        m_cluster.log(dpp::ll_warning, oss.str());
        return {};
    }

    return response_body;
}

// /v4/loadtracks wrapper
load_result node::load_tracks(const std::string& identifier) const {
    load_result result;

    const std::string encoded = dpp::utility::url_encode(identifier);
    const std::string path    = "/v4/loadtracks?identifier=" + encoded;

    const std::string body = http_request("GET", path, "");
    if (body.empty()) {
        result.type          = load_type::empty;
        result.error_message = "Empty response from Lavalink /loadtracks";
        m_cluster.log(dpp::ll_warning, result.error_message);
        return result;
    }

    json j;
    try {
        j = json::parse(body);
    } catch (const std::exception& e) {
        std::ostringstream oss;
        oss << "Failed to parse /loadtracks response: " << e.what();
        m_cluster.log(dpp::ll_warning, oss.str());
        result.type          = load_type::error;
        result.error_message = oss.str();
        return result;
    }

    const std::string lt = j.value("loadType", std::string("empty"));
    if (lt == "track") {
        result.type = load_type::track;
        const auto& t = j["data"];
        track tr;
        tr.encoded    = t.value("encoded", std::string{});
        const auto& info = t["info"];
        tr.title      = info.value("title", std::string{});
        tr.author     = info.value("author", std::string{});
        tr.uri        = info.value("uri", std::string{});
        tr.length_ms  = info.value("length", 0LL);
        result.tracks.push_back(std::move(tr));
    } else if (lt == "search") {
        result.type = load_type::search;
        for (const auto& t : j["data"]) {
            track tr;
            tr.encoded    = t.value("encoded", std::string{});
            const auto& info = t["info"];
            tr.title      = info.value("title", std::string{});
            tr.author     = info.value("author", std::string{});
            tr.uri        = info.value("uri", std::string{});
            tr.length_ms  = info.value("length", 0LL);
            result.tracks.push_back(std::move(tr));
        }
    } else if (lt == "playlist") {
        result.type = load_type::playlist;
        for (const auto& t : j["data"]["tracks"]) {
            track tr;
            tr.encoded    = t.value("encoded", std::string{});
            const auto& info = t["info"];
            tr.title      = info.value("title", std::string{});
            tr.author     = info.value("author", std::string{});
            tr.uri        = info.value("uri", std::string{});
            tr.length_ms  = info.value("length", 0LL);
            result.tracks.push_back(std::move(tr));
        }
    } else if (lt == "empty") {
        result.type = load_type::empty;
    } else if (lt == "error") {
        result.type = load_type::error;
        if (j.contains("data")) {
            result.error_message = j["data"].value("message", std::string{"Unknown Lavalink error"});
        } else {
            result.error_message = "Unknown Lavalink error (no data field)";
        }
        m_cluster.log(dpp::ll_warning, "Lavalink loadtracks error: " + result.error_message);
    } else {
        result.type          = load_type::error;
        result.error_message = "Unknown loadType from Lavalink: " + lt;
        m_cluster.log(dpp::ll_warning, result.error_message);
    }

    std::ostringstream oss;
    oss << "Loaded " << result.tracks.size() << " track(s) from Lavalink for identifier: " << identifier;
    m_cluster.log(dpp::ll_info, oss.str());

    return result;
}

// Internal helper for PATCH /players
bool node::send_player_update(dpp::snowflake guild_id,
                              const json& payload,
                              bool no_replace)
{
    if (m_session_id.empty()) {
        m_cluster.log(dpp::ll_warning,
                      "Cannot send player update: session_id is empty. "
                      "Lavalink v4 requires a valid sessionId from the READY websocket op.");
        return false;
    }

    std::string path = "/v4/sessions/" + m_session_id +
                       "/players/" + guild_id.str() +
                       (no_replace ? "?noReplace=true" : "?noReplace=false");

    const std::string body = http_request("PATCH", path, payload.dump());
    if (body.empty()) {
        // http_request already logged details; just mark as failure here
        return false;
    }

    // Successful HTTP (2xx/4xx) responses still return a body. For detailed
    // error info, check logs produced by http_request.
    return true;
}

bool node::play(dpp::snowflake guild_id,
                const std::string& encoded_track,
                bool no_replace,
                std::optional<std::int64_t> start_position_ms,
                std::optional<int> volume)
{
    json payload;

    // New-style v4 track object
    payload["track"]["encoded"] = encoded_track;

    if (start_position_ms.has_value()) {
        payload["position"] = *start_position_ms;
    }
    if (volume.has_value()) {
        payload["volume"] = *volume;
    }

    // Attach voice state if we have a complete triple
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

    return send_player_update(guild_id, payload, no_replace);
}

bool node::stop(dpp::snowflake guild_id) {
    json payload;
    // As per v4 docs, null encoded track stops playback.
    payload["track"] = nullptr;

    std::ostringstream oss;
    oss << "Sending stop to Lavalink for guild " << guild_id;
    m_cluster.log(dpp::ll_info, oss.str());

    return send_player_update(guild_id, payload, false);
}

bool node::pause(dpp::snowflake guild_id, bool pause_flag) {
    json payload;
    payload["paused"] = pause_flag;

    std::ostringstream oss;
    oss << "Sending pause=" << (pause_flag ? "true" : "false")
        << " to Lavalink for guild " << guild_id;
    m_cluster.log(dpp::ll_info, oss.str());

    return send_player_update(guild_id, payload, false);
}

bool node::set_volume(dpp::snowflake guild_id, int volume_percent) {
    json payload;
    payload["volume"] = volume_percent;

    std::ostringstream oss;
    oss << "Sending volume=" << volume_percent
        << " to Lavalink for guild " << guild_id;
    m_cluster.log(dpp::ll_info, oss.str());

    return send_player_update(guild_id, payload, false);
}

} // namespace fc::lavalink
