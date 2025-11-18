#pragma once

#include <string>
#include <vector>
#include <optional>
#include <mutex>
#include <unordered_map>
#include <cstdint>

#include <dpp/dpp.h>
#include <dpp/json_fwd.h>

namespace fc::lavalink {

// Use DPP's bundled nlohmann json type
using json = nlohmann::json_abi_v3_11_2::json;

struct node_config {
    std::string host;       // e.g. "127.0.0.1"
    uint16_t    port = 2333;
    bool        https = false;
    std::string password;   // Lavalink password
    std::string session_id; // Lavalink v4 session id, e.g. "default" or "freechasers"
};

struct track {
    std::string  encoded;
    std::string  title;
    std::string  author;
    std::string  uri;
    std::int64_t length    = 0;
    bool         is_stream = false;
};

enum class load_type {
    track,
    playlist,
    search,
    empty,
    error
};

struct load_result {
    load_type          type = load_type::empty;
    std::vector<track> tracks;
    std::string        error_message; // for load_type::error
};

class node {
public:
    explicit node(dpp::cluster& cluster, const node_config& cfg);

    // Hook these from your bot:
    void handle_voice_state_update(const dpp::voice_state_update_t& ev);
    void handle_voice_server_update(const dpp::voice_server_update_t& ev);

    // Track lookup
    load_result load_tracks(const std::string& identifier) const;

    // Player controls
    bool play(dpp::snowflake guild_id,
              const std::string& encoded_track,
              bool no_replace = false,
              std::optional<long int> start_ms = std::nullopt,
              std::optional<int> volume = std::nullopt);

    bool stop(dpp::snowflake guild_id);
    bool pause(dpp::snowflake guild_id, bool pause_flag);
    bool set_volume(dpp::snowflake guild_id, int volume_percent);

private:
    struct voice_state {
        std::string session_id;      // Discord voice session id
        std::string token;
        std::string endpoint;
        bool        has_server_info = false;
    };

    dpp::cluster& m_cluster;
    node_config   m_cfg;

    // Lavalink v4 session id (used in /v4/sessions/{sessionId}/players/...)
    std::string   m_session_id;

    mutable std::mutex m_voice_mutex;
    std::unordered_map<dpp::snowflake, voice_state> m_voice_states;

    std::string http_request(const std::string& method,
                             const std::string& urlpath,
                             const std::string& body_json = "") const;

    std::optional<voice_state> get_voice_state_locked(dpp::snowflake guild_id) const;

    bool send_player_update(dpp::snowflake guild_id,
                            const json& payload,
                            bool no_replace);

    // NEW: ensure the Lavalink v4 session exists (PATCH /v4/sessions/{sessionId})
    void ensure_session();
};

} // namespace fc::lavalink
