#pragma once

#include <dpp/dpp.h>
#include "fc/lavalink/client.hpp"

#include <vector>

namespace fc::music {

/**
 * Build all music slash commands:
 *   /play   query:string
 *   /pause
 *   /stop
 *   /volume level:int (0–1000)
 *   /queue
 */
std::vector<dpp::slashcommand> make_commands(dpp::cluster& bot);

/**
 * Route an incoming slashcommand event to the correct music handler.
 * Call this from your global on_slashcommand handler.
 */
void route_slashcommand(const dpp::slashcommand_t& event,
                        fc::lavalink::node& lavalink);

} // namespace fc::music
