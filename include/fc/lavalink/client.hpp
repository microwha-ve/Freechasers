#pragma once

#include <dpp/dpp.h>

#include <string>
#include <vector>
#include <optional>
#include <mutex>
#include <unordered_map>

namespace fc::lavalink {

struct node_config {
    std::string host;
    uint16_t    port       = 2333;
    bool        https      = false;
    std::string password   = "youshallnotpass";
    std::string session_id = "default";
};

struct track {
    std::string encoded;
    std::string title;
    std::string uri;
    std::string author;
    long long   length_ms{};
};

struct load_result {
    std::string            load_type;
    std::vector<track>     tracks;
};

class node {
public:
    node(dpp::cluster& cluster, const node_config& cfg);

    // Called from main.cpp
    void handle_voice_state_update(const dpp::voice_state_update_t& ev);
    void handle_voice_server_update(const dpp::voice_server_update_t& ev);

    // Explicitly called from on_ready (so it runs AFTER bot.start)
    void ensure_session();

    // Lavalink operations
    load_result load_tracks(const std::string& identifier) const;

    bool play(dpp::snowflake guild_id,
              const std::string& encoded_track,
              bool no_replace = false,
              std::optional<long int> start_ms = std::nullopt,
              std::optional<int> volume_percent = std::nullopt);

    bool stop(dpp::snowflake guild_id);
    bool pause(dpp::snowflake guild_id, bool pause_flag);
    bool set_volume(dpp::snowflake guild_id, int volume_percent);

private:
    struct voice_state {
        dpp::snowflake guild_id{};
        std::string    token;
        std::string    endpoint;
        std::string    session_id;
    };

    dpp::cluster& m_cluster;
    node_config   m_cfg;
    std::string   m_base_url;
    std::string   m_session_id;

    mutable std::mutex m_voice_mutex;
    std::unordered_map<dpp::snowflake, voice_state> m_voice_states;

    using json = dpp::json;

    std::optional<voice_state> get_voice_state_locked(dpp::snowflake guild_id) const;

    std::string http_request(const std::string& method,
                             const std::string& urlpath,
                             const std::string& body_json = "") const;

    bool send_player_update(dpp::snowflake guild_id,
                            const json& payload,
                            bool log_body = true);
};

} // namespace fc::lavalink
