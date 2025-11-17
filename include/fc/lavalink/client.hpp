#pragma once

#include <dpp/dpp.h>
#include <dpp/httpsclient.h>
#include <dpp/json.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace fc::lavalink {

struct node_config {
    std::string host = "127.0.0.1";   // Lavalink host (no scheme)
    std::uint16_t port = 2333;        // Lavalink port
    bool https = false;               // true = https, false = http
    std::string password;             // Lavalink password (Authorization)
    std::string client_name = "FreechasersBot-Lavalink/0.1.0";
    std::string session_id = "default"; // Lavalink session id (path param)
};

struct track {
    std::string encoded;
    std::string title;
    std::string author;
    std::string uri;
    std::int64_t length_ms = 0;
};

class node {
public:
    node(dpp::cluster& cluster, node_config config);

    // Lavalink session id (not Discord voice session)
    void set_lavalink_session_id(const std::string& id);
    const std::string& get_lavalink_session_id() const;

    // /v4/loadtracks
    std::vector<track> load_tracks(const std::string& identifier) const;
    std::string load_tracks_raw(const std::string& identifier) const;

    // Player controls: PATCH /v4/sessions/{session}/players/{guild}
    void play(dpp::snowflake guild_id,
              const std::string& encoded_track,
              bool no_replace = false) const;

    void stop(dpp::snowflake guild_id) const;
    void pause(dpp::snowflake guild_id, bool pause) const;
    void set_volume(dpp::snowflake guild_id, int volume) const;         // 0–1000
    void seek(dpp::snowflake guild_id, std::int64_t position_ms) const; // >= 0

    // Voice glue: hook to D++ events
    void handle_voice_state_update(const dpp::voice_state_update_t& event);
    void handle_voice_server_update(const dpp::voice_server_update_t& event);

private:
    struct voice_state_data {
        std::string token;        // from voice_server_update
        std::string endpoint;     // from voice_server_update
        std::string session_id;   // Discord voice session id (from voice_state_update)
    };

    dpp::cluster& m_cluster;
    node_config m_config;
    std::unordered_map<dpp::snowflake, voice_state_data> m_voice_state; // by guild id

    dpp::http_headers build_default_headers(bool json_body) const;
    std::string http_request(const std::string& urlpath,
                             const std::string& method,
                             const std::string& body = "") const;

    void send_voice_update(dpp::snowflake guild_id);

    static std::string url_encode(const std::string& value);
    static std::string json_escape(const std::string& value);
    static std::string to_string_snowflake(dpp::snowflake id);
};

} // namespace fc::lavalink
