/*       +------------------------------------+
 *       | Inspire Internet Relay Chat Daemon |
 *       +------------------------------------+
 *
 *  InspIRCd: (C) 2002-2010 InspIRCd Development Team
 * See: http://wiki.inspircd.org/Credits
 *
 * This program is free but copyrighted software; see
 *	    the file COPYING for details.
 *
 * ---------------------------------------------------
 */

#include "inspircd.h"
#include "httpd.h"
#include "protocol.h"

/* $ModDesc: Provides access control lists (passwording of resources, ip restrictions etc) to m_httpd.so dependent modules */

class HTTPACL
{
 public:
	std::string path;
	std::string username;
	std::string password;
	std::string whitelist;
	std::string blacklist;

	HTTPACL(const std::string &set_path, const std::string &set_username, const std::string &set_password,
		const std::string &set_whitelist, const std::string &set_blacklist)
		: path(set_path), username(set_username), password(set_password), whitelist(set_whitelist),
		blacklist(set_blacklist) { }

	~HTTPACL() { }
};

class ModuleHTTPAccessList : public Module
{

	std::string stylesheet;
	bool changed;
	std::vector<HTTPACL> acl_list;

 public:

	void ReadConfig()
	{
		acl_list.clear();
		ConfigTagList acls = ServerInstance->Config->ConfTags("httpdacl");
		for (ConfigIter i = acls.first; i != acls.second; i++)
		{
			ConfigTag* c = i->second;
			std::string path = c->getString("path");
			std::string types = c->getString("types");
			irc::commasepstream sep(types);
			std::string type;
			std::string username;
			std::string password;
			std::string whitelist;
			std::string blacklist;

			while (sep.GetToken(type))
			{
				if (type == "password")
				{
					username = c->getString("username");
					password = c->getString("password");
				}
				else if (type == "whitelist")
				{
					whitelist = c->getString("whitelist");
				}
				else if (type == "blacklist")
				{
					blacklist = c->getString("blacklist");
				}
				else
				{
					throw ModuleException("Invalid HTTP ACL type '" + type + "'");
				}
			}

			ServerInstance->Logs->Log("m_httpd_acl", DEBUG, "Read ACL: path=%s pass=%s whitelist=%s blacklist=%s", path.c_str(),
					password.c_str(), whitelist.c_str(), blacklist.c_str());

			acl_list.push_back(HTTPACL(path, username, password, whitelist, blacklist));
		}
	}

	ModuleHTTPAccessList() 	{
		ReadConfig();
		Implementation eventlist[] = { I_OnEvent };
		ServerInstance->Modules->Attach(eventlist, this, 1);
	}

	void BlockAccess(HTTPRequest* http, int returnval, const std::string &extraheaderkey = "", const std::string &extraheaderval="")
	{
		ServerInstance->Logs->Log("m_httpd_acl", DEBUG, "BlockAccess (%d)", returnval);

		std::stringstream data("Access to this resource is denied by an access control list. Please contact your IRC administrator.");
		HTTPDocumentResponse response(this, *http, &data, returnval);
		response.headers.SetHeader("X-Powered-By", "m_httpd_acl.so");
		if (!extraheaderkey.empty())
			response.headers.SetHeader(extraheaderkey, extraheaderval);
		response.Send();
	}

	void OnEvent(Event& event)
	{
		if (event.id == "httpd_acl")
		{
			ServerInstance->Logs->Log("m_http_stats", DEBUG,"Handling httpd acl event");
			HTTPRequest* http = (HTTPRequest*)&event;

			for (std::vector<HTTPACL>::const_iterator this_acl = acl_list.begin(); this_acl != acl_list.end(); ++this_acl)
			{
				if (InspIRCd::Match(http->GetURI(), this_acl->path, ascii_case_insensitive_map))
				{
					if (!this_acl->blacklist.empty())
					{
						/* Blacklist */
						irc::commasepstream sep(this_acl->blacklist);
						std::string entry;

						while (sep.GetToken(entry))
						{
							if (InspIRCd::Match(http->GetIP(), entry, ascii_case_insensitive_map))
							{
								ServerInstance->Logs->Log("m_httpd_acl", DEBUG, "Denying access to blacklisted resource %s (matched by pattern %s) from ip %s (matched by entry %s)",
										http->GetURI().c_str(), this_acl->path.c_str(), http->GetIP().c_str(), entry.c_str());
								BlockAccess(http, 403);
								return;
							}
						}
					}
					if (!this_acl->whitelist.empty())
					{
						/* Whitelist */
						irc::commasepstream sep(this_acl->whitelist);
						std::string entry;
						bool allow_access = false;

						while (sep.GetToken(entry))
						{
							if (InspIRCd::Match(http->GetIP(), entry, ascii_case_insensitive_map))
								allow_access = true;
						}

						if (!allow_access)
						{
							ServerInstance->Logs->Log("m_httpd_acl", DEBUG, "Denying access to whitelisted resource %s (matched by pattern %s) from ip %s (Not in whitelist)",
									http->GetURI().c_str(), this_acl->path.c_str(), http->GetIP().c_str());
							BlockAccess(http, 403);
							return;
						}
					}
					if (!this_acl->password.empty() && !this_acl->username.empty())
					{
						/* Password auth, first look to see if we have a basic authentication header */
						ServerInstance->Logs->Log("m_httpd_acl", DEBUG, "Checking HTTP auth password for resource %s (matched by pattern %s) from ip %s, against username %s",
								http->GetURI().c_str(), this_acl->path.c_str(), http->GetIP().c_str(), this_acl->username.c_str());

						if (http->headers->IsSet("Authorization"))
						{
							/* Password has been given, validate it */
							std::string authorization = http->headers->GetHeader("Authorization");
							irc::spacesepstream sep(authorization);
							std::string authtype;
							std::string base64;

							sep.GetToken(authtype);
							if (authtype == "Basic")
							{
								std::string user;
								std::string pass;

								sep.GetToken(base64);
								std::string userpass = Base64ToBin(base64);
								ServerInstance->Logs->Log("m_httpd_acl", DEBUG, "HTTP authorization: %s (%s)", userpass.c_str(), base64.c_str());

								irc::sepstream userpasspair(userpass, ':');
								if (userpasspair.GetToken(user))
								{
									userpasspair.GetToken(pass);

									/* Access granted if username and password are correct */
									if (user == this_acl->username && pass == this_acl->password)
									{
										ServerInstance->Logs->Log("m_httpd_acl", DEBUG, "HTTP authorization: password and username match");
										return;
									}
									else
										/* Invalid password */
										BlockAccess(http, 401, "WWW-Authenticate", "Basic realm=\"Restricted Object\"");
								}
								else
									/* Malformed user:pass pair */
									BlockAccess(http, 401, "WWW-Authenticate", "Basic realm=\"Restricted Object\"");
							}
							else
								/* Unsupported authentication type */
								BlockAccess(http, 401, "WWW-Authenticate", "Basic realm=\"Restricted Object\"");
						}
						else
						{
							/* No password given at all, access denied */
							BlockAccess(http, 401, "WWW-Authenticate", "Basic realm=\"Restricted Object\"");
						}
					}

					/* A path may only match one ACL (the first it finds in the config file) */
					return;
				}
			}
		}
	}

	virtual ~ModuleHTTPAccessList()
	{
	}

	virtual Version GetVersion()
	{
		return Version("Provides access control lists (passwording of resources, ip restrictions etc) to m_httpd.so dependent modules", VF_VENDOR);
	}
};

MODULE_INIT(ModuleHTTPAccessList)
