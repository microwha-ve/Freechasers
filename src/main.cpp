#include <dpp/dpp.h>
#include <dpp/presence.h>
#include <cstdlib>

using namespace dpp;

int main(){
    
	cluster bot(std::getenv("token"));
    
	bot.on_log(utility::cout_logger());

	bot.on_slashcommand([](const slashcommand_t& event){
		if (event.command.get_command_name() == "ping"){
			event.reply("Pong!");
		}
	});


	bot.on_ready([&bot](const ready_t& event) {
        std::cout << "Logged in as " << bot.me.username << "!" << std::endl;
        
        if (run_once<struct set_status>()) {
            std::cout << "Setting Presence status..." << std::endl;
            bot.set_presence(presence(ps_dnd,at_competing, "bhop_arcane"));
            std::cout << "Presence status set!" << std::endl;
        }
        
		if (run_once<struct register_bot_commands>()){
            std::cout << "Registering slash commands..." << std::endl;
			bot.global_command_create(slashcommand("ping", "Pong!", bot.me.id));
            std::cout << "Registered slash commands!" << std::endl;
		}
	});

	bot.start(st_wait);

}
