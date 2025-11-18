#pragma once

#include <dpp/dpp.h>

#include <mutex>
#include <unordered_map>
#include <optional>
#include <vector>
#include <string>

namespace fc::lavalink {

struct node_config {
    std::string host       = "127.0.0.1";
    uint16_t    port       = 2333;
    bool        https      = false;
    std::string password   = "youshallnotpass";
    std::string session_id = "default";
};

struct track {
    std::string encoded;
    std::string title;
    std::string author;
    int64_t     length_ms = 0;
    std::string uri;
};

enum class load_type {
    track,
    playlist,
    search,
    empty,
    error
};

struct load_result {
    load_type           type = load_type::empty;
    std::vector<track>  tracks;
    std::string         error_message;
};

class node {
public:
    node(dpp::cluster& cluster, const node_config& cfg);

    void handle_voice_state_update(const dpp::voice_state_update_t& ev);
    void handle_voice_server_update(const dpp::voice_server_update_t& ev);

    load_result load_tracks(const std::string& identifier) const;

    bool play(dpp::snowflake guild_id,
              const std::string& encoded_track,
              bool no_replace,
              std::optional<long int> start_position_ms,
              std::optional<int> volume);

    bool stop(dpp::snowflake guild_id);
    bool pause(dpp::snowflake guild_id, bool pause_flag);
    bool set_volume(dpp::snowflake guild_id, int volume_percent);

private:
    struct voice_state {
        std::string token;
        std::string endpoint;
        std::string session_id;
    };

    dpp::cluster& m_cluster;
    node_config   m_cfg;
    std::string   m_session_id;

    mutable std::mutex m_voice_mutex;
    std::unordered_map<dpp::snowflake, voice_state> m_voice_states;

    std::optional<voice_state> get_voice_state_locked(dpp::snowflake guild_id) const;

    std::string http_request(const std::string& method,
                             const std::string& urlpath,
                             const std::string& body_json) const;

    // NOTE: takes a JSON STRING here, not a json object
    bool send_player_update(dpp::snowflake guild_id,
                            const std::string& body_json,
                            bool log_payload);
};

} // namespace fc::lavalink
