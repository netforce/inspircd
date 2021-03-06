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

#define _CRT_SECURE_NO_DEPRECATE
#define _SCL_SECURE_NO_DEPRECATE

#include "inspircd.h"
#include <iostream>

typedef std::map<irc::string,irc::string> censor_t;

/* $ModDesc: Provides user and channel +G mode */

/** Handles usermode +G
 */
class CensorUser : public SimpleUserModeHandler
{
 public:
	CensorUser(Module* Creator) : SimpleUserModeHandler(Creator, "u_censor", 'G') { }
};

/** Handles channel mode +G
 */
class CensorChannel : public SimpleChannelModeHandler
{
 public:
	CensorChannel(Module* Creator) : SimpleChannelModeHandler(Creator, "censor", 'G') { }
};

class ModuleCensor : public Module
{
	censor_t censors;
	CensorUser cu;
	CensorChannel cc;

 public:
	ModuleCensor() : cu(this), cc(this) { }

	void init()
	{
		/* Read the configuration file on startup.
		 */
		OnRehash(NULL);
		ServerInstance->Modules->AddService(cu);
		ServerInstance->Modules->AddService(cc);
		Implementation eventlist[] = { I_OnRehash, I_OnUserPreMessage, I_OnUserPreNotice };
		ServerInstance->Modules->Attach(eventlist, this, 3);
	}


	virtual ~ModuleCensor()
	{
	}

	// format of a config entry is <badword text="shit" replace="poo">
	virtual ModResult OnUserPreMessage(User* user,void* dest,int target_type, std::string &text, char status, CUList &exempt_list)
	{
		if (!IS_LOCAL(user))
			return MOD_RES_PASSTHRU;

		bool active = false;

		if (target_type == TYPE_USER)
			active = ((User*)dest)->IsModeSet('G');
		else if (target_type == TYPE_CHANNEL)
		{
			active = ((Channel*)dest)->IsModeSet('G');
			Channel* c = (Channel*)dest;
			ModResult res = ServerInstance->OnCheckExemption(user,c,"censor");

			if (res == MOD_RES_ALLOW)
				return MOD_RES_PASSTHRU;
		}

		if (!active)
			return MOD_RES_PASSTHRU;

		irc::string text2 = text.c_str();
		for (censor_t::iterator index = censors.begin(); index != censors.end(); index++)
		{
			if (text2.find(index->first) != irc::string::npos)
			{
				if (index->second.empty())
				{
					user->WriteNumeric(ERR_WORDFILTERED, "%s %s %s :Your message contained a censored word, and was blocked", user->nick.c_str(), ((Channel*)dest)->name.c_str(), index->first.c_str());
					return MOD_RES_DENY;
				}

				SearchAndReplace(text2, index->first, index->second);
			}
		}
		text = text2.c_str();
		return MOD_RES_PASSTHRU;
	}

	virtual ModResult OnUserPreNotice(User* user,void* dest,int target_type, std::string &text, char status, CUList &exempt_list)
	{
		return OnUserPreMessage(user,dest,target_type,text,status,exempt_list);
	}

	virtual void OnRehash(User* user)
	{
		/*
		 * reload our config file on rehash - we must destroy and re-allocate the classes
		 * to call the constructor again and re-read our data.
		 */
		ConfigReader MyConf;
		censors.clear();

		for (int index = 0; index < MyConf.Enumerate("badword"); index++)
		{
			irc::string pattern = (MyConf.ReadValue("badword","text",index)).c_str();
			irc::string replace = (MyConf.ReadValue("badword","replace",index)).c_str();
			censors[pattern] = replace;
		}
	}

	virtual Version GetVersion()
	{
		return Version("Provides user and channel +G mode",VF_VENDOR);
	}

};

MODULE_INIT(ModuleCensor)
