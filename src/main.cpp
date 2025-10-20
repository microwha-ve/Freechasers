#include <dpp/dpp.h>        // D++
#include <dpp/presence.h>   // D++ Presence, otherwise later code wont run
#include <cstdlib>          // Used for getenv();

using namespace dpp;

int main(){
    
	cluster bot(std::getenv("token"));  // Sets token
    
	bot.on_log(utility::cout_logger()); // D++ own logger

    // Defines what the commands will do
	bot.on_slashcommand([](const slashcommand_t& event){
		if (event.command.get_command_name() == "ping"){
			event.reply("Pong!");
		}
	});

    // Things that run when the bot is connected to discord api
	bot.on_ready([&bot](const ready_t& event) {
        std::cout << "Logged in as " << bot.me.username << "!" << std::endl;
        
        // Sets activity, ps_dnd = Do Not Disturb, "bhop_arcane" is the text status, at_competing = is like the sub thingy, like listening, watching, playing etc etc
        if (run_once<struct set_status>()) {
            std::cout << "Setting Presence status..." << std::endl;
            bot.set_presence(presence(ps_dnd,at_competing, "bhop_arcane"));
            std::cout << "Presence status set!" << std::endl;
        }
        // The only registered command, provides the command name and "description" when you type "/" in discord.
		if (run_once<struct register_bot_commands>()){
            std::cout << "Registering slash commands..." << std::endl;
			bot.global_command_create(slashcommand("ping", "Pong!", bot.me.id));
            std::cout << "Registered slash commands!" << std::endl;
		}
	});
    
    // Actually starts the bot.
	bot.start(st_wait);

}
