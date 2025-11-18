#include <dpp/dpp.h>                // D++

#include <dpp/presence.h>           // D++ Presence, may or may not be needed idk yet

#include <dpp/user.h>               // D++ User, may or may not be needed idk yet

#include <dpp/appcommand.h>         // D++ Commands, may or may not be needed idk yet

#include <dpp/application.h>        // D++ Application, may or may not be needed idk yet

#include <dpp/utility.h>            // D++ Utility, may or may not be needed idk yet

#include <cstdlib>                  // Used for getenv();

#include <unordered_set>            // Used for storing Dev Team members

#include "fc/lavalink/client.hpp"   // Lavalnk Connection

#include "fc/commands/music.hpp"    // Lavalink Music Control

#include <fstream>                  // File logging

#include <chrono>                   // Time? lwk thought this already was added

#include <iomanip>                  // more time

#include <thread>                   // eepy

#include <ctime>                    // more time



using namespace dpp;

void log_shutdown_to_file(const dpp::user& user) {
    std::ofstream log("shutdown.log", std::ios::app);
    if (!log.is_open()) {
        std::cout << "File log failed" << std::endl;
        return;
    }

    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);

    log << '['
        << std::put_time(std::localtime(&t), "%Y-%m-%d %H:%M:%S")
        << "] Shutdown requested by "
        << user.format_username()   // username + discriminator or display name
        << " (" << user.id << ')'
        << '\n';
}

int main() {

    cluster bot(std::getenv("token")); // Sets token

    bot.on_log(utility::cout_logger()); // D++ own logger

    std::unordered_set < snowflake > dev_team;
    
    fc::lavalink::node_config lav_cfg;
    lav_cfg.host       = "127.0.0.1";
    lav_cfg.port       = 2333;
    lav_cfg.https      = false;
    lav_cfg.password   = "youshallnotpass";
    lav_cfg.session_id = "default";

    fc::lavalink::node lavalink(bot, lav_cfg);

    // Lavalink voice glue
    bot.on_voice_state_update([&](const dpp::voice_state_update_t& ev) {
        lavalink.handle_voice_state_update(ev);
    });

    bot.on_voice_server_update([&](const dpp::voice_server_update_t& ev) {
        lavalink.handle_voice_server_update(ev);
    });

    // Defines what the commands will do
    bot.on_slashcommand([&bot, &dev_team, &lavalink](const slashcommand_t& event) {
        fc::music::route_slashcommand(event, lavalink, bot);


        if (event.command.get_command_name() == "ping") {
            
            event.thinking(true);

                    // ---- GATEWAY PING ----
                    double gateway_ping_ms = 0.0;

                    const auto& shards = bot.get_shards();  // const std::map<unsigned, dpp::discord_client*>
                    if (!shards.empty()) {
                        auto it = shards.begin();          // just take the first shard
                        if (it->second) {
                            // websocket_ping is in seconds → convert to ms
                            gateway_ping_ms = it->second->websocket_ping * 1000.0;
                        }
                    }

                    // ---- REST PING ----
                    // In your DPP version this is a plain member, in seconds.
                    double rest_ping_ms = bot.rest_ping * 1000.0;

                    std::ostringstream out;
                    out << "Pong!\n"
                        << "Gateway: **" << std::fixed << std::setprecision(2)
                        << gateway_ping_ms << " ms**\n"
                        << "REST: **" << std::fixed << std::setprecision(2)
                        << rest_ping_ms << " ms**\n" << "Development Branch";

                    // edit_original_response needs a dpp::message, not a string literal
                    event.edit_original_response(dpp::message(out.str()));
                }



        if (event.command.get_command_name() == "status") {

          event.thinking(true);

          if (dev_team.find(event.command.get_issuing_user().id) == dev_team.end()) {
            event.edit_original_response(dpp::message("Mein Fräulein has not given you permission to issue me that order."));
            return;
          }

          // Variables to be filled
          presence_status status;
          activity_type activity;

          // Variables to compare then fill the previous variables
          std::string status_str = std::get < std::string > (event.get_parameter("status"));
          std::string activity_str = std::get < std::string > (event.get_parameter("activity"));
          std::string text_str = std::get < std::string > (event.get_parameter("text"));

          // Filling variables
          if (status_str == "onl") {
            status = ps_online;
          } else if (status_str == "idle") {
            status = ps_idle;
          } else if (status_str == "dnd") {
            status = ps_dnd;
          } else {
            event.edit_original_response(dpp::message("Invalid Status!"));
            return;
          }

          if (activity_str == "ply") {
            activity = at_game;
          } else if (activity_str == "listn") {
            activity = at_listening;
          } else if (activity_str == "watch") {
            activity = at_watching;
          } else {
            event.edit_original_response(dpp::message("Invalid Activity!"));
            return;
          }

          // Setting the status
          bot.set_presence(presence(status, activity, text_str));
          std::cout << "\033[33mStatus updated by: " << event.command.get_issuing_user().id << "\033[0m" << std::endl; // WOW DOES WORK!!!!

          // Replies so the user gets feedback.
          event.edit_original_response(dpp::message("Status Updated!"));
        }

        /* It doesnt work and its just not needed rn
         if (event.command.get_command_name() == "whoami") {

          std::string username = event.command.get_issuing_user().username;
          std::string screenname = event.command.get_issuing_user().global_name;
          std::string id = event.command.get_issuing_user().id.str();
          std::string created = std::to_string(event.command.get_issuing_user().id.get_creation_time());
          //std::string tag         = event.command.get_issuing_user().primary_guild.tag; //Doesnt work for some reason
          std::string servers = std::to_string(event.command.get_issuing_user().refcount);

          event.reply(" Your username is: " + username + "\n Your screenname is: " + screenname + "\n Your discord ID is: " + id + "\n Your account was created: " + created + "\n Your primary server/tag is: " + tag  + "\n You are in: " + servers + " servers!");
          std::cout << event.command.get_issuing_user().get_creation_time() << std::endl;
        }*/

        if (event.command.get_command_name() == "ban") {
          event.thinking(true);
          if (dev_team.find(event.command.get_issuing_user().id) == dev_team.end()) {
            event.edit_original_response(dpp::message("Mein Fräulein has not given you permission to issue me that order."));
            return;
          }
          
          snowflake log_channel_id = 1077358314809204866;
          snowflake guildID = event.command.guild_id;
          snowflake userID = std::get < snowflake > (event.get_parameter("userid"));
          std::string banReason = std::get < std::string > (event.get_parameter("reason"));
          long int days = std::get < long int > (event.get_parameter("deletemessages"));

          if (days < 0 || days > 7) {
            event.edit_original_response(dpp::message("The span of memory deletion must be between 0 and 7 days. Nothing more, nothing less."));
            return;
          }

          event.thinking();

          bot.set_audit_reason(banReason);

          bot.guild_ban_add(guildID, userID, days, [event = event, userID, banReason, & bot, log_channel_id](const confirmation_callback_t & cc) mutable {
            if (cc.is_error()) {
              event.edit_response("The attempt has failed. The one in question remains unscathed.");
              std::cerr << "Failed to ban user " << userID << "! Err: " << cc.get_error().message << std::endl;
              return;
            }

            std::cout << userID << " has been banned with the reasoning: " << banReason << std::endl;
            std::string msg = "<@" + userID.str() + "> has been banned for " + banReason + ".\n Banned by <@" + event.command.get_issuing_user().id.str() + ">";
            std::string msg2 = "You have been banned from a server, for " + banReason + ".";
            event.edit_response("The deed is done. The user has been removed.");
            bot.message_create(message(log_channel_id, msg));
            bot.message_create(message(userID, msg2));
          });
        }

        if (event.command.get_command_name() == "timeout") {

          event.thinking(true);

          if (dev_team.find(event.command.get_issuing_user().id) == dev_team.end()) {
            event.edit_original_response(dpp::message("Mein Fräulein has not given you permission to issue me that order."));
            return;
          }

          snowflake guildID = event.command.guild_id;
          snowflake userID = std::get < snowflake > (event.get_parameter("userid"));
          std::string reason = std::get < std::string > (event.get_parameter("reason"));
          long int minutes = std::get < long int > (event.get_parameter("time"));

          if (minutes < 1 || minutes > 10080) {
            event.edit_original_response(dpp::message("The time frame specified is illogical. Limit it between 1 minute and 7 days."));
            return;
          }

          snowflake timeout_role_id = 1077355031680000000; // replace with your role ID
          snowflake log_channel_id = 1077358314809204866; //replace with your channel ID

          event.thinking(true);


          // Add timeout role
          bot.guild_member_add_role(guildID, userID, timeout_role_id,
            [event, & bot, guildID, userID, timeout_role_id, minutes, log_channel_id, reason](const confirmation_callback_t & cc) mutable {
              if (cc.is_error()) {
                std::cerr << "Failed to add timeout role to user " << userID << "! Err: " << cc.get_error().message << std::endl;
                event.edit_response("Attempt to restrain the subject failed. The role remains untouched.");
                return;
              }

              // Set audit reason
              bot.set_audit_reason(reason);
              // Apply timeout
              time_t timeout_until = std::time(nullptr) + (minutes * 60);
              bot.guild_member_timeout(guildID, userID, timeout_until,
                [event, & bot, guildID, userID, timeout_role_id, minutes, log_channel_id, reason](const confirmation_callback_t & cc2) mutable {
                  if (cc2.is_error()) {
                    event.edit_response("The role is applied, yet the timeout faltered. The subject remains partially free.");
                    std::cerr << "Failed to timeout user " << userID << "! Err: " << cc2.get_error().message << std::endl;
                    return;
                  }

                  event.edit_response("Timeout applied successfully.");

                  // Notify in log channel
                  std::string msg = "<@" + userID.str() + "> has been confined for " + std::to_string(minutes) + " minutes";
                  if (!reason.empty())
                    msg += ". Reason: " + reason;

                  bot.message_create(message(log_channel_id, msg));

                  // Schedule role removal
                  std::thread([ & bot, guildID, userID, timeout_role_id, minutes]() {
                    std::this_thread::sleep_for(std::chrono::minutes(minutes));
                    bot.guild_member_remove_role(guildID, userID, timeout_role_id, utility::log_error());
                  }).detach();
                }
              );
            }
          );
        };
        
        if (event.command.get_command_name() == "shutdown") {
          event.thinking(true);
            if (dev_team.find(event.command.get_issuing_user().id) == dev_team.end()) {
              event.edit_original_response(dpp::message("Mein Fräulein has not given you permission to issue me that order."));
              return;
            }
            
            log_shutdown_to_file(event.command.get_issuing_user());
            
            event.edit_original_response(dpp::message("Shutting down..."));
            
            bot.start_timer([&bot](timer timer){
                bot.shutdown();
            }, 3.0);
            
        }
    });
      // Things that run when the bot is connected to discord api
    bot.on_ready([&bot, &dev_team, &lavalink](const ready_t& event) {

        std::cout << "Logged in as " << bot.me.username << "!" << std::endl;

        // Load developer team member IDs
        std::cout << "Loading Dev Team members..." << std::endl;

        bot.current_application_get([&](const confirmation_callback_t& cc) {
            if (cc.is_error()) {
                std::cerr << "Failed to fetch app info (team check unavailable)" << std::endl;
                return;
            }

            auto app = std::get<application>(cc.value);
            for (auto& member : app.team.members) {
                dev_team.insert(member.member_user.id);
                std::cout << "Added " << member.member_user.id << " to the Dev Team List!" << std::endl;
            }
            std::cout << "Dev Team members loaded!" << std::endl;
        });

        // Presence
        if (run_once<struct set_status>()) {
            std::cout << "Setting Presence status..." << std::endl;
            bot.set_presence(presence(ps_dnd, at_game, "Traveling from the Immernarchtreich"));
            std::cout << "Presence status set!" << std::endl;
        }

        // Slash commands
        if (run_once<struct register_bot_commands>()) {
            std::cout << "Registering slash commands..." << std::endl;
            
            slashcommand pingCommand("ping", "Pong!", bot.me.id);
            slashcommand statusCommand("status", "Set bot status!", bot.me.id);
            slashcommand whoamiCommand("whoami", "Who are you? Shouldn't like you know that?", bot.me.id);
            slashcommand banCommand("ban", "Ban a user", bot.me.id);
            slashcommand timeoutCommand("timeout", "Put a user in timeout", bot.me.id);
            slashcommand shutdownCommand("shutdown", "Turns the bot off? Like what did u expect", bot.me.id);
            
            // status options
            statusCommand.add_option(
                                     command_option(co_string, "status", "Select a status", true)
                                     .add_choice(command_option_choice("Online", std::string("onl")))
                                     .add_choice(command_option_choice("Do Not Disturb", std::string("dnd")))
                                     .add_choice(command_option_choice("Idle", std::string("idle")))
                                     );
            statusCommand.add_option(
                                     command_option(co_string, "activity", "Select an activity for the status", true)
                                     .add_choice(command_option_choice("Playing", std::string("ply")))
                                     .add_choice(command_option_choice("Listening", std::string("listn")))
                                     .add_choice(command_option_choice("Watching", std::string("watch")))
                                     );
            statusCommand.add_option(
                                     command_option(co_string, "text", "Write the status message!", true)
                                     );
            
            // ban options
            banCommand.add_option(
                                  command_option(co_user, "userid", "Select a user to be banned", true)
                                  );
            banCommand.add_option(
                                  command_option(co_string, "reason", "Write a ban reason", true)
                                  );
            banCommand.add_option(
                                  command_option(co_integer, "deletemessages",
                                                 "Delete the users messages for the past X days (at most 7 days), if unsure enter 0",
                                                 true)
                                  );
            
            // timeout options
            timeoutCommand.add_option(
                                      command_option(co_user, "userid", "Who did the oopsie?", true)
                                      );
            timeoutCommand.add_option(
                                      command_option(co_string, "reason", "Reasoning for the timeout", true)
                                      );
            timeoutCommand.add_option(
                                      command_option(co_integer, "time", "Time in minutes, from 1 to 10080 minutes", true)
                                      );
            
            // Build full command list
            std::vector<slashcommand> all_cmds{
                pingCommand,
                statusCommand,
                // whoamiCommand, // if you want it, uncomment
                banCommand,
                timeoutCommand,
                shutdownCommand
            };
            
            auto music_cmds = fc::music::make_commands(bot);
            all_cmds.insert(all_cmds.end(), music_cmds.begin(), music_cmds.end());
            
            bot.global_bulk_command_create(all_cmds);
            
            std::cout << "Registered slash commands!" << std::endl;
        } // <-- close the if, only }

            }); // <-- close the lambda and on_ready with );

            bot.start();
            return 0;
        }
