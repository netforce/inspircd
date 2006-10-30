/*       +------------------------------------+
 *       | Inspire Internet Relay Chat Daemon |
 *       +------------------------------------+
 *
 *  InspIRCd is copyright (C) 2002-2006 ChatSpike-Dev.
 *                       E-mail:
 *                <brain@chatspike.net>
 *                <Craig@chatspike.net>
 *
 * Written by Craig Edwards, Craig McLure, and others.
 * This program is free but copyrighted software; see
 *            the file COPYING for details.
 *
 * ---------------------------------------------------
 */

#include "inspircd.h"
#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>
#include "socketengine_kqueue.h"


KQueueEngine::KQueueEngine(InspIRCd* Instance) : SocketEngine(Instance)
{
	EngineHandle = kqueue();
	if (EngineHandle == -1)
	{
		ServerInstance->Log(SPARSE,"ERROR: Could not initialize socket engine. Your kernel probably does not have the proper features.");
		ServerInstance->Log(SPARSE,"ERROR: this is a fatal error, exiting now.");
		printf("ERROR: Could not initialize socket engine. Your kernel probably does not have the proper features.");
		printf("ERROR: this is a fatal error, exiting now.");
		InspIRCd::Exit(ERROR);
	}
	CurrentSetSize = 0;
}

KQueueEngine::~KQueueEngine()
{
	ServerInstance->Log(DEBUG,"KQueueEngine::~KQueueEngine()");
	close(EngineHandle);
}

bool KQueueEngine::AddFd(EventHandler* eh)
{
	int fd = eh->GetFd();

	ServerInstance->Log(DEBUG,"KQueueEngine::AddFd(%d)",fd);

	if ((fd < 0) || (fd > MAX_DESCRIPTORS))
	{
		ServerInstance->Log(DEFAULT,"ERROR: FD of %d added above max of %d",fd,MAX_DESCRIPTORS);
		return false;
	}
	if (GetRemainingFds() <= 1)
	{
		ServerInstance->Log(DEFAULT,"ERROR: System out of file descriptors!");
		return false;
	}

	if (ref[fd])
	{
		ServerInstance->Log(DEFAULT,"ERROR: Slot already occupied");
		return false;
	}

	ref[fd] = eh;
	ServerInstance->Log(DEBUG,"Add socket %d",fd);

	struct kevent ke;
	ServerInstance->Log(DEBUG,"kqueue: Add socket to events, kq=%d socket=%d",EngineHandle,fd);
	EV_SET(&ke, fd, eh->Readable() ? EVFILT_READ : EVFILT_WRITE, EV_ADD, 0, 0, NULL);

	int i = kevent(EngineHandle, &ke, 1, 0, 0, NULL);
	if (i == -1)
	{
		ServerInstance->Log(DEBUG,"kqueue: List insertion failure!");
		return false;
	}

	CurrentSetSize++;
	return true;
}

bool KQueueEngine::DelFd(EventHandler* eh)
{
	int fd = eh->GetFd();

	ServerInstance->Log(DEBUG,"KQueueEngine::DelFd(%d)",fd);

	if ((fd < 0) || (fd > MAX_DESCRIPTORS))
		return false;

	struct kevent ke;
	EV_SET(&ke, fd, eh->Readable() ? EVFILT_READ : EVFILT_WRITE, EV_DELETE, 0, 0, NULL);

	int i = kevent(EngineHandle, &ke, 1, 0, 0, NULL);
	if (i == -1)
	{
		ServerInstance->Log(DEBUG,"kqueue: Failed to remove socket from queue: %s",strerror(errno));
		return false;
	}

	CurrentSetSize--;
	ref[fd] = NULL;

	return true;
}

void KQueueEngine::WantWrite(EventHandler* eh)
{
	struct kevent ke;
	EV_SET(&ke, eh->GetFd(), EVFILT_WRITE, EV_ADD | EV_ONESHOT, 0, 0, NULL);
	int i = kevent(EngineHandle, &ke, 1, 0, 0, NULL);
	if (i == -1)
	{
		ServerInstance->Log(DEBUG,"kqueue: Unable to set fd %d for wanting write", eh->GetFd());
	}
	else
	{
		ServerInstance->Log(DEBUG,"kqueue: Set fd %d for want write", eh->GetFd());
	}
}

int KQueueEngine::GetMaxFds()
{
	return MAX_DESCRIPTORS;
}

int KQueueEngine::GetRemainingFds()
{
	return MAX_DESCRIPTORS - CurrentSetSize;
}

int KQueueEngine::DispatchEvents()
{
	ts.tv_nsec = 5000L;
	ts.tv_sec = 0;
	int i = kevent(EngineHandle, NULL, 0, &ke_list[0], MAX_DESCRIPTORS, &ts);
	for (int j = 0; j < i; j++)
	{
		ServerInstance->Log(DEBUG,"Handle %s event on fd %d",ke_list[j].flags & EVFILT_WRITE ? "write" : "read", ke_list[j].ident);
		if (ke_list[j].flags & EVFILT_WRITE)
		{
			ServerInstance->Log(DEBUG,"kqueue: Write socket wants to be set back to read");
			struct kevent ke;
			EV_SET(&ke, ke_list[j].ident, EVFILT_READ, EV_ADD, 0, 0, NULL);
			int i = kevent(EngineHandle, &ke, 1, 0, 0, NULL);
			if (i == -1)
			{
				ServerInstance->Log(DEBUG,"kqueue: Unable to set fd %d back to just wanting to read!", ke_list[j].ident);
			}
			ref[ke_list[j].ident]->HandleEvent(EVENT_WRITE);
		}
		else
		{
			ref[ke_list[j].ident]->HandleEvent(EVENT_READ);
		}
	}

	return i;
}

std::string KQueueEngine::GetName()
{
	return "kqueue";
}
