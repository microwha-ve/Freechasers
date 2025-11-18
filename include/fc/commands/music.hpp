#pragma once

#include <dpp/dpp.h>
#include <unordered_map>
#include <deque>

#include "fc/lavalink/client.hpp"

namespace fc::music {

struct guild_music_state {
    std::deque<fc::lavalink::track> queue;
    bool playing = false;
    int volume = 100;
};

/// Global per-guild music state (defined in music.cpp)
extern std::unordered_map<dpp::snowflake, guild_music_state> g_states;

/// Legacy hook – safe to keep even if it does nothing.
void register_commands(dpp::cluster& bot);

/// Build the music slash commands (/play, /pause, /stop, /volume, /queue).
std::vector<dpp::slashcommand> make_commands(dpp::cluster& bot);

/// Dispatch a music slash command to the correct handler.
void route_slashcommand(const dpp::slashcommand_t& ev,
                        fc::lavalink::node& lavalink);

} // namespace fc::music
