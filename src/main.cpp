#include <dpp/dpp.h>
#include <dpp/presence.h>
#include <cstdlib>

using namespace dpp;

int main(){
    
	cluster bot(std::getenv("token"));
    
    //activity(at_listening, "Phoon", "bhop_arcane idk im just testing stuff out", "");
    
	bot.on_log(utility::cout_logger());

	bot.on_slashcommand([](const slashcommand_t& event){
		if (event.command.get_command_name() == "ping"){
			event.reply("Pong!");
		}
	});


	bot.on_ready([&bot](const ready_t& event) {
		if (run_once<struct register_bot_commands>()){
			bot.global_command_create(slashcommand("ping", "Pong!", bot.me.id));
		}
	});

	bot.start(st_wait);

}
