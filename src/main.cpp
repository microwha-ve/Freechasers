#include <dpp/dpp.h>
#include <cstdlib>

int main(){
	dpp::cluster bot(std::getenv("token"));
	bot.on_log(dpp::utility::cout_logger());

	bot.on_slashcommand([](const dpp::slashcommand_t& event){
		if (event.command.get_command_name() == "ping"){
			event.reply("Pong!");
		}
	});


	bot.on_ready([&bot](const dpp::ready_t& event) {
		if (dpp::run_once<struct register_bot_commands>()){
			bot.global_command_create(dpp::slashcommand("ping", "Pong!", bot.me.id));
		}
	});

	bot.start(dpp::st_wait);

}
