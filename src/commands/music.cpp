#include "fc/commands/music.hpp"

#include <sstream>
#include <optional>

using namespace dpp;

namespace fc::music {

std::unordered_map<dpp::snowflake, guild_music_state> g_states;

static guild_music_state& get_state(dpp::snowflake guild_id) {
    return g_states[guild_id];
}

static std::optional<dpp::snowflake>
find_user_voice_channel(const dpp::slashcommand_t& ev) {
    dpp::snowflake guild_id = ev.command.guild_id;
    if (!guild_id) {
        return std::nullopt;
    }

    const dpp::guild* g = dpp::find_guild(guild_id);
    if (!g) {
        return std::nullopt;
    }

    dpp::snowflake user_id = ev.command.get_issuing_user().id;
    auto it = g->members.find(user_id);
    if (it == g->members.end()) {
        return std::nullopt;
    }

    const dpp::guild_member& gm = it->second;
    if (!gm.is_in_voice()) {
        return std::nullopt;
    }

    return gm.voice_state.channel_id;
}

static void handle_play(const dpp::slashcommand_t& ev,
                        fc::lavalink::node& lavalink) {
    ev.thinking(true);

    dpp::snowflake guild_id = ev.command.guild_id;
    if (!guild_id) {
        ev.edit_original_response(dpp::message("This command can only be used in a server."));
        return;
    }

    // Find the user's voice channel
    auto maybe_vc = find_user_voice_channel(ev);
    if (!maybe_vc.has_value()) {
        ev.edit_original_response(
            dpp::message("You must be connected to a voice channel first."));
        return;
    }

    // Connect the bot to voice (this will also trigger voice state/server
    // events that the lavalink::node listens to in main.cpp).
    dpp::cluster& bot = *ev.from->creator;
    bot.guild_connect_voice(guild_id, *maybe_vc, false, false);

    // Resolve query
    std::string query = std::get<std::string>(ev.get_parameter("query"));
    std::string identifier = query;
    if (identifier.rfind("http://", 0) != 0 &&
        identifier.rfind("https://", 0) != 0) {
        identifier = "ytsearch:" + identifier;
    }

    bot.log(dpp::ll_info, "[Music] /play by " +
                           ev.command.get_issuing_user().id.str() +
                           " in guild " + guild_id.str() +
                           " query='" + query + "'");

    // Load tracks from Lavalink (new API: returns load_result)
    fc::lavalink::load_result result = lavalink.load_tracks(identifier);

    if (result.type == fc::lavalink::load_type::error) {
        ev.edit_original_response(
            dpp::message("Failed to load tracks for that query."));
        return;
    }

    if (result.tracks.empty()) {
        ev.edit_original_response(
            dpp::message("No tracks found for '" + query + "'."));
        return;
    }

    // Pick the first track
    const fc::lavalink::track& first = result.tracks.front();
    guild_music_state& st = get_state(guild_id);
    st.queue.push_back(first);

    std::ostringstream log;
    log << "[Music] /play resolved to '" << first.title << "'";
    bot.log(dpp::ll_info, log.str());

    bool started_now = !st.playing;
    if (started_now) {
        st.playing = true;

        // New play() signature:
        // bool play(snowflake guild_id,
        //           const std::string& encoded_track,
        //           bool no_replace,
        //           std::optional<long int> end_time,
        //           std::optional<int> volume);
        bool ok = lavalink.play(
            guild_id,
            first.encoded,
            /*no_replace*/ false,
            std::nullopt,
            st.volume   // start at current volume
        );

        if (!ok) {
            st.playing = false;
            ev.edit_original_response(
                dpp::message("Failed to start playback on Lavalink."));
            return;
        }

        std::ostringstream msg;
        msg << "Enqueued: **" << first.title << "** (now playing)";
        ev.edit_original_response(dpp::message(msg.str()));
    } else {
        std::ostringstream msg;
        msg << "Enqueued: **" << first.title << "**";
        ev.edit_original_response(dpp::message(msg.str()));
    }
}

static void handle_pause(const dpp::slashcommand_t& ev,
                         fc::lavalink::node& lavalink) {
    ev.thinking(true);

    dpp::snowflake guild_id = ev.command.guild_id;
    if (!guild_id) {
        ev.edit_original_response(dpp::message("This command can only be used in a server."));
        return;
    }

    bool pause = std::get<bool>(ev.get_parameter("pause"));

    if (!lavalink.pause(guild_id, pause)) {
        ev.edit_original_response(dpp::message("Failed to update pause state."));
        return;
    }

    ev.edit_original_response(
        dpp::message(pause ? "Paused playback." : "Resumed playback."));
}

static void handle_stop(const dpp::slashcommand_t& ev,
                        fc::lavalink::node& lavalink) {
    ev.thinking(true);

    dpp::snowflake guild_id = ev.command.guild_id;
    if (!guild_id) {
        ev.edit_original_response(dpp::message("This command can only be used in a server."));
        return;
    }

    lavalink.stop(guild_id);
    auto it = g_states.find(guild_id);
    if (it != g_states.end()) {
        it->second.queue.clear();
        it->second.playing = false;
    }

    ev.edit_original_response(dpp::message("Stopped playback and cleared the queue."));
}

static void handle_volume(const dpp::slashcommand_t& ev,
                          fc::lavalink::node& lavalink) {
    ev.thinking(true);

    dpp::snowflake guild_id = ev.command.guild_id;
    if (!guild_id) {
        ev.edit_original_response(dpp::message("This command can only be used in a server."));
        return;
    }

    int vol = static_cast<int>(std::get<int64_t>(ev.get_parameter("level")));
    if (vol < 0) vol = 0;
    if (vol > 1000) vol = 1000;

    guild_music_state& st = get_state(guild_id);
    st.volume = vol;

    if (!lavalink.set_volume(guild_id, vol)) {
        ev.edit_original_response(dpp::message("Failed to set volume on Lavalink."));
        return;
    }

    std::ostringstream msg;
    msg << "Volume set to **" << vol << "%**.";
    ev.edit_original_response(dpp::message(msg.str()));
}

static void handle_queue(const dpp::slashcommand_t& ev) {
    ev.thinking(true);

    dpp::snowflake guild_id = ev.command.guild_id;
    if (!guild_id) {
        ev.edit_original_response(dpp::message("This command can only be used in a server."));
        return;
    }

    auto it = g_states.find(guild_id);
    if (it == g_states.end() || it->second.queue.empty()) {
        ev.edit_original_response(dpp::message("The queue is currently empty."));
        return;
    }

    const guild_music_state& st = it->second;

    std::ostringstream msg;
    msg << "Current queue (" << st.queue.size() << " track"
        << (st.queue.size() == 1 ? "" : "s") << "):\n";

    std::size_t index = 0;
    for (const auto& t : st.queue) {
        msg << (index == 0 ? "**Now playing**" : std::to_string(index) + ".")
            << " — " << t.title;
        if (!t.author.empty()) {
            msg << " — *" << t.author << '*';
        }
        msg << '\n';
        ++index;

        if (index >= 10) { // avoid huge messages
            msg << "... and more.";
            break;
        }
    }

    ev.edit_original_response(dpp::message(msg.str()));
}

/* Public API */

void register_commands(dpp::cluster& /*bot*/) {
    // Kept for compatibility; registration is done via make_commands() in main.cpp
}

std::vector<dpp::slashcommand> make_commands(dpp::cluster& bot) {
    std::vector<dpp::slashcommand> cmds;

    // /play query:<string>
    {
        dpp::slashcommand play_cmd("play", "Play a track or search YouTube", bot.me.id);
        play_cmd.add_option(
            dpp::command_option(dpp::co_string, "query",
                                "URL or search term", true));
        cmds.push_back(play_cmd);
    }

    // /pause pause:<bool>
    {
        dpp::slashcommand pause_cmd("pause", "Pause or resume playback", bot.me.id);
        pause_cmd.add_option(
            dpp::command_option(dpp::co_boolean, "pause",
                                "True to pause, false to resume", true));
        cmds.push_back(pause_cmd);
    }

    // /stop
    {
        dpp::slashcommand stop_cmd("stop", "Stop playback and clear the queue", bot.me.id);
        cmds.push_back(stop_cmd);
    }

    // /volume level:<int>
    {
        dpp::slashcommand vol_cmd("volume", "Set the player volume", bot.me.id);
        vol_cmd.add_option(
            dpp::command_option(dpp::co_integer, "level",
                                "Volume percentage (0–1000)", true));
        cmds.push_back(vol_cmd);
    }

    // /queue
    {
        dpp::slashcommand q_cmd("queue", "Show the current music queue", bot.me.id);
        cmds.push_back(q_cmd);
    }

    return cmds;
}

void route_slashcommand(const dpp::slashcommand_t& ev,
                        fc::lavalink::node& lavalink) {
    const std::string& name = ev.command.get_command_name();

    if (name == "play") {
        handle_play(ev, lavalink);
    } else if (name == "pause") {
        handle_pause(ev, lavalink);
    } else if (name == "stop") {
        handle_stop(ev, lavalink);
    } else if (name == "volume") {
        handle_volume(ev, lavalink);
    } else if (name == "queue") {
        handle_queue(ev);
    }
}

} // namespace fc::music

