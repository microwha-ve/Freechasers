// Main Branch

#include <dpp/dpp.h>            // D++

#include <dpp/presence.h>       // D++ Presence, may or may not be needed idk yet

#include <dpp/user.h>           // D++ User, may or may not be needed idk yet

#include <dpp/appcommand.h>     // D++ Commands, may or may not be needed idk yet

#include <dpp/application.h>    // D++ Application, may or may not be needed idk yet

#include <dpp/utility.h>        // D++ Utility, may or may not be needed idk yet

#include <cstdlib>              // Used for getenv();

#include <unordered_set>        // Used for storing Dev Team members

using namespace dpp;

int main() {

    cluster bot(std::getenv("token")); // Sets token

    bot.on_log(utility::cout_logger()); // D++ own logger

    std::unordered_set < snowflake > dev_team;

    // Defines what the commands will do
    bot.on_slashcommand([ & bot, & dev_team](const slashcommand_t & event) {

        if (event.command.get_command_name() == "ping") {
          event.reply("Pong!");
        }

        if (event.command.get_command_name() == "status") {

          //new perms check to be implemented

          if (dev_team.find(event.command.get_issuing_user().id) == dev_team.end()) {
            event.reply("Mein Fräulein has not given you permission to issue me that order.");
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
            event.reply("Invalid Status!");
            return;
          }

          if (activity_str == "ply") {
            activity = at_game;
          } else if (activity_str == "listn") {
            activity = at_listening;
          } else if (activity_str == "watch") {
            activity = at_watching;
          } else {
            event.reply("Invalid Activity!");
            return;
          }

          // Setting the status
          bot.set_presence(presence(status, activity, text_str));
          std::cout << "\033[33mStatus updated by: " << event.command.get_issuing_user().id << "\033[0m" << std::endl; // WOW DOES WORK!!!!

          // Replies so the user gets feedback.
          event.reply("Status Updated!");
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
          if (dev_team.find(event.command.get_issuing_user().id) == dev_team.end()) {
            event.reply("Mein Fräulein has not given you permission to issue me that order.");
            return;
          }

          snowflake guildID = event.command.guild_id;
          snowflake userID = std::get < snowflake > (event.get_parameter("userid"));
          std::string banReason = std::get < std::string > (event.get_parameter("reason"));
          long int days = std::get < long int > (event.get_parameter("deletemessages"));

          if (days < 0 || days > 7) {
            event.reply("The span of memory deletion must be between 0 and 7 days. Nothing more, nothing less.");
            return;
          }

          event.thinking();

          bot.set_audit_reason(banReason);

          bot.guild_ban_add(guildID, userID, days, [event = event, userID, banReason](const confirmation_callback_t & cc) mutable {
            if (cc.is_error()) {
              event.edit_response("The attempt has failed. The one in question remains unscathed.");
              std::cerr << "Failed to ban user " << userID << "! Err: " << cc.get_error().message << std::endl;
              return;
            }

            std::cout << userID << " has been banned with the reasoning: " << banReason << std::endl;
            event.edit_response("The deed is done. The user has been removed.");
          });
        }

        if (event.command.get_command_name() == "timeout") {
          if (dev_team.find(event.command.get_issuing_user().id) == dev_team.end()) {
            event.reply("Mein Fräulein has not given you permission to issue me that order.");
            return;
          }

          snowflake guildID = event.command.guild_id;
          snowflake userID = std::get < snowflake > (event.get_parameter("userid"));
          std::string reason = std::get < std::string > (event.get_parameter("reason"));
          long int minutes = std::get < long int > (event.get_parameter("time"));

          if (minutes < 1 || minutes > 10080) {
            event.reply("The time frame specified is illogical. Limit it between 1 minute and 7 days.");
            return;
          }

          snowflake timeout_role_id = 1077355031680000000; // replace with your role ID
          snowflake log_channel_id = 1077358314809204866; //replace with your channel ID

          event.thinking();

          // Set audit reason
          bot.set_audit_reason(reason);

          // Add timeout role
          bot.guild_member_add_role(guildID, userID, timeout_role_id,
            [event, & bot, guildID, userID, timeout_role_id, minutes, log_channel_id, reason](const confirmation_callback_t & cc) mutable {
              if (cc.is_error()) {
                std::cerr << "Failed to add timeout role to user " << userID << "! Err: " << cc.get_error().message << std::endl;
                event.edit_response("Attempt to restrain the subject failed. The role remains untouched.");
                return;
              }

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
    });
      // Things that run when the bot is connected to discord api
      bot.on_ready([ & bot, & dev_team](const ready_t & event) {

        std::cout << "Logged in as " << bot.me.username << "!" << std::endl;

        // Load developer team member IDs
        std::cout << "Loading Dev Team members..." << std::endl;

        bot.current_application_get([ & ](const confirmation_callback_t & cc) {
          if (cc.is_error()) {
            std::cerr << "Failed to fetch app info (team check unavailable)" << std::endl;
            return;
          }

          auto app = std::get < application > (cc.value);
          for (auto & member: app.team.members) {
            dev_team.insert(member.member_user.id);
            std::cout << "Added " << member.member_user.id << " to the Dev Team List!" << std::endl;
          }
          std::cout << "Dev Team members loaded!" << std::endl;
        });

        // Sets activity, ps_dnd = Do Not Disturb, "bhop_arcane" is the text status, at_competing = is like the sub thingy, like listening, watching, playing etc etc
        if (run_once < struct set_status > ()) {
          std::cout << "Setting Presence status..." << std::endl;
          bot.set_presence(presence(ps_dnd, at_game, "Traveling from the Immernarchtreich")); // Default status
          std::cout << "Presence status set!" << std::endl;
        }
        // The only registered command, provides the command name and "description" when you type "/" in discord.
        if (run_once < struct register_bot_commands > ()) {

          slashcommand pingCommand("ping", "Pong!", bot.me.id);

          slashcommand statusCommand("status", "Set bot status!", bot.me.id);

          slashcommand whoamiCommand("whoami", "Who are you? Shouldn't like you know that?", bot.me.id);

          slashcommand banCommand("ban", "Ban a user", bot.me.id);

          slashcommand timeoutCommand("timeout", "Put a user in timeout", bot.me.id);

          // statusOption #1
          statusCommand.add_option(
            command_option(co_string, "status", "Select a status", true) // 'true' makes the option required
            .add_choice(command_option_choice("Online", std::string("onl")))
            .add_choice(command_option_choice("Do Not Disturb", std::string("dnd")))
            .add_choice(command_option_choice("Idle", std::string("idle")))
          );
          // statusOption #2
          statusCommand.add_option(
            command_option(co_string, "activity", "Select an activity for the status", true)
            .add_choice(command_option_choice("Playing", std::string("ply")))
            .add_choice(command_option_choice("Listening", std::string("listn")))
            .add_choice(command_option_choice("Watching", std::string("watch")))
          );
          // statusOption #3
          statusCommand.add_option(
            command_option(co_string, "text", "Write the status message!", true)
          );

          // banOption #1
          banCommand.add_option(
            command_option(co_user, "userid", "Select a user to be banned", true)
          );
          // banOption #2
          banCommand.add_option(
            command_option(co_string, "reason", "Write a ban reason", true)
          );
          // banOption #3
          banCommand.add_option(
            command_option(co_integer, "deletemessages", "Delete the users messages for the past X days (at most 7 days), if unsure enter 0", true)
          );
          // timoutOption #1
          timeoutCommand.add_option(
            command_option(co_user, "userid", "Who did the oopsie?", true)
          );
          // timoutOption #2
          timeoutCommand.add_option(
            command_option(co_string, "reason", "Reasoning for the timeout", true)
          );
          // timoutOption #3
          timeoutCommand.add_option(
            command_option(co_integer, "time", "Time in minutes, from 1 to 10080 minutes", true)
          );

          std::cout << "Registering slash commands..." << std::endl;
          bot.global_bulk_command_create({
            pingCommand,
            statusCommand,
            //whoamiCommand,
            banCommand,
            timeoutCommand
          });

          std::cout << "Registered slash commands!" << std::endl;
        }
      });

      // Actually starts the bot.
      bot.start(st_wait);

      return 0;
    }
