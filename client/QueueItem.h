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


#ifndef DCPLUSPLUS_DCPP_QUEUE_ITEM_H
#define DCPLUSPLUS_DCPP_QUEUE_ITEM_H

#include "Segment.h"
#include "HintedUser.h"
#include "RWLock.h"
#include "Download.h"
#include "TransferFlags.h"
#include "Locks.h"
#include <atomic>
#include <boost/container/flat_set.hpp>

typedef std::vector<DownloadPtr> DownloadList;

extern const string dctmpExtension;

class QueueManager;

#ifdef USE_QUEUE_RWLOCK
#define QueueRLock READ_LOCK
#define QueueWLock WRITE_LOCK
#else
#define QueueRLock LOCK
#define QueueWLock LOCK
#endif

class QueueItem
{
	public:
		typedef boost::unordered_map<string, QueueItemPtr> QIStringMap;
		typedef uint32_t MaskType;

		static const size_t PFS_MIN_FILE_SIZE = 20 * 1024 * 1024;

		enum Priority
		{
			DEFAULT = -1,
			PAUSED = 0,
			LOWEST,
			LOWER,
			LOW,
			NORMAL,
			HIGH,
			HIGHER,
			HIGHEST,
			LAST
		};

		enum FileFlags
		{
			FLAG_NORMAL             = 0x0000, // Normal download, no flags set
			FLAG_USER_LIST          = 0x0001, // This is a user file listing download
			FLAG_DIRECTORY_DOWNLOAD = 0x0002, // The file list is downloaded to use for directory download (used with USER_LIST)
			FLAG_CLIENT_VIEW        = 0x0004, // The file is downloaded to be viewed in the gui
			FLAG_TEXT               = 0x0008, // Flag to indicate that file should be viewed as a text file
			FLAG_MATCH_QUEUE        = 0x0010, // Match the queue against this list
			FLAG_XML_BZLIST         = 0x0020, // The file list downloaded was actually an .xml.bz2 list
			FLAG_PARTIAL_LIST       = 0x0040, // Only download a part of the file list
#ifdef IRAINMAN_INCLUDE_USER_CHECK
			FLAG_USER_CHECK         = 0x0080, // Test user's file list for fake share
#endif
			FLAG_AUTODROP           = 0x0100, // Autodrop slow source is enabled for this file
			FLAG_USER_GET_IP        = 0x0200,
			FLAG_DCLST_LIST         = 0x0400,
			FLAG_DOWNLOAD_CONTENTS  = 0x0800,
			FLAG_RECURSIVE_LIST     = 0x1000,
			FLAG_WANT_END           = 0x2000,
			FLAG_COPYING            = 0x4000
		};

		bool isUserList() const
		{
			return isAnySet(QueueItem::FLAG_USER_LIST | QueueItem::FLAG_DCLST_LIST | QueueItem::FLAG_USER_GET_IP);
		}

		typedef std::vector<uint16_t> PartsInfo;
		static int countParts(const PartsInfo& pi);
		static bool compareParts(const PartsInfo& a, const PartsInfo& b);

		/**
		 * Source parts info
		 * Meaningful only when Source::FLAG_PARTIAL is set
		 */
		class PartialSource
		{
			public:
				PartialSource(const string& myNick, const string& hubIpPort, const IpAddress& ip, uint16_t udp, int64_t blockSize) :
					myNick(myNick), hubIpPort(hubIpPort), ip(ip), udpPort(udp), nextQueryTime(0), pendingQueryCount(0), blockSize(blockSize) { }

				typedef std::shared_ptr<PartialSource> Ptr;
				bool isCandidate(const uint64_t now) const
				{
					return getUdpPort() != 0 && getNextQueryTime() <= now;
				}

				GETSET(PartsInfo, parts, Parts);
				GETSET(string, myNick, MyNick);         // for NMDC support only
				GETSET(string, hubIpPort, HubIpPort);
				GETSET(IpAddress, ip, Ip);
				GETSET(uint64_t, nextQueryTime, NextQueryTime);
				GETSET(uint16_t, udpPort, UdpPort);
				GETSET(uint8_t, pendingQueryCount, PendingQueryCount);
				GETSET(int64_t, blockSize, BlockSize);
		};
		
		class Source : public Flags
		{
			public:
				enum
				{
					FLAG_NONE               = 0x000,
					FLAG_FILE_NOT_AVAILABLE = 0x001,
					FLAG_PASSIVE            = 0x002,
					FLAG_REMOVED            = 0x004,
					FLAG_NO_TTHF            = 0x008,
					FLAG_BAD_TREE           = 0x010,
					FLAG_SLOW_SOURCE        = 0x020,
					FLAG_NO_TREE            = 0x040,
					FLAG_NO_NEED_PARTS      = 0x080,
					FLAG_PARTIAL            = 0x100,
					FLAG_TTH_INCONSISTENCY  = 0x200,
					FLAG_UNTRUSTED          = 0x400,
					FLAG_MASK = FLAG_FILE_NOT_AVAILABLE
					            | FLAG_PASSIVE | FLAG_REMOVED | FLAG_BAD_TREE | FLAG_SLOW_SOURCE
					            | FLAG_NO_TREE | FLAG_TTH_INCONSISTENCY | FLAG_UNTRUSTED
				};
				
				Source()  {}
				
				bool isCandidate(const bool isBadSource) const
				{
					return isSet(FLAG_PARTIAL) && (isBadSource || !isSet(FLAG_TTH_INCONSISTENCY));
				}
				
				GETSET(PartialSource::Ptr, partialSource, PartialSource);
		};

		// used by getChunksVisualisation
		struct RunningSegment
		{
			int64_t start;
			int64_t end;
			int64_t pos;
		};
		
		typedef boost::unordered_map<UserPtr, Source, User::Hash> SourceMap;
		typedef SourceMap::iterator SourceIter;
		typedef SourceMap::const_iterator SourceConstIter;

		typedef std::set<Segment> SegmentSet;
		
		QueueItem(const string& aTarget, int64_t aSize, Priority aPriority, bool aAutoPriority, Flags::MaskType aFlag,
		          time_t aAdded, const TTHValue& tth, uint8_t maxSegments, const string& aTempTarget);
		          
		QueueItem(const QueueItem &) = delete;
		QueueItem& operator= (const QueueItem &) = delete;

		struct SourceListItem
		{
			uint64_t queryTime;
			QueueItemPtr qi;
			SourceConstIter si;
			bool operator< (const SourceListItem& other) const { return queryTime < other.queryTime; }
		};
		typedef boost::container::flat_multiset<SourceListItem> SourceList;

		static void getPFSSourcesL(const QueueItemPtr& qi, SourceList& sourceList, uint64_t now, size_t maxCount);

		bool countOnlineUsersGreatOrEqualThanL(const size_t maxValue) const;
		void getOnlineUsers(UserList& l) const;
		
#ifdef USE_QUEUE_RWLOCK
		static std::unique_ptr<RWLock> g_cs;
#else
		static std::unique_ptr<CriticalSection> g_cs;
#endif
		static std::atomic_bool checkTempDir;

		const SourceMap& getSourcesL()
		{
			return sources;
		}
		const SourceMap& getBadSourcesL()
		{
			return badSources;
		}
#ifdef _DEBUG
		bool isSourceValid(const QueueItem::Source* sourcePtr);
#endif
		size_t getSourcesCount() const
		{
			return sources.size();
		}
		SourceIter findSourceL(const UserPtr& user)
		{
			return sources.find(user);
		}
		SourceIter findBadSourceL(const UserPtr& user)
		{
			return badSources.find(user);
		}
		bool isSourceL(const UserPtr& user) const
		{
			return sources.find(user) != sources.end();
		}
		bool isBadSourceL(const UserPtr& user) const
		{
			return badSources.find(user) != badSources.end();
		}
		bool isBadSourceExceptL(const UserPtr& user, Flags::MaskType exceptions) const;
		void getChunksVisualisation(vector<RunningSegment>& running, vector<Segment>& done) const;
		bool isChunkDownloaded(int64_t startPos, int64_t& len) const;
		void setOverlapped(const Segment& segment, const bool isOverlapped);
		/**
		 * Is specified parts needed by this download?
		 */
		static bool isNeededPart(const PartsInfo& theirParts, const PartsInfo& ourParts);
		
		/**
		 * Get shared parts info, max 255 parts range pairs
		 */
		void getParts(PartsInfo& partialInfo, uint64_t blockSize) const;
		
		int64_t getDownloadedBytes() const { return downloadedBytes; }
		void updateDownloadedBytes();
		void updateDownloadedBytesAndSpeedL();
		void addDownload(const DownloadPtr& download);
		bool removeDownload(const UserPtr& user);
		size_t getDownloadsSegmentCount() const
		{
			return downloads.size();
		}
		bool disconnectSlow(const DownloadPtr& d);
		void disconnectOthers(const DownloadPtr& d);
		uint8_t calcActiveSegments() const;
		bool isDownloadTree() const;
		UserPtr getFirstUser() const;
		void getUsers(UserList& users) const;
		/** Next segment that is not done and not being downloaded, zero-sized segment returned if there is none is found */
		Segment getNextSegmentL(const int64_t blockSize, const int64_t wantedSize, const int64_t lastSpeed, const PartialSource::Ptr &partialSource) const;
		
		void addSegment(const Segment& segment);
		void addSegmentL(const Segment& segment);
		void resetDownloaded();
		void resetDownloadedL();
		
		bool isFinished() const;
		
		bool isRunning() const
		{
			return !isWaiting();
		}
		bool isWaiting() const
		{
			// fix lock - �� ��������! LOCK(m_fcs_download);
			return downloads.empty();
		}
		string getListName() const;
		const string& getTempTarget();
		const string& getTempTargetConst() const { return tempTarget; }

	private:
		MaskType flags;
		std::atomic_bool removed;
		const TTHValue tthRoot;
		uint64_t blockSize;

		Segment getNextSegmentForward(const int64_t blockSize, const int64_t targetSize, vector<Segment>* neededParts, const vector<int64_t>& posArray) const;
		Segment getNextSegmentBackward(const int64_t blockSize, const int64_t targetSize, vector<Segment>* neededParts, const vector<int64_t>& posArray) const;
		bool shouldSearchBackward() const;

	public:
		bool isSet(MaskType flag) const { return (flags & flag) == flag; }
		bool isAnySet(MaskType flag) const { return (flags & flag) != 0; }
		MaskType getFlags() const { return flags; }
		
		const TTHValue& getTTH() const
		{
			return tthRoot;
		}
		
		void updateBlockSize(uint64_t treeBlockSize);
		uint64_t getBlockSize() const
		{
			return blockSize;
		}
		
		DownloadList downloads;		
		mutable FastCriticalSection csDownloads;
		
		SegmentSet doneSegments;
		int64_t doneSegmentsSize;
		int64_t downloadedBytes;				
		mutable FastCriticalSection csSegments;
		
		void getDoneSegments(vector<Segment>& done) const;

		GETSET(uint64_t, timeFileBegin, TimeFileBegin);
		GETSET(int64_t, lastsize, LastSize);
		GETSET(time_t, added, Added);
#ifdef DEBUG_TRANSFERS
		GETSET(string, downloadPath, DownloadPath);
#endif
		
	private:
		Priority priority;
		string target;
		int64_t size;
		uint8_t maxSegments;
		bool autoPriority;

	public:
		bool getAutoPriority() const { return autoPriority; }
		void setAutoPriority(bool value) { autoPriority = value; }
		
		uint8_t getMaxSegments() const { return maxSegments; }
		void setMaxSegments(uint8_t value) { maxSegments = value; }
		
		int64_t getSize() const { return size; }
		void setSize(int64_t value) { size = value; }
		
		const string& getTarget() const { return target; }
		void setTarget(const string& value) { target = value; }
		
		Priority getPriority() const { return priority; }
		void setPriority(Priority value) { priority = value; }
		
		void setTempTarget(const string& value) { tempTarget = value; }
		
		int16_t getTransferFlags(int& flags) const;
		QueueItem::Priority calculateAutoPriority() const;
		
		bool isAutoDrop() const
		{
			return (flags & FLAG_AUTODROP) != 0;
		}
		
		void changeAutoDrop()
		{
			flags ^= FLAG_AUTODROP;
		}

		int64_t getAverageSpeed() const { return averageSpeed; }
		size_t getLastOnlineCount();
		static string getDCTempName(const string& fileName, const TTHValue* tth);

	private:
		int64_t averageSpeed;
		std::atomic_bool cachedOnlineSourceCountInvalid;
		size_t cachedOnlineSourceCount;
		SourceMap sources;
		SourceMap badSources;
		string tempTarget;
		
		SourceIter addSourceL(const UserPtr& user, bool isFirstLoad);
		void removeSourceL(const UserPtr& user, Flags::MaskType reason);

		friend class QueueManager;
};

#endif // !defined(QUEUE_ITEM_H)
