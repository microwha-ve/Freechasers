#include <dpp/dpp.h>        // D++

#include <dpp/presence.h>   // D++ Presence, otherwise later code wont run

#include <dpp/appcommand.h> // D++ Commands, may or may not be needed idk yet

#include <cstdlib>          // Used for getenv();

using namespace dpp;

int main() {

    cluster bot(std::getenv("token")); // Sets token

    bot.on_log(utility::cout_logger()); // D++ own logger

    // Defines what the commands will do
    bot.on_slashcommand([ & bot](const slashcommand_t & event) {
        if (event.command.get_command_name() == "ping") {
            event.reply("Pong!");
        }
        if (event.command.get_command_name() == "status") {
            
            std::string status_str = event.get_parameter("Status");
            std::string activity_str = event.get_parameter("Activity");
            
            if (status_str == "onl") {
                status = ps_online;
            } else if (status_str == "idle") {
                status = ps_idle;
            } else if (status_str == "dnd") {
                status = ps_dnd;
            } else {
                event.reply("Invalid Status!");
            }
            
            if (activity_str == "ply") {
                activity = at_playing;
            } else if (activity_str == "listn") {
                activity = at_listening;
            } else if (activity_str == "watch") {
                activity = at_watching;
            } else {
                event.reply("Invalid Activity!");
            }
            
            bot.set_presence(presence(status, activity, "It Worked!"));
        }
    });

    // Things that run when the bot is connected to discord api
    bot.on_ready([ & bot](const ready_t & event) {
        std::cout << "Logged in as " << bot.me.username << "!" << std::endl;

        // Sets activity, ps_dnd = Do Not Disturb, "bhop_arcane" is the text status, at_competing = is like the sub thingy, like listening, watching, playing etc etc
        if (run_once < struct set_status > ()) {
            std::cout << "Setting Presence status..." << std::endl;
            bot.set_presence(presence(ps_dnd, at_competing, "bhop_arcane"));
            std::cout << "Presence status set!" << std::endl;
        }
        // The only registered command, provides the command name and "description" when you type "/" in discord.
        if (run_once < struct register_bot_commands > ()) {

            slashcommand pingCommand("ping", "Pong!", bot.me.id);

            slashcommand statusCommand("status", "Set bot status!", bot.me.id);
            statusCommand.add_option(
                command_option(co_string, "Status", "Select a status", true)
                .add_choice(command_option_choice("Online", std::string("onl")))
                .add_choice(command_option_choice("Do Not Disturb", std::string("dnd")))
                .add_choice(command_option_choice("Idle", std::string("idle")))
            );
            statusCommand.add_option(
                command_option(co_string, "Activity", "Select an activity for the status", true)
                .add_choice(command_option_choice("Playing", std::string("ply")))
                .add_choice(command_option_choice("Listening", std::string("listn")))
                .add_choice(command_option_choice("Watching", std::string("watch")))
            );
            

            std::cout << "Registering slash commands..." << std::endl;
            bot.global_builk_command_create({
                pingCommand,
                statusCommand
            });
            std::cout << "Registered slash commands!" << std::endl;
        }
    });

    // Actually starts the bot.
    bot.start(st_wait);

    return 0;

}
