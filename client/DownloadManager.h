/*
 * Copyright (C) 2001-2017 Jacek Sieka, arnetheduck on gmail point com
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

#ifndef DCPLUSPLUS_DCPP_DOWNLOAD_MANAGER_H
#define DCPLUSPLUS_DCPP_DOWNLOAD_MANAGER_H

#include "DownloadManagerListener.h"
#include "QueueItem.h"
#include "TimerManager.h"
#include "Singleton.h"

/**
 * Singleton. Use its listener interface to update the download list
 * in the user interface.
 */
//typedef boost::unordered_map<UserPtr, UserConnection*, User::Hash> IdlersMap;
typedef std::vector<UserConnection*> UserConnectionList;

class DownloadManager : public Speaker<DownloadManagerListener>,
	private TimerManagerListener,
	public Singleton<DownloadManager>
{
	public:
		/** @internal */
		void addConnection(UserConnection* p_conn);
		static void checkIdle(const UserPtr& user);
		
		/** @internal */
		static void abortDownload(const string& target);
		
		/** @return Running average download speed in Bytes/s */
		static int64_t getRunningAverage() { return g_runningAverage; }
		static void setRunningAverage(int64_t avg) { g_runningAverage = avg; }
		
		static size_t getDownloadCount();
		
		static bool isStartDownload(QueueItem::Priority prio);
		static bool checkFileDownload(const UserPtr& user);
		void onData(UserConnection*, const uint8_t*, size_t) noexcept;

		void processUpdatedConnection(UserConnection* source) noexcept;
		void checkUserIP(UserConnection* source) noexcept;
		void fileNotAvailable(UserConnection* source);
		void noSlots(UserConnection* source, const string& param = Util::emptyString) noexcept;
		void fail(UserConnection* source, const string& error) noexcept;

		void processSTA(UserConnection* source, const AdcCommand& cmd) noexcept;
		void processSND(UserConnection* source, const AdcCommand& cmd) noexcept;

	private:
		static std::unique_ptr<RWLock> g_csDownload;
		static DownloadList g_download_map;
		static UserConnectionList g_idlers;
		static void removeIdleConnection(UserConnection* source);
		
		static int64_t g_runningAverage;
		
		static void removeDownload(const DownloadPtr& download);
		
		void failDownload(UserConnection* source, const string& reason);
		
		friend class Singleton<DownloadManager>;
		
		DownloadManager();
		~DownloadManager();
		
		void checkDownloads(UserConnection* conn);
		void startData(UserConnection* source, int64_t start, int64_t newSize, bool z);
		void endData(UserConnection* source);
		
		// TimerManagerListener
		void on(TimerManagerListener::Second, uint64_t tick) noexcept override;
};

#endif // !defined(DOWNLOAD_MANAGER_H)
