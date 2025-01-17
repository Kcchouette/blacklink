
/*
 * ApexDC speedmod (c) SMT 2007
 */

#include "stdinc.h"
#include "ShareManager.h"
#include "QueueManager.h"
#include "UploadManager.h"
#include "LogManager.h"
#include "UserInfoBase.h"

void UserInfoBase::matchQueue()
{
	if (getUser())
	{
		try
		{
			QueueManager::getInstance()->addList(getUser(), QueueItem::FLAG_MATCH_QUEUE);
		}
		catch (const Exception& e)
		{
			LogManager::message(e.getError());
		}
	}
}

void UserInfoBase::getUserResponses()
{
	if (getUser())
	{
		try
		{
			QueueManager::getInstance()->addCheckUserIP(getUser());
		}
		catch (const Exception& e)
		{
			LogManager::message(e.getError());
		}
	}
}

#ifdef IRAINMAN_INCLUDE_USER_CHECK
void UserInfoBase::checkList()
{
	if (getUser())
	{
		try
		{
			QueueManager::getInstance()->addList(getUser(), QueueItem::FLAG_USER_CHECK);
		}
		catch (const Exception& e)
		{
			LogManager::message(e.getError());
		}
	}
}
#endif

void UserInfoBase::doReport(const string& hubHint)
{
	if (getUser())
		ClientManager::dumpUserInfo(HintedUser(getUser(), hubHint));
}

void UserInfoBase::getList()
{
	if (getUser())
	{
		try
		{
			QueueManager::getInstance()->addList(getUser(), QueueItem::FLAG_CLIENT_VIEW);
		}
		catch (const Exception& e)
		{
			LogManager::message(e.getError());
		}
	}
}

void UserInfoBase::browseList()
{
	if (getUser())
	{
		try
		{
			QueueManager::getInstance()->addList(getUser(), QueueItem::FLAG_CLIENT_VIEW | QueueItem::FLAG_PARTIAL_LIST);
		}
		catch (const Exception& e)
		{
			dcassert(0);
			LogManager::message(e.getError());
		}
	}
}

void UserInfoBase::addFav()
{
	if (getUser())
		FavoriteManager::getInstance()->addFavoriteUser(getUser());
}

void UserInfoBase::setIgnorePM()
{
	if (getUser())
		FavoriteManager::getInstance()->setFlags(getUser(), FavoriteUser::FLAG_IGNORE_PRIVATE, FavoriteUser::PM_FLAGS_MASK);
}

void UserInfoBase::setFreePM()
{
	if (getUser())
		FavoriteManager::getInstance()->setFlags(getUser(), FavoriteUser::FLAG_FREE_PM_ACCESS, FavoriteUser::PM_FLAGS_MASK);
}

void UserInfoBase::setNormalPM()
{
	if (getUser())
		FavoriteManager::getInstance()->setFlags(getUser(), FavoriteUser::FLAG_NONE, FavoriteUser::PM_FLAGS_MASK);
}

void UserInfoBase::setUploadLimit(const int limit)
{
	if (getUser())
		FavoriteManager::getInstance()->setUploadLimit(getUser(), limit);
}

void UserInfoBase::delFav()
{
	if (getUser())
		FavoriteManager::getInstance()->removeFavoriteUser(getUser());
}

void UserInfoBase::ignoreOrUnignoreUserByName()
{
	if (getUser())
	{
		const string nick = getUser()->getLastNick();
		UserManager* userManager = UserManager::getInstance();
		if (userManager->isInIgnoreList(nick))
			userManager->removeFromIgnoreList(nick);
		else
			userManager->addToIgnoreList(nick);
	}
}

void UserInfoBase::pm(const string& hubHint)
{
	if (getUser()
#ifndef _DEBUG
	&& !getUser()->isMe()
#endif
	)
	{
		UserManager::getInstance()->outgoingPrivateMessage(getUser(), hubHint, Util::emptyStringT);
	}
}

void UserInfoBase::pmText(const string& hubHint, const tstring& message)
{
	if (!message.empty())
		UserManager::getInstance()->outgoingPrivateMessage(getUser(), hubHint, message);
}

void UserInfoBase::createSummaryInfo(const string& hubHint)
{
	if (getUser())
		UserManager::getInstance()->collectSummaryInfo(getUser(), hubHint);
}

void UserInfoBase::removeAll()
{
	if (getUser())
		QueueManager::getInstance()->removeSource(getUser(), QueueItem::Source::FLAG_REMOVED);
}

void UserInfoBase::connectFav()
{
	if (getUser())
		UserManager::getInstance()->openUserUrl(getUser());
}

void UserInfoBase::grantSlotPeriod(const string& hubHint, const uint64_t period)
{
	if (period && getUser())
		UploadManager::getInstance()->reserveSlot(HintedUser(getUser(), hubHint), period);
}

void UserInfoBase::ungrantSlot(const string& hubHint)
{
	if (getUser())
		UploadManager::getInstance()->unreserveSlot(HintedUser(getUser(), hubHint));
}
