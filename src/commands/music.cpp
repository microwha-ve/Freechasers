#include "fc/commands/music.hpp"

#include <deque>
#include <unordered_map>
#include <sstream>
#include <algorithm>
#include <cstdint>
#include <iostream>

namespace fc::music {

struct guild_state {
    std::deque<fc::lavalink::track> queue;
    bool paused = false;
    int volume = 100; // 0–1000
};

static std::unordered_map<dpp::snowflake, guild_state> g_guild_state;

static void reply_ephemeral(const dpp::slashcommand_t& event,
                            const std::string& content) {
    dpp::message m;
    m.set_content(content);
    m.set_flags(dpp::m_ephemeral);
    event.reply(m);
}

static std::string make_identifier(const std::string& query) {
    if (query.rfind("http://", 0) == 0 || query.rfind("https://", 0) == 0) {
        return query;
    }
    return "ytsearch:" + query;
}

// ----------------- command handlers -----------------

static void handle_play(const dpp::slashcommand_t& event,
                        fc::lavalink::node& lavalink) {
    if (!event.command.guild_id) {
        reply_ephemeral(event, "This command can only be used in a server.");
        return;
    }

    dpp::command_value v = event.get_parameter("query");
    std::string query = std::get<std::string>(v);

    const dpp::snowflake guild_id = event.command.guild_id;

    std::cout << "[Music] /play by " << event.command.get_issuing_user().id
              << " in guild " << guild_id
              << " query='" << query << "'" << std::endl;

    std::string identifier = make_identifier(query);
    auto tracks = lavalink.load_tracks(identifier);

    if (tracks.empty()) {
        std::cout << "[Music] /play found no tracks for '" << query << "'" << std::endl;
        reply_ephemeral(event, "No tracks found for that query.");
        return;
    }

    auto& st = g_guild_state[guild_id];
    bool start_immediately = st.queue.empty();

    st.queue.push_back(tracks.front());

    if (start_immediately) {
        std::cout << "[Music] /play starting track now: '"
                  << tracks.front().title << "'" << std::endl;
        lavalink.play(guild_id, st.queue.front().encoded);
        lavalink.set_volume(guild_id, st.volume);
        st.paused = false;
    } else {
        std::cout << "[Music] /play enqueued track at position "
                  << st.queue.size() << ": '"
                  << tracks.front().title << "'" << std::endl;
    }

    std::ostringstream msg;
    msg << "Enqueued: **" << tracks.front().title << "**";

    if (start_immediately) {
        msg << " (now playing)";
    } else {
        msg << " (position " << st.queue.size() << " in the queue)";
    }

    event.reply(msg.str());
}

static void handle_pause(const dpp::slashcommand_t& event,
                         fc::lavalink::node& lavalink) {
    if (!event.command.guild_id) {
        reply_ephemeral(event, "This command can only be used in a server.");
        return;
    }

    const dpp::snowflake guild_id = event.command.guild_id;
    auto it = g_guild_state.find(guild_id);

    if (it == g_guild_state.end() || it->second.queue.empty()) {
        std::cout << "[Music] /pause but nothing is playing in guild "
                  << guild_id << std::endl;
        reply_ephemeral(event, "Nothing is currently playing.");
        return;
    }

    auto& st = it->second;

    st.paused = !st.paused;
    lavalink.pause(guild_id, st.paused);

    if (st.paused) {
        std::cout << "[Music] /pause → paused in guild " << guild_id << std::endl;
        event.reply("Paused playback.");
    } else {
        std::cout << "[Music] /pause → resumed in guild " << guild_id << std::endl;
        event.reply("Resumed playback.");
    }
}

static void handle_stop(const dpp::slashcommand_t& event,
                        fc::lavalink::node& lavalink) {
    if (!event.command.guild_id) {
        reply_ephemeral(event, "This command can only be used in a server.");
        return;
    }

    const dpp::snowflake guild_id = event.command.guild_id;

    std::cout << "[Music] /stop in guild " << guild_id << std::endl;

    lavalink.stop(guild_id);
    g_guild_state.erase(guild_id);

    event.reply("Stopped playback and cleared the queue.");
}

static void handle_volume(const dpp::slashcommand_t& event,
                          fc::lavalink::node& lavalink) {
    if (!event.command.guild_id) {
        reply_ephemeral(event, "This command can only be used in a server.");
        return;
    }

    const dpp::snowflake guild_id = event.command.guild_id;

    dpp::command_value v = event.get_parameter("level");
    int64_t level64 = std::get<int64_t>(v);
    int level = static_cast<int>(level64);

    if (level < 0) level = 0;
    if (level > 1000) level = 1000;

    auto& st = g_guild_state[guild_id];
    st.volume = level;

    std::cout << "[Music] /volume in guild " << guild_id
              << " set to " << level << std::endl;

    lavalink.set_volume(guild_id, level);

    std::ostringstream msg;
    msg << "Volume set to **" << level << "**.";
    event.reply(msg.str());
}

static void handle_queue(const dpp::slashcommand_t& event) {
    if (!event.command.guild_id) {
        reply_ephemeral(event, "This command can only be used in a server.");
        return;
    }

    const dpp::snowflake guild_id = event.command.guild_id;
    auto it = g_guild_state.find(guild_id);

    if (it == g_guild_state.end() || it->second.queue.empty()) {
        std::cout << "[Music] /queue empty for guild " << guild_id << std::endl;
        reply_ephemeral(event, "The queue is currently empty.");
        return;
    }

    const auto& st = it->second;

    std::cout << "[Music] /queue requested in guild " << guild_id
              << ", " << st.queue.size() << " track(s) queued" << std::endl;

    dpp::embed e;
    e.set_title("Music queue");

    std::ostringstream desc;
    const std::size_t max_entries = 10;
    std::size_t index = 0;

    for (const auto& t : st.queue) {
        if (index >= max_entries) {
            desc << "\n...and more.";
            break;
        }

        if (index == 0) {
            desc << "**Now playing**: ";
        } else {
            desc << index << ". ";
        }

        std::string title = t.title.empty() ? "(unknown title)" : t.title;
        desc << "**" << title << "**";

        if (!t.author.empty()) {
            desc << " — " << t.author;
        }

        desc << "\n";
        ++index;
    }

    desc << "\nVolume: **" << st.volume << "**";
    desc << "\nStatus: **" << (st.paused ? "Paused" : "Playing") << "**";

    e.set_description(desc.str());

    dpp::message m;
    m.add_embed(e);

    event.reply(m);
}

// ----------------- public API -----------------

std::vector<dpp::slashcommand> make_commands(dpp::cluster& bot) {
    std::vector<dpp::slashcommand> cmds;

    dpp::slashcommand play_cmd("play",
                               "Play or queue a track",
                               bot.me.id);
    play_cmd.add_option(
        dpp::command_option(dpp::co_string, "query",
                            "URL or search query", true)
    );

    dpp::slashcommand pause_cmd("pause",
                                "Toggle pause/resume for the current track",
                                bot.me.id);

    dpp::slashcommand stop_cmd("stop",
                               "Stop playback and clear the queue",
                               bot.me.id);

    dpp::slashcommand volume_cmd("volume",
                                 "Set playback volume (0–1000)",
                                 bot.me.id);
    dpp::command_option vol_opt(dpp::co_integer, "level",
                                "Volume level (0–1000)", true);
    vol_opt.set_min_value(0);
    vol_opt.set_max_value(1000);
    volume_cmd.add_option(vol_opt);

    dpp::slashcommand queue_cmd("queue",
                                "Show the current music queue",
                                bot.me.id);

    play_cmd.set_interaction_contexts({ dpp::itc_guild });
    pause_cmd.set_interaction_contexts({ dpp::itc_guild });
    stop_cmd.set_interaction_contexts({ dpp::itc_guild });
    volume_cmd.set_interaction_contexts({ dpp::itc_guild });
    queue_cmd.set_interaction_contexts({ dpp::itc_guild });

    cmds.push_back(play_cmd);
    cmds.push_back(pause_cmd);
    cmds.push_back(stop_cmd);
    cmds.push_back(volume_cmd);
    cmds.push_back(queue_cmd);

    return cmds;
}

void route_slashcommand(const dpp::slashcommand_t& event,
                        fc::lavalink::node& lavalink) {
    const std::string& name = event.command.get_command_name();

    if (name == "play") {
        handle_play(event, lavalink);
    } else if (name == "pause") {
        handle_pause(event, lavalink);
    } else if (name == "stop") {
        handle_stop(event, lavalink);
    } else if (name == "volume") {
        handle_volume(event, lavalink);
    } else if (name == "queue") {
        handle_queue(event);
    }
}

} // namespace fc::music
