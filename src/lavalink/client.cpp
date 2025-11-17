#include "fc/lavalink/client.hpp"

#include <dpp/utility.h>

#include <iomanip>
#include <sstream>

using namespace dpp;

namespace fc::lavalink {

// ---------- small helpers ----------

std::string node::to_string_snowflake(dpp::snowflake id) {
    return std::to_string(static_cast<std::uint64_t>(id));
}

std::string node::url_encode(const std::string& value) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex << std::uppercase;

    for (unsigned char c : value) {
        // Unreserved characters according to RFC 3986
        if (std::isalnum(c) ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
        } else {
            escaped << '%' << std::setw(2)
                    << static_cast<int>(c);
            escaped << std::setw(0);
        }
    }

    return escaped.str();
}

std::string node::json_escape(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 16); // small safety margin

    for (unsigned char c : value) {
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b";  break;
        case '\f': out += "\\f";  break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        default:
            if (c < 0x20) {
                // control chars → \u00XX
                std::ostringstream oss;
                oss << "\\u"
                    << std::hex << std::setw(4) << std::setfill('0')
                    << static_cast<int>(c);
                out += oss.str();
            } else {
                out += static_cast<char>(c);
            }
        }
    }

    return out;
}

// ---------- ctor / config ----------

node::node(dpp::cluster& cluster, node_config config)
    : m_cluster(cluster)
    , m_config(std::move(config)) {}

void node::set_lavalink_session_id(const std::string& id) {
    m_config.session_id = id;
}

const std::string& node::get_lavalink_session_id() const {
    return m_config.session_id;
}

// ---------- HTTP helper ----------

dpp::http_headers node::build_default_headers(bool json_body) const {
    dpp::http_headers headers;

    // Required by Lavalink
    headers.emplace("Authorization", m_config.password);
    headers.emplace("User-Id", to_string_snowflake(m_cluster.me.id));
    headers.emplace("Client-Name", m_config.client_name);

    if (json_body) {
        headers.emplace("Content-Type", "application/json");
    }

    return headers;
}

std::string node::http_request(const std::string& urlpath,
                               const std::string& method,
                               const std::string& body) const {
    // dpp::https_client is synchronous:
    // it performs the request in the constructor and then we can read status/body.
    const bool plaintext = !m_config.https;

    https_client client(
        m_config.host,
        m_config.port,
        urlpath,
        method,
        body,
        build_default_headers(!body.empty()),
        plaintext,
        5,         // timeout seconds
        "1.1"      // HTTP/1.1
    );

    const auto status = client.get_status();
    const auto content = client.get_content();

    if (status >= 400) {
        m_cluster.log(dpp::ll_warning, "Lavalink HTTP " + std::to_string(status) +
                                         " on " + method + " " + urlpath);
    }

    return content;
}

// ---------- Voice glue ----------

void node::handle_voice_state_update(const dpp::voice_state_update_t& event) {
    // ignore other users; we only care about this bot
    if (event.state.user_id != m_cluster.me.id) {
        return;
    }

    if (event.state.guild_id == 0) {
        return;
    }

    auto& data = m_voice_state[event.state.guild_id];
    data.session_id = event.state.session_id;

    // If the bot left voice, you may want to clear or send a voice null update.
    if (!event.state.channel_id) {
        // For now we simply drop stored state.
        m_voice_state.erase(event.state.guild_id);
        return;
    }

    send_voice_update(event.state.guild_id);
}

void node::handle_voice_server_update(const dpp::voice_server_update_t& event) {
    if (!event.guild_id) {
        return;
    }

    auto& data = m_voice_state[event.guild_id];
    data.token = event.token;
    data.endpoint = event.endpoint;

    send_voice_update(event.guild_id);
}

void node::send_voice_update(dpp::snowflake guild_id) {
    auto it = m_voice_state.find(guild_id);
    if (it == m_voice_state.end()) {
        return;
    }

    const auto& vs = it->second;

    if (vs.token.empty() || vs.endpoint.empty() || vs.session_id.empty()) {
        // We need all three before we can send to Lavalink
        return;
    }

    // PATCH /v4/sessions/{sessionId}/players/{guildId}
    std::string url = "/v4/sessions/" + m_config.session_id
                    + "/players/" + to_string_snowflake(guild_id);

    std::string body = "{"
                       "\"voice\":{"
                       "\"token\":\""    + json_escape(vs.token) + "\","
                       "\"endpoint\":\"" + json_escape(vs.endpoint) + "\","
                       "\"sessionId\":\""+ json_escape(vs.session_id) + "\""
                       "}"
                       "}";

    http_request(url, "PATCH", body);
}

// ---------- Track loading ----------

std::string node::load_tracks_raw(const std::string& identifier) const {
    const std::string encoded = url_encode(identifier);
    const std::string url =
        "/v4/loadtracks?identifier=" + encoded;

    return http_request(url, "GET");
}

std::vector<track> node::load_tracks(const std::string& identifier) const {
    std::vector<track> result;
    const std::string raw = load_tracks_raw(identifier);

    if (raw.empty()) {
        return result;
    }

    dpp::json j = dpp::json::parse(raw, nullptr, false);
    if (j.is_discarded()) {
        m_cluster.log(dpp::ll_warning, "Failed to parse Lavalink /loadtracks JSON");
        return result;
    }

    dpp::json tracks_json;

    // Lavalink v3: top-level array
    if (j.is_array()) {
        tracks_json = j;
    }
    // Lavalink v4: top-level object with 'data' or 'tracks'
    else if (j.contains("tracks")) {
        tracks_json = j["tracks"];
    } else if (j.contains("data")) {
        if (j["data"].is_array()) {
            tracks_json = j["data"];
        } else if (j["data"].contains("tracks")) {
            tracks_json = j["data"]["tracks"];
        }
    }

    if (!tracks_json.is_array()) {
        return result;
    }

    for (const auto& t : tracks_json) {
        if (!t.is_object()) {
            continue;
        }

        track out;

        if (t.contains("encoded") && t["encoded"].is_string()) {
            out.encoded = t["encoded"].get<std::string>();
        }

        const dpp::json* info = nullptr;

        if (t.contains("info") && t["info"].is_object()) {
            info = &t["info"];
        } else if (t.contains("track") && t["track"].is_object()) {
            // some clients use 'track' instead of 'info'
            info = &t["track"];
        }

        if (info) {
            if (info->contains("title") && (*info)["title"].is_string()) {
                out.title = (*info)["title"].get<std::string>();
            }
            if (info->contains("author") && (*info)["author"].is_string()) {
                out.author = (*info)["author"].get<std::string>();
            }
            if (info->contains("uri") && (*info)["uri"].is_string()) {
                out.uri = (*info)["uri"].get<std::string>();
            }
            if (info->contains("length") && (*info)["length"].is_number_integer()) {
                out.length_ms = (*info)["length"].get<std::int64_t>();
            }
        }

        // encoded is the only thing strictly required to play
        if (!out.encoded.empty()) {
            result.push_back(std::move(out));
        }
    }

    return result;
}

// ---------- Player controls ----------

void node::play(dpp::snowflake guild_id,
                const std::string& encoded_track,
                bool no_replace) const {
    std::string url = "/v4/sessions/" + m_config.session_id
                    + "/players/" + to_string_snowflake(guild_id);

    std::ostringstream body;
    body << "{"
         << "\"track\":{"
         << "\"encoded\":\"" << json_escape(encoded_track) << "\""
         << "}"
         << ",\"noReplace\":" << (no_replace ? "true" : "false")
         << "}";

    http_request(url, "PATCH", body.str());
}

void node::stop(dpp::snowflake guild_id) const {
    std::string url = "/v4/sessions/" + m_config.session_id
                    + "/players/" + to_string_snowflake(guild_id);

    // stopping: send track = null
    std::string body = "{\"track\":null}";

    http_request(url, "PATCH", body);
}

void node::pause(dpp::snowflake guild_id, bool pause) const {
    std::string url = "/v4/sessions/" + m_config.session_id
                    + "/players/" + to_string_snowflake(guild_id);

    std::string body = std::string("{\"pause\":")
                     + (pause ? "true" : "false") + "}";

    http_request(url, "PATCH", body);
}

void node::set_volume(dpp::snowflake guild_id, int volume) const {
    if (volume < 0) volume = 0;
    if (volume > 1000) volume = 1000;

    std::string url = "/v4/sessions/" + m_config.session_id
                    + "/players/" + to_string_snowflake(guild_id);

    std::string body = "{\"volume\":" + std::to_string(volume) + "}";

    http_request(url, "PATCH", body);
}

void node::seek(dpp::snowflake guild_id, std::int64_t position_ms) const {
    if (position_ms < 0) {
        position_ms = 0;
    }

    std::string url = "/v4/sessions/" + m_config.session_id
                    + "/players/" + to_string_snowflake(guild_id);

    std::string body = "{\"position\":" + std::to_string(position_ms) + "}";

    http_request(url, "PATCH", body);
}

} // namespace fc::lavalink
