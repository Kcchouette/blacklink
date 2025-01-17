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

#ifndef CONNECTIVITY_MANAGER_H
#define CONNECTIVITY_MANAGER_H

#include "Singleton.h"
#include "MappingManager.h"
#include "IpAddress.h"
#include <atomic>

class ConnectivityManager : public Singleton<ConnectivityManager>
{
	public:
		void setupConnections(bool forcePortTest = false);
		bool isSetupInProgress() const noexcept { return getRunningFlags() != 0; }
		void processPortTestResult();
		void setReflectedIP(const IpAddress& ip) noexcept;
		IpAddress getReflectedIP(int af) const noexcept;
		void setLocalIP(const IpAddress& ip) noexcept;
		IpAddress getLocalIP(int af) const noexcept;
		const MappingManager& getMapper(int af) const;
		string getInformation() const;
		static bool isIP6Supported() { return ipv6Supported; }
		static void checkIP6();
		static bool hasIP6() { return ipv6Enabled; }

	private:
		friend class Singleton<ConnectivityManager>;
		friend class MappingManager;
		
		ConnectivityManager();
		virtual ~ConnectivityManager();

		void mappingFinished(const string& mapper, int af);
		void log(const string& msg, Severity sev, int af);
		
		void detectConnection(int af);
		void listenTCP(int af);
		void listenUDP(int af);
		void setPassiveMode(int af);
		void testPorts();
		bool setup(int af);
		void disconnect();
		unsigned getRunningFlags() const noexcept;

		IpAddress reflectedIP[2];
		IpAddress localIP[2];
		MappingManager mappers[2];
		mutable FastCriticalSection cs;
		unsigned running;
		bool forcePortTest;
		bool autoDetect[2];
		static std::atomic_bool ipv6Supported;
		static std::atomic_bool ipv6Enabled;
};

#endif // !defined(CONNECTIVITY_MANAGER_H)
