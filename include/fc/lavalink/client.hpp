#pragma once

#include <dpp/dpp.h>

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace fc::lavalink {

struct node_config {
    std::string host = "127.0.0.1";
    std::uint16_t port = 2333;
    bool https = false;
    std::string password;
    // For now this is taken from your config (e.g. "default").
    // A proper Lavalink v4 client should update this from the READY websocket op.
    std::string session_id;
};

struct track {
    std::string encoded;
    std::string title;
    std::string author;
    std::string uri;
    std::int64_t length_ms = 0;
};

enum class load_type {
    empty,
    track,
    search,
    playlist,
    error,
};

struct load_result {
    load_type type = load_type::empty;
    std::vector<track> tracks;
    std::string error_message;
};

class node {
public:
    node(dpp::cluster& cluster, const node_config& cfg);

    // DPP voice glue – call these from your handlers:
    //  bot.on_voice_state_update(...)
    //  bot.on_voice_server_update(...)
    void handle_voice_state_update(const dpp::voice_state_update_t& ev);
    void handle_voice_server_update(const dpp::voice_server_update_t& ev);

    // /v4/loadtracks wrapper
    load_result load_tracks(const std::string& identifier) const;

    // Player controls – PATCH /v4/sessions/{sessionId}/players/{guildId}
    bool play(dpp::snowflake guild_id,
              const std::string& encoded_track,
              bool no_replace,
              std::optional<std::int64_t> start_position_ms = std::nullopt,
              std::optional<int> volume = std::nullopt);

    bool stop(dpp::snowflake guild_id);
    bool pause(dpp::snowflake guild_id, bool pause);
    bool set_volume(dpp::snowflake guild_id, int volume_percent);

    const std::string& session_id() const noexcept { return m_session_id; }
    void set_session_id(const std::string& id) { m_session_id = id; }

private:
    struct voice_state {
        std::string token;
        std::string endpoint;
        std::string session_id;
    };

    // Synchronous HTTP helper (safe, with timeout and logging)
    std::string http_request(const std::string& method,
                             const std::string& urlpath,
                             const std::string& body_json) const;

    // Internal helper to send player PATCH
    bool send_player_update(dpp::snowflake guild_id,
                            const nlohmann::json& payload,
                            bool no_replace);

    std::optional<voice_state> get_voice_state_locked(dpp::snowflake guild_id) const;

    dpp::cluster& m_cluster;
    node_config   m_cfg;
    std::string   m_session_id;

    mutable std::mutex m_voice_mutex;
    std::unordered_map<dpp::snowflake, voice_state> m_voice_states;
};

} // namespace fc::lavalink
