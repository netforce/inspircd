/*       +------------------------------------+
 *       | Inspire Internet Relay Chat Daemon |
 *       +------------------------------------+
 *
 *  InspIRCd: (C) 2002-2010 InspIRCd Development Team
 * See: http://wiki.inspircd.org/Credits
 *
 * This program is free but copyrighted software; see
 *            the file COPYING for details.
 *
 * ---------------------------------------------------
 */

#include "inspircd.h"
#include "xline.h"
/** Handle /REHASH. These command handlers can be reloaded by the core,
 * and handle basic RFC1459 commands. Commands within modules work
 * the same way, however, they can be fully unloaded, where these
 * may not.
 */
class CommandRehash : public Command
{
 public:
	/** Constructor for rehash.
	 */
	CommandRehash ( Module* parent) : Command(parent,"REHASH",0) { flags_needed = 'o'; Penalty = 2; syntax = "[<servermask>]"; }
	/** Handle command.
	 * @param parameters The parameters to the comamnd
	 * @param pcnt The number of parameters passed to teh command
	 * @param user The user issuing the command
	 * @return A value from CmdResult to indicate command success or failure.
	 */
	CmdResult Handle(const std::vector<std::string>& parameters, User *user);
};

CmdResult CommandRehash::Handle (const std::vector<std::string>& parameters, User *user)
{
	std::string param = parameters.size() ? parameters[0] : "";

	FOREACH_MOD(I_OnPreRehash,OnPreRehash(user, param));

	if (param.empty())
	{
		// standard rehash of local server
	}
	else if (param.find_first_of("*.") != std::string::npos)
	{
		// rehash of servers by server name (with wildcard)
		if (!InspIRCd::Match(ServerInstance->Config->ServerName, parameters[0]))
		{
			// Doesn't match us. PreRehash is already done, nothing left to do
			return CMD_SUCCESS;
		}
	}
	else
	{
		// parameterized rehash

		// the leading "-" is optional; remove it if present.
		if (param[0] == '-')
			param = param.substr(1);

		FOREACH_MOD(I_OnModuleRehash,OnModuleRehash(user, param));
		return CMD_SUCCESS;
	}

	// Rehash for me. Try to start the rehash thread
	if (!ServerInstance->ConfigThread)
	{
		std::string m = user->nick + " is rehashing config file " + ServerConfig::CleanFilename(ServerInstance->ConfigFileName.c_str()) + " on " + ServerInstance->Config->ServerName;
		ServerInstance->SNO->WriteGlobalSno('a', m);

		if (IS_LOCAL(user))
			user->WriteNumeric(RPL_REHASHING, "%s %s :Rehashing",
				user->nick.c_str(),ServerConfig::CleanFilename(ServerInstance->ConfigFileName.c_str()));
		else
			ServerInstance->PI->SendUserNotice(user, std::string("*** Rehashing server ") +
				ServerConfig::CleanFilename(ServerInstance->ConfigFileName.c_str()));

		/* Don't do anything with the logs here -- logs are restarted
		 * after the config thread has completed.
		 */

		ServerInstance->RehashUsersAndChans();
		FOREACH_MOD(I_OnGarbageCollect, OnGarbageCollect());


		ServerInstance->ConfigThread = new ConfigReaderThread(user->uuid);
		ServerInstance->Threads->Start(ServerInstance->ConfigThread);

		return CMD_SUCCESS;
	}
	else
	{
		/*
		 * A rehash is already in progress! ahh shit.
		 * XXX, todo: we should find some way to kill runaway rehashes that are blocking, this is a major problem for unrealircd users
		 */
		if (IS_LOCAL(user))
			user->WriteServ("NOTICE %s :*** Could not rehash: A rehash is already in progress.", user->nick.c_str());
		else
			ServerInstance->PI->SendUserNotice(user, "*** Could not rehash: A rehash is already in progress.");

		return CMD_FAILURE;
	}
}


COMMAND_INIT(CommandRehash)
