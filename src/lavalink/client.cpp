#include "fc/lavalink/client.hpp"

#include <dpp/dpp.h>
#include <dpp/utility.h>
#include <dpp/nlohmann/json.hpp>

#include <sstream>
#include <future>

namespace fc::lavalink {

using dpp::snowflake;
using json = fc::lavalink::json;

node::node(dpp::cluster& cluster, const node_config& cfg)
    : m_cluster(cluster)
    , m_cfg(cfg)
{
    if (m_cfg.session_id.empty()) {
        m_cfg.session_id = "default";
    }
    m_session_id = m_cfg.session_id;

    std::ostringstream oss;
    oss << "[Lavalink] Using host=" << (m_cfg.https ? "https://" : "http://")
        << m_cfg.host << ":" << m_cfg.port
        << " session_id=" << m_session_id;
    m_cluster.log(dpp::ll_info, oss.str());

    // Make sure the Lavalink v4 session exists
    ensure_session();
}

void node::ensure_session() {
    if (m_session_id.empty()) {
        m_session_id = m_cfg.session_id.empty() ? "default" : m_cfg.session_id;
    }

    json payload;
    payload["resuming"] = false; // change to true if you want resume behaviour
    payload["timeout"]  = 60;    // seconds; adjust as you like

    std::string path = "/v4/sessions/" + m_session_id;

    std::ostringstream oss;
    oss << "[Lavalink] Ensuring session '" << m_session_id
        << "' via PATCH " << path;
    m_cluster.log(dpp::ll_debug, oss.str());

    std::string body = http_request("PATCH", path, payload.dump());
    if (body.empty()) {
        m_cluster.log(dpp::ll_warning,
                      "[Lavalink] Session ensure returned empty body. Check Lavalink logs/password.");
    } else {
        m_cluster.log(dpp::ll_info,
                      "[Lavalink] Session '" + m_session_id + "' is ready.");
    }
}

void node::handle_voice_state_update(const dpp::voice_state_update_t& ev) {
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

void node::handle_voice_server_update(const dpp::voice_server_update_t& ev) {
    std::lock_guard<std::mutex> lock(m_voice_mutex);
    auto& vs = m_voice_states[ev.guild_id];
    vs.token          = ev.token;
    vs.endpoint       = ev.endpoint;
    vs.has_server_info = true;

    std::ostringstream oss;
    oss << "Cached voice_server for guild " << ev.guild_id
        << " token=<set> endpoint=" << vs.endpoint;
    m_cluster.log(dpp::ll_debug, oss.str());
}

std::optional<node::voice_state> node::get_voice_state_locked(dpp::snowflake guild_id) const {
    auto it = m_voice_states.find(guild_id);
    if (it == m_voice_states.end()) {
        return std::nullopt;
    }
    const voice_state& vs = it->second;
    if (vs.session_id.empty() || !vs.has_server_info) {
        return std::nullopt;
    }
    return vs;
}

std::string node::http_request(const std::string& method,
                               const std::string& urlpath,
                               const std::string& body_json) const
{
    dpp::http_method http_method_enum = dpp::m_get;
    if (method == "GET")         http_method_enum = dpp::m_get;
    else if (method == "POST")   http_method_enum = dpp::m_post;
    else if (method == "PATCH")  http_method_enum = dpp::m_patch;
    else if (method == "DELETE") http_method_enum = dpp::m_delete;
    else if (method == "PUT")    http_method_enum = dpp::m_put;

    const std::string scheme = m_cfg.https ? "https://" : "http://";
    std::string full_url = scheme + m_cfg.host + ":" + std::to_string(m_cfg.port) + urlpath;

    dpp::http_headers headers{
        {"Authorization", m_cfg.password},
        {"User-Agent", "FreechasersBot-Lavalink/1.0"},
        {"Accept", "application/json"}
    };

    auto prom = std::make_shared<std::promise<dpp::http_request_completion_t>>();
    auto fut  = prom->get_future();

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
        [this, method, urlpath, full_url, prom](const dpp::http_request_completion_t& cc) {
            std::ostringstream oss;
            oss << "Lavalink HTTP " << cc.status
                << " on " << method << " " << urlpath
                << " (URL=" << full_url
                << ", response length=" << cc.body.size() << ")";
            m_cluster.log(dpp::ll_debug, oss.str());
            prom->set_value(cc);
        },
        body_json,
        body_json.empty() ? "" : "application/json",
        headers
    );

    dpp::http_request_completion_t cc = fut.get();

    if (cc.status == 0) {
        std::ostringstream warn;
        warn << "Lavalink HTTP status 0 (request failed) on "
             << method << " " << urlpath;
        m_cluster.log(dpp::ll_warning, warn.str());
        return {};
    }

    // non-2xx is still returned so caller can inspect error JSON
    return cc.body;
}

load_result node::load_tracks(const std::string& identifier) const {
    load_result out;

    std::string path = "/v4/loadtracks?identifier=" +
                       dpp::utility::url_encode(identifier);

    {
        std::ostringstream oss;
        oss << "Requesting " << path << " for identifier: " << identifier;
        m_cluster.log(dpp::ll_debug, oss.str());
    }

    std::string body = http_request("GET", path, "");
    if (body.empty()) {
        std::ostringstream warn;
        warn << "Empty response from Lavalink /loadtracks for identifier: " << identifier;
        m_cluster.log(dpp::ll_warning, warn.str());
        return out; // type stays 'empty'
    }

    json j;
    try {
        j = json::parse(body);
    } catch (const std::exception& e) {
        out.type = load_type::error;
        out.error_message = std::string("Failed to parse JSON from Lavalink /loadtracks: ") + e.what();
        return out;
    }

    std::string lt = j.value("loadType", "empty");
    if (lt == "track")      out.type = load_type::track;
    else if (lt == "playlist") out.type = load_type::playlist;
    else if (lt == "search")   out.type = load_type::search;
    else if (lt == "empty")    out.type = load_type::empty;
    else if (lt == "error")    out.type = load_type::error;
    else                       out.type = load_type::error;

    if (out.type == load_type::error) {
        if (j.contains("data") && j["data"].contains("message")) {
            out.error_message = j["data"]["message"].get<std::string>();
        } else {
            out.error_message = "Unknown Lavalink load error";
        }
        return out;
    }

    if (!j.contains("data") || !j["data"].is_array()) {
        return out;
    }

    for (const auto& item : j["data"]) {
        track t;
        t.encoded = item.value("encoded", "");

        if (item.contains("info")) {
            const auto& info = item["info"];
            t.title     = info.value("title", "");
            t.author    = info.value("author", "");
            t.uri       = info.value("uri", "");
            t.length    = info.value("length", 0LL);
            t.is_stream = info.value("isStream", false);
        }

        out.tracks.push_back(std::move(t));
    }

    {
        std::ostringstream oss;
        oss << "Loaded " << out.tracks.size()
            << " track(s) from Lavalink for identifier: " << identifier;
        m_cluster.log(dpp::ll_info, oss.str());
    }

    return out;
}

bool node::send_player_update(dpp::snowflake guild_id,
                              const json& payload,
                              bool no_replace)
{
    if (m_session_id.empty()) {
        ensure_session();
        if (m_session_id.empty()) {
            m_cluster.log(dpp::ll_warning,
                          "Cannot send player update: no Lavalink session_id");
            return false;
        }
    }

    json full = payload;
    if (!full.contains("noReplace")) {
        full["noReplace"] = no_replace;
    }

    std::string path = "/v4/sessions/" + m_session_id +
                       "/players/" + guild_id.str();

    std::string body = http_request("PATCH", path, full.dump());
    if (body.empty()) {
        std::ostringstream oss;
        oss << "Lavalink player update returned empty body for guild " << guild_id;
        m_cluster.log(dpp::ll_warning, oss.str());
        return false;
    }

    return true;
}

bool node::play(dpp::snowflake guild_id,
                const std::string& encoded_track,
                bool no_replace,
                std::optional<long int> start_ms,
                std::optional<int> volume)
{
    json payload;
    payload["encodedTrack"] = encoded_track;

    if (start_ms.has_value()) {
        payload["position"] = *start_ms;
    }
    if (volume.has_value()) {
        payload["volume"] = *volume;
    }

    // Attach voice info if we have it
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

    {
        std::ostringstream oss;
        oss << "Sending play to Lavalink for guild " << guild_id
            << " (noReplace=" << (no_replace ? "true" : "false") << ")";
        m_cluster.log(dpp::ll_info, oss.str());
    }

    return send_player_update(guild_id, payload, no_replace);
}

bool node::stop(dpp::snowflake guild_id) {
    json payload;
    // In v4, setting encodedTrack to null stops playback
    payload["encodedTrack"] = nullptr;

    {
        std::ostringstream oss;
        oss << "Sending stop to Lavalink for guild " << guild_id;
        m_cluster.log(dpp::ll_info, oss.str());
    }

    return send_player_update(guild_id, payload, true);
}

bool node::pause(dpp::snowflake guild_id, bool pause_flag) {
    json payload;
    payload["paused"] = pause_flag;

    {
        std::ostringstream oss;
        oss << "Sending pause=" << (pause_flag ? "true" : "false")
            << " to Lavalink for guild " << guild_id;
        m_cluster.log(dpp::ll_info, oss.str());
    }

    return send_player_update(guild_id, payload, false);
}

bool node::set_volume(dpp::snowflake guild_id, int volume_percent) {
    json payload;
    payload["volume"] = volume_percent;

    {
        std::ostringstream oss;
        oss << "Sending volume=" << volume_percent
            << " to Lavalink for guild " << guild_id;
        m_cluster.log(dpp::ll_info, oss.str());
    }

    return send_player_update(guild_id, payload, false);
}

} // namespace fc::lavalink
