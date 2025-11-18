#include "fc/commands/music.hpp"

#include <dpp/dpp.h>
#include <dpp/utility.h>

#include <sstream>
#include <optional>

using dpp::slashcommand;
using dpp::slashcommand_t;
using dpp::command_option;
using dpp::command_option_choice;

namespace fc::music {

namespace {

std::unordered_map<dpp::snowflake, guild_state> g_guild_states;

/// Get or create state for a guild.
guild_state& get_state(dpp::snowflake guild_id) {
    return g_guild_states[guild_id];
}

/// Try to connect the bot to the issuing user's voice channel.
bool join_users_voice_channel(const dpp::slashcommand_t& ev) {
    dpp::cluster* bot = ev.from->creator;
    if (!bot) {
        return false;
    }

    dpp::snowflake guild_id = ev.command.guild_id;
    dpp::snowflake user_id  = ev.command.get_issuing_user().id;

    auto git = bot->guilds.find(guild_id);
    if (git == bot->guilds.end() || !git->second) {
        return false;
    }

    dpp::guild* g = git->second.get();
    if (!g) {
        return false;
    }

    // This is the DPP 10.x+ helper: it finds the member's voice channel and joins.
    bool ok = g->connect_member_voice(*bot, user_id, false, false, false);
    if (!ok) {
        bot->log(dpp::ll_warning,
                 "connect_member_voice() returned false (user not in VC or cannot join).");
    }
    return ok;
}

void handle_play(const dpp::slashcommand_t& ev, fc::lavalink::node& lavalink) {
    ev.thinking(true);

    dpp::snowflake guild_id = ev.command.guild_id;
    std::string query = std::get<std::string>(ev.get_parameter("query"));

    // Turn plain text into a YouTube search query if it's not a URL
    std::string identifier = query;
    if (!(identifier.rfind("http://", 0) == 0 || identifier.rfind("https://", 0) == 0)) {
        identifier = "ytsearch:" + query;
    }

    if (ev.from && ev.from->creator) {
        std::ostringstream log;
        log << "[Music] /play by " << ev.command.get_issuing_user().id
            << " in guild " << guild_id
            << " query='" << query << "'";
        ev.from->creator->log(dpp::ll_info, log.str());
    }

    fc::lavalink::load_result res = lavalink.load_tracks(identifier);

    if (res.type == fc::lavalink::load_type::error) {
        std::string msg = "Error from Lavalink: " + res.error_message;
        ev.edit_original_response(dpp::message(msg));
        return;
    }

    if (res.tracks.empty()) {
        std::ostringstream msg;
        msg << "No tracks found for `" << query << "`.";
        ev.edit_original_response(dpp::message(msg.str()));
        return;
    }

    // Use the first track for now.
    const fc::lavalink::track& first = res.tracks.front();
    auto& st = get_state(guild_id);
    st.queue.push_back(first);

    if (!join_users_voice_channel(ev)) {
        ev.edit_original_response(dpp::message(
            "You must be in a voice channel I can join before I can play anything."
        ));
        return;
    }

    bool start_now = (st.queue.size() == 1);

    std::ostringstream msg;
    if (start_now) {
        bool ok = lavalink.play(guild_id, first.encoded, false, std::nullopt, 100);
        if (!ok) {
            ev.edit_original_response(dpp::message(
                "Failed to start playback on Lavalink (no session or HTTP error)."
            ));
            return;
        }

        msg << "Enqueued: **" << first.title << "** (now playing)";
    } else {
        msg << "Enqueued: **" << first.title << "** (position "
            << st.queue.size() << " in queue)";
    }

    ev.edit_original_response(dpp::message(msg.str()));
}

void handle_pause(const dpp::slashcommand_t& ev, fc::lavalink::node& lavalink) {
    ev.thinking(true);

    dpp::snowflake guild_id = ev.command.guild_id;
    auto it = g_guild_states.find(guild_id);
    if (it == g_guild_states.end() || it->second.queue.empty()) {
        ev.edit_original_response(dpp::message("Nothing is currently playing."));
        return;
    }

    guild_state& st = it->second;
    bool new_state = !st.paused;
    bool ok = lavalink.pause(guild_id, new_state);
    if (!ok) {
        ev.edit_original_response(dpp::message("Failed to send pause/resume to Lavalink."));
        return;
    }

    st.paused = new_state;

    if (new_state) {
        ev.edit_original_response(dpp::message("Paused playback."));
    } else {
        ev.edit_original_response(dpp::message("Resumed playback."));
    }
}

void handle_stop(const dpp::slashcommand_t& ev, fc::lavalink::node& lavalink) {
    ev.thinking(true);

    dpp::snowflake guild_id = ev.command.guild_id;
    auto it = g_guild_states.find(guild_id);
    if (it == g_guild_states.end() || it->second.queue.empty()) {
        ev.edit_original_response(dpp::message("Nothing is currently playing."));
        return;
    }

    bool ok = lavalink.stop(guild_id);
    it->second.queue.clear();
    it->second.paused = false;

    if (!ok) {
        ev.edit_original_response(dpp::message("Failed to send stop to Lavalink."));
        return;
    }

    ev.edit_original_response(dpp::message("Stopped playback and cleared the queue."));
}

void handle_volume(const dpp::slashcommand_t& ev, fc::lavalink::node& lavalink) {
    ev.thinking(true);

    dpp::snowflake guild_id = ev.command.guild_id;

    long int vol_raw = std::get<long int>(ev.get_parameter("percent"));
    int vol = static_cast<int>(vol_raw);
    if (vol < 0) vol = 0;
    if (vol > 1000) vol = 1000;

    bool ok = lavalink.set_volume(guild_id, vol);
    if (!ok) {
        ev.edit_original_response(dpp::message("Failed to set volume on Lavalink."));
        return;
    }

    std::ostringstream msg;
    msg << "Volume set to **" << vol << "%**.";
    ev.edit_original_response(dpp::message(msg.str()));
}

void handle_queue(const dpp::slashcommand_t& ev, fc::lavalink::node&) {
    ev.thinking(true);

    dpp::snowflake guild_id = ev.command.guild_id;
    auto it = g_guild_states.find(guild_id);
    if (it == g_guild_states.end() || it->second.queue.empty()) {
        ev.edit_original_response(dpp::message("The queue is currently empty."));
        return;
    }

    const guild_state& st = it->second;

    std::ostringstream msg;
    msg << "Current queue (" << st.queue.size() << " track(s)):\n";

    std::size_t idx = 1;
    for (const auto& t : st.queue) {
        msg << idx << ". **" << t.title << "**";
        if (!t.author.empty()) {
            msg << " — *" << t.author << "*";
        }
        if (!t.uri.empty()) {
            msg << " <" << t.uri << ">";
        }
        msg << "\n";
        if (++idx > 10) {
            msg << "... and more.";
            break;
        }
    }

    ev.edit_original_response(dpp::message(msg.str()));
}

} // anonymous namespace

void register_commands(dpp::cluster&) {
    // Currently unused; left for parity with main.cpp calls.
}

std::vector<dpp::slashcommand> make_commands(dpp::cluster& bot) {
    std::vector<slashcommand> cmds;

    // /play
    slashcommand play_cmd("play", "Play a track via Lavalink", bot.me.id);
    play_cmd.add_option(
        command_option(dpp::co_string, "query", "YouTube URL or search query", true)
    );

    // /pause
    slashcommand pause_cmd("pause", "Pause or resume the current track", bot.me.id);

    // /stop
    slashcommand stop_cmd("stop", "Stop playback and clear the queue", bot.me.id);

    // /volume
    slashcommand volume_cmd("volume", "Set playback volume (0-1000)", bot.me.id);
    volume_cmd.add_option(
        command_option(dpp::co_integer, "percent", "Volume percent (0-1000)", true)
    );

    // /queue
    slashcommand queue_cmd("queue", "Show the current music queue", bot.me.id);

    cmds.push_back(play_cmd);
    cmds.push_back(pause_cmd);
    cmds.push_back(stop_cmd);
    cmds.push_back(volume_cmd);
    cmds.push_back(queue_cmd);

    return cmds;
}

void route_slashcommand(const dpp::slashcommand_t& ev, fc::lavalink::node& lavalink) {
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
        handle_queue(ev, lavalink);
    }
}

} // namespace fc::music
