/*
* Copyright (C) 2001-2019 Jacek Sieka, arnetheduck on gmail point com
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
*/

#ifndef DCPLUSPLUS_DCPP_HUBLIST_MANAGER_H
#define DCPLUSPLUS_DCPP_HUBLIST_MANAGER_H

#include "HublistManagerListener.h"
#include "HubEntry.h"
#include "HttpConnection.h"
#include "Singleton.h"
#include "Speaker.h"

#include "CFlyThread.h"

/**
 * Assumed to be called only by UI thread.
 */
class HublistManager : public Speaker<HublistManagerListener>, private HttpConnectionListener, public Singleton<HublistManager>
{
public:
	HublistManager();
	~HublistManager();

	enum
	{
		STATE_IDLE,
		STATE_DOWNLOADING,
		STATE_DOWNLOADED,
		STATE_DOWNLOAD_FAILED,
		STATE_FROM_CACHE,
		STATE_PARSE_FAILED
	};

	struct HubListInfo
	{
		uint64_t id;
		string url;
		string lastRedirUrl;
		HubEntry::List list;
		int state;
		string error;
		time_t lastModified;
	};

	void setServerList(const string &str);
	void getHubLists(vector<HubListInfo> &result) const noexcept;
	bool getHubList(HubListInfo &result, uint64_t id) const noexcept;
	bool refreshAndGetHubList(HubListInfo &result, uint64_t id) noexcept;
	bool refresh(uint64_t id) noexcept;

private:
	enum
	{
		TYPE_NORMAL,
		TYPE_BZIP2
	};

	struct HubList
	{
		const uint64_t id;
		const string url;
		string lastRedirUrl;
		HubEntry::List list;
		int state;
		HttpConnection *conn;
		string downloadBuf;
		string error;
		time_t lastModified;
		bool used;

		HubList(uint64_t id, const string &url) noexcept;
		void parse(int listType) noexcept;
		void save() const noexcept;		
		void getListData(bool forceDownload, HublistManager *manager) noexcept;
		void getInfo(HubListInfo &info) const noexcept;
	};
	
	mutable CriticalSection cs;
	std::list<HubList> hubLists;
	uint64_t nextID;

	// HttpConnectionListener
	void on(Data, HttpConnection*, const uint8_t*, size_t) noexcept;
	void on(Failed, HttpConnection*, const string&) noexcept;
	void on(Complete, HttpConnection*, const string&) noexcept;
	void on(Redirected, HttpConnection*, const string&) noexcept;
	void on(Retried, HttpConnection*, bool) noexcept;
};


#endif // !defined(FAVORITE_MANAGER_H)