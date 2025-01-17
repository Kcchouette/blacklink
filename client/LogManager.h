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


#ifndef DCPLUSPLUS_DCPP_LOG_MANAGER_H
#define DCPLUSPLUS_DCPP_LOG_MANAGER_H

#include "File.h"
#include "Locks.h"

namespace Util
{
	class ParamExpander;
}

class LogManager
{
	public:
		enum
		{
			CHAT,
			PM,
			DOWNLOAD,
			UPLOAD,
			SYSTEM,
			STATUS,
			WEBSERVER,
			SQLITE_TRACE,
			DDOS_TRACE,
#ifdef FLYLINKDC_USE_TORRENT
			TORRENT_TRACE,
#endif
			SEARCH_TRACE,
			DHT_TRACE,
			PSR_TRACE,
			FLOOD_TRACE,
			TCP_MESSAGES,
			UDP_PACKETS,
			LAST
		};

		// Flags for commandTrace
		enum
		{
			FLAG_IN  = 1,
			FLAG_UDP = 2
		};
		             
		static void init();
		static void log(int area, const string& msg) noexcept;
		static void log(int area, const StringMap& params) noexcept;
		static void log(int area, Util::ParamExpander* ex) noexcept;
		static void ddos_message(const string& message) noexcept;
		static void flood_message(const string& message) noexcept;
#ifdef FLYLINKDC_USE_TORRENT
		static void torrent_message(const string& message, bool addToSystem = true) noexcept;
#endif
		static void message(const string& msg, bool useStatus = true) noexcept;
		static void commandTrace(const string& msg, int flags, const string& ip, int port) noexcept;
		static void speakStatusMessage(const string& message) noexcept;
		static void getOptions(int area, TStringPair& p) noexcept;
		static void setOptions(int area, const TStringPair& p) noexcept;
		static void closeOldFiles(int64_t now) noexcept;
		static string getLogFileName(int area, const StringMap& params) noexcept;

#ifdef _WIN32
		static HWND g_mainWnd;
#endif
		static bool g_isLogSpeakerEnabled;
		static int  g_LogMessageID;

	private:
		static bool g_isInit;
		static int64_t nextCloseTime;
		
		LogManager();
		~LogManager()
		{
		}

		struct LogFile
		{
			File file;
			int64_t timeout;

			LogFile() {}
			LogFile(const LogFile&) = delete;
			LogFile& operator= (LogFile&) = delete;
		};
		
		struct LogArea
		{
			CriticalSection cs;
			boost::unordered_map<string, LogFile> files;
			int fileOption;
			int formatOption;
		};

		static LogArea types[LAST];		

		static void logRaw(int area, const string& msg, Util::ParamExpander* ex) noexcept;
};

#define LOG(area, msg) LogManager::log(LogManager::area, msg)

class StepLogger
{
	public:
		StepLogger(const string& message, bool skipStart = true, bool skipStop = false);
		~StepLogger();
		void step(const string& what);

	private:
		const string message;
		const bool skipStop;
		uint64_t startTime;
		uint64_t stepTime;
};

#endif // DCPLUSPLUS_DCPP_LOG_MANAGER_H
