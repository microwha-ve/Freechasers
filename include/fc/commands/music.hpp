#pragma once

#include <dpp/dpp.h>
#include "fc/lavalink/client.hpp"

#include <deque>
#include <unordered_map>

namespace fc::music {

struct guild_state {
    std::deque<fc::lavalink::track> queue;
    bool paused = false;
};

/// Optional hook for future use.
void register_commands(dpp::cluster& bot);

/// Build the music slash commands: /play, /pause, /stop, /volume, /queue
std::vector<dpp::slashcommand> make_commands(dpp::cluster& bot);

/// New main entry: we need the cluster.
void route_slashcommand(const dpp::slashcommand_t& ev,
                        fc::lavalink::node& lavalink,
                        dpp::cluster& bot);

/// Backwards-compatible wrapper for existing call sites:
/// uses ev.from()->creator to obtain the cluster.
void route_slashcommand(const dpp::slashcommand_t& ev,
                        fc::lavalink::node& lavalink);

} // namespace fc::music
