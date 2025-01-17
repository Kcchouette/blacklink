/*
 * Copyright (C) 2011-2013 Alexey Solomin, a.rainman on gmail pt com
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

#include "stdinc.h"

#  if defined(_MSC_VER) && _MSC_VER >= 1800 && _MSC_VER < 1900 && defined(_M_X64)
#    include <math.h> /* needed for _set_FMA3_enable */
#  endif

#define FLYLINKDC_USE_DISK_SPACE_INFO

#include <WinError.h>
#include <winnt.h>
#include <ImageHlp.h>
#include <shlwapi.h>
#ifdef OSVER_WIN_VISTA
#define PSAPI_VERSION 1
#endif
#include <psapi.h>
#include "Util.h"
#include "Socket.h"
#include "DownloadManager.h"
#include "UploadManager.h"
#include "CompatibilityManager.h"
#include "DatabaseManager.h"
#include "ShareManager.h"
#include <iphlpapi.h>
#include <direct.h>

#pragma comment(lib, "Imagehlp.lib")

string CompatibilityManager::g_incopatibleSoftwareList;
string CompatibilityManager::g_startupInfo;
DWORDLONG CompatibilityManager::g_TotalPhysMemory;
DWORDLONG CompatibilityManager::g_FreePhysMemory;
OSVERSIONINFOEX CompatibilityManager::g_osvi = {0};
SYSTEM_INFO CompatibilityManager::g_sysInfo = {0};
bool CompatibilityManager::g_supports[LAST_SUPPORTS];
LONG CompatibilityManager::g_comCtlVersion = 0;
DWORD CompatibilityManager::g_oldPriorityClass = 0;
FINDEX_INFO_LEVELS CompatibilityManager::findFileLevel = FindExInfoStandard;
// http://msdn.microsoft.com/ru-ru/library/windows/desktop/aa364415%28v=vs.85%29.aspx
// FindExInfoBasic
//     The FindFirstFileEx function does not query the short file name, improving overall enumeration speed.
//     The data is returned in a WIN32_FIND_DATA structure, and the cAlternateFileName member is always a NULL string.
//     Windows Server 2008, Windows Vista, Windows Server 2003, and Windows XP:  This value is not supported until Windows Server 2008 R2 and Windows 7.
DWORD CompatibilityManager::findFileFlags = 0;
// http://msdn.microsoft.com/ru-ru/library/windows/desktop/aa364419%28v=vs.85%29.aspx
// Uses a larger buffer for directory queries, which can increase performance of the find operation.
// Windows Server 2008, Windows Vista, Windows Server 2003, and Windows XP:  This value is not supported until Windows Server 2008 R2 and Windows 7.

#if defined(OSVER_WIN_XP) || defined(OSVER_WIN_VISTA)
DWORD CompatibilityManager::compareFlags = 0;
#endif

void CompatibilityManager::init()
{
#ifdef _WIN64
	// https://code.google.com/p/chromium/issues/detail?id=425120
	// FMA3 support in the 2013 CRT is broken on Vista and Windows 7 RTM (fixed in SP1). Just disable it.
	// fix crash https://drdump.com/Problem.aspx?ProblemID=102616
	//           https://drdump.com/Problem.aspx?ProblemID=102601
#if _MSC_VER >= 1800 && _MSC_VER < 1900
	_set_FMA3_enable(0);
#endif
#endif
	WSADATA wsaData = {0};
	WSAStartup(MAKEWORD(2, 2), &wsaData);

	setWine(detectWine());
	
	if (!isWine() && getFromSystemIsAppRunningIsWow64())
		set(RUNNING_IS_WOW64);
		
	detectOsSupports();
	getSystemInfoFromOS();
	generateSystemInfoForApp();
	if (CompatibilityManager::isWin7Plus())
	{
		findFileLevel = FindExInfoBasic;
		findFileFlags = FIND_FIRST_EX_LARGE_FETCH;
#if defined(OSVER_WIN_XP) || defined(OSVER_WIN_VISTA)
		compareFlags = SORT_DIGITSASNUMBERS;
#endif
	}
}

void CompatibilityManager::getSystemInfoFromOS()
{
	// http://msdn.microsoft.com/en-us/library/windows/desktop/ms724958(v=vs.85).aspx
	GetSystemInfo(&g_sysInfo);
}

void CompatibilityManager::detectOsSupports()
{
	g_osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEX);
	
	if (!GetVersionEx((OSVERSIONINFO*)&g_osvi))
		memset(&g_osvi, 0, sizeof(OSVERSIONINFOEX));
		
#define FUTURE_VER(future_major_version) \
	(getOsMajor() >= future_major_version)
		
#define FUTURE_MINOR_VER(current_major_version, future_minor_version) \
	(getOsMajor() == current_major_version && getOsMinor() >= future_minor_version)
		
#define CURRENT_VER(current_major_version, current_minor_version) \
	(getOsMajor() == current_major_version && getOsMinor() == current_minor_version)
		
#define CURRENT_VER_SP(current_major_version, current_minor_version, current_sp_version) \
	(getOsMajor() == current_major_version && getOsMinor() == current_minor_version && getOsSpMajor() == current_sp_version)
		
	if (FUTURE_VER(8) || // future version
	        FUTURE_MINOR_VER(10, 1) || // Windows 10 and newer
	        CURRENT_VER(10, 0)) // Windows 10
		set(OS_WINDOWS10_PLUS);
		
	if (FUTURE_VER(7) || // future version
	        FUTURE_MINOR_VER(6, 3) || // Windows 8.1
	        CURRENT_VER(6, 2)) // Windows 8
		set(OS_WINDOWS8_PLUS);
		
	if (FUTURE_VER(7) || // future version
	        FUTURE_MINOR_VER(6, 2) || // Windows 8 and newer
	        CURRENT_VER(6, 1)) // Windows 7
		set(OS_WINDOWS7_PLUS);
	if (FUTURE_VER(7) || // future version
	        FUTURE_MINOR_VER(6, 1) || // Windows 7 and newer
	        CURRENT_VER(6, 0)) // Windows Vista
		set(OS_VISTA_PLUS);
		
	if (FUTURE_VER(6) || // Windows Vista and newer
	        CURRENT_VER_SP(5, 2, 2) || // Windows Server 2003 SP2
	        CURRENT_VER_SP(5, 1, 3)) // Windows XP SP3
		set(OS_XP_SP3_PLUS);
		
#undef FUTURE_VER
#undef FUTURE_MINOR_VER
#undef CURRENT_VER
#undef CURRENT_VER_SP
	
	g_comCtlVersion = getComCtlVersionFromOS();
}

bool CompatibilityManager::detectWine()
{
	const HMODULE module = GetModuleHandle(_T("ntdll.dll"));
	if (!module)
		return false;
		
	const bool ret = GetProcAddress(module, "wine_server_call") != nullptr;
	FreeLibrary(module);
	return ret;
}

struct DllInfo
{
	const TCHAR* dllName;
	const char* info;
};

// TODO - config
static const DllInfo g_IncompatibleDll[] =
{
	_T("nvappfilter.dll"), "NVIDIA ActiveArmor (NVIDIA Firewall)",
	_T("nvlsp.dll"), "NVIDIA Application Filter",
	_T("nvlsp64.dll"), "NVIDIA Application Filter",
	_T("chtbrkg.dll"), "Adskip or Internet Download Manager integration",
	_T("adguard.dll"), "Adguard", // Latest verion 5.1 not bad.
	_T("pcapwsp.dll"), "ProxyCap",
	_T("NetchartFilter.dll"), "NetchartFilter",
	_T("vlsp.dll"), "Venturi Wireless Software",
	_T("radhslib.dll"), "Naomi web filter",
	_T("AmlMaple.dll"), "Aml Maple",
	_T("sdata.dll"), "Trojan-PSW.Win32.LdPinch.ajgw",
	_T("browsemngr.dll"), "Browser Manager",
	_T("owexplorer_10616.dll"), "Overwolf Overlay",
	_T("owexplorer_10615.dll"), "Overwolf Overlay",
	_T("owexplorer_20125.dll"), "Overwolf Overlay",
	_T("owexplorer_2006.dll"), "Overwolf Overlay",
	_T("owexplorer_20018.dll"), "Overwolf Overlay",
	_T("owexplorer_2000.dll"), "Overwolf Overlay",
	_T("owexplorer_20018.dll"), "Overwolf Overlay",
	_T("owexplorer_20015.dll"), "Overwolf Overlay",
	_T("owexplorer_1069.dll"), "Overwolf Overlay",
	_T("owexplorer_10614.dll"), "Overwolf Overlay",
	_T("am32_33707.dll"), "Ad Muncher",
	_T("am32_32562.dll"), "Ad Muncher",
	_T("am64_33707.dll"), "Ad Muncher x64",
	_T("am64_32130.dll"), "Ad Muncher x64",
	_T("browserprotect.dll"), "Browserprotect",
	_T("searchresultstb.dll"), "DTX Toolbar",
	_T("networx.dll"), "NetWorx",
	nullptr, nullptr
};

void CompatibilityManager::detectIncompatibleSoftware()
{
	const DllInfo* currentDllInfo = g_IncompatibleDll;
	// TODO http://stackoverflow.com/questions/420185/how-to-get-the-version-info-of-a-dll-in-c
	for (; currentDllInfo->dllName; ++currentDllInfo)
	{
		const HMODULE hIncompatibleDll = GetModuleHandle(currentDllInfo->dllName);
		if (hIncompatibleDll)
		{
			g_incopatibleSoftwareList += "\r\n";
			const auto l_dll = Text::fromT(currentDllInfo->dllName);
			g_incopatibleSoftwareList += l_dll;
			if (currentDllInfo->info)
			{
				g_incopatibleSoftwareList += " - ";
				g_incopatibleSoftwareList += currentDllInfo->info;
			}
		}
	}
	g_incopatibleSoftwareList.shrink_to_fit();
}

string CompatibilityManager::getIncompatibleSoftwareMessage()
{
	if (isIncompatibleSoftwareFound())
	{
		string temp;
		temp.resize(32768);
		temp.resize(sprintf_s(&temp[0], temp.size() - 1, CSTRING(INCOMPATIBLE_SOFTWARE_FOUND), g_incopatibleSoftwareList.c_str()));
		return temp;
	}
	return Util::emptyString;
}

string CompatibilityManager::getFormattedOsVersion()
{
	string s = Util::toString(getOsMajor()) + '.' + Util::toString(getOsMinor());
	if (getOsType() == VER_NT_SERVER)
		s += " Server";
	if (getOsSpMajor() > 0)
		s += " SP" + Util::toString(getOsSpMajor());
	if (isWine())
		s += " (Wine)";
	if (runningIsWow64())
		s += " (WOW64)";
	return s;
}

string CompatibilityManager::getProcArchString()
{
	string s;
	switch (g_sysInfo.wProcessorArchitecture)
	{
		case PROCESSOR_ARCHITECTURE_AMD64:
			s = " x86-x64";
			break;
		case PROCESSOR_ARCHITECTURE_INTEL:
			s = " x86";
			break;
		case PROCESSOR_ARCHITECTURE_IA64:
			s = " Intel Itanium-based";
			break;
		default: // PROCESSOR_ARCHITECTURE_UNKNOWN
			s = " Unknown";
			break;
	};
	return s;
}

string CompatibilityManager::getWindowsVersionName()
{
	string l_OS = "Microsoft Windows ";
	DWORD dwType;
	
// Check for unsupported OS
//	if (VER_PLATFORM_WIN32_NT != g_osvi.dwPlatformId || getOsMajor() <= 4)
//	{
//		return false;
//	}

	// Test for the specific product.
	// https://msdn.microsoft.com/ru-ru/library/windows/desktop/ms724833(v=vs.85).aspx
	if (getOsMajor() == 6 || getOsMajor() == 10)
	{
		if (getOsMajor() == 10)
		{
			if (getOsMinor() == 0)
			{
				if (getOsType() == VER_NT_WORKSTATION)
					l_OS += "10 ";
				else
					l_OS += "Windows Server 2016 Technical Preview ";
			}
			// check type for Win10: Desktop, Mobile, etc...
		}
		if (getOsMajor() == 6)
		{
			if (getOsMinor() == 0)
			{
				if (getOsType() == VER_NT_WORKSTATION)
					l_OS += "Vista ";
				else
					l_OS += "Server 2008 ";
			}
			if (getOsMinor() == 1)
			{
				if (getOsType() == VER_NT_WORKSTATION)
					l_OS += "7 ";
				else
					l_OS += "Server 2008 R2 ";
			}
			if (getOsMinor() == 2)
			{
				if (getOsType() == VER_NT_WORKSTATION)
					l_OS += "8 ";
				else
					l_OS += "Server 2012 ";
			}
			if (getOsMinor() == 3)
			{
				if (getOsType() == VER_NT_WORKSTATION)
					l_OS += "8.1 ";
				else
					l_OS += "Server 2012 R2 ";
			}
		}
		// Product Info  https://msdn.microsoft.com/en-us/library/windows/desktop/ms724358(v=vs.85).aspx
		typedef BOOL(WINAPI * PGPI)(DWORD, DWORD, DWORD, DWORD, PDWORD);
		PGPI pGPI = (PGPI)GetProcAddress(GetModuleHandle(TEXT("kernel32.dll")), "GetProductInfo");
		if (pGPI)
		{
			pGPI(getOsMajor(), getOsMinor(), 0, 0, &dwType);
			switch (dwType)
			{
				case PRODUCT_ULTIMATE:
					l_OS += "Ultimate Edition";
					break;
				case PRODUCT_PROFESSIONAL:
					l_OS += "Professional";
					break;
				case PRODUCT_PROFESSIONAL_N:
					l_OS += "Professional N";
					break;
				case PRODUCT_HOME_PREMIUM:
					l_OS += "Home Premium Edition";
					break;
				case PRODUCT_HOME_BASIC:
					l_OS += "Home Basic Edition";
					break;
				case PRODUCT_ENTERPRISE:
					l_OS += "Enterprise Edition";
					break;
				case PRODUCT_BUSINESS:
					l_OS += "Business Edition";
					break;
				case PRODUCT_STARTER:
					l_OS += "Starter Edition";
					break;
				case PRODUCT_CLUSTER_SERVER:
					l_OS += "Cluster Server Edition";
					break;
				case PRODUCT_DATACENTER_SERVER:
					l_OS += "Datacenter Edition";
					break;
				case PRODUCT_DATACENTER_SERVER_CORE:
					l_OS += "Datacenter Edition (core installation)";
					break;
				case PRODUCT_ENTERPRISE_SERVER:
					l_OS += "Enterprise Edition";
					break;
				case PRODUCT_ENTERPRISE_SERVER_CORE:
					l_OS += "Enterprise Edition (core installation)";
					break;
				case PRODUCT_ENTERPRISE_SERVER_IA64:
					l_OS += "Enterprise Edition for Itanium-based Systems";
					break;
				case PRODUCT_SMALLBUSINESS_SERVER:
					l_OS += "Small Business Server";
					break;
				case PRODUCT_SMALLBUSINESS_SERVER_PREMIUM:
					l_OS += "Small Business Server Premium Edition";
					break;
				case PRODUCT_STANDARD_SERVER:
					l_OS += "Standard Edition";
					break;
				case PRODUCT_STANDARD_SERVER_CORE:
					l_OS += "Standard Edition (core installation)";
					break;
				case PRODUCT_WEB_SERVER:
					l_OS += "Web Server Edition";
					break;
				case PRODUCT_UNLICENSED:
					l_OS += "Unlicensed";
					break;
					// Only Windows 10 support:
					// VC 2015 not supported these defines :(
					/*
					case PRODUCT_MOBILE_CORE:
					l_OS += "Mobile";
					break;
					case PRODUCT_MOBILE_ENTERPRISE:
					l_OS += "Mobile Enterprise";
					break;
					case PRODUCT_IOTUAP:
					l_OS += "IoT Core";
					break;
					case PRODUCT_IOTUAPCOMMERCIAL:
					l_OS += "IoT Core Commercial";
					break;
					case PRODUCT_EDUCATION:
					l_OS += "Education";
					break;
					case PRODUCT_ENTERPRISE_S:
					l_OS += "Enterprise 2015 LTSB";
					break;
					case PRODUCT_CORE:
					l_OS += "Home";
					break;
					case PRODUCT_CORE_SINGLELANGUAGE:
					l_OS += "Home Single Language";
					break;
					*/
			}
		}
	}
	if (getOsMajor() == 5)
	{
		if (getOsMinor() == 2)
		{
			if (GetSystemMetrics(SM_SERVERR2))
				l_OS += "Server 2003 R2, ";
			else if (getOsSuiteMask() & VER_SUITE_STORAGE_SERVER)
				l_OS += "Storage Server 2003";
			else if (getOsSuiteMask() & VER_SUITE_WH_SERVER)
				l_OS += "Home Server";
			else if (getOsType() == VER_NT_WORKSTATION && getProcArch() == PROCESSOR_ARCHITECTURE_AMD64)
			{
				l_OS += "XP Professional x64 Edition";
			}
			else
				l_OS += "Server 2003, ";  // Test for the server type.
			if (getOsType() != VER_NT_WORKSTATION)
			{
				if (getProcArch() == PROCESSOR_ARCHITECTURE_IA64)
				{
					if (getOsSuiteMask() & VER_SUITE_DATACENTER)
						l_OS += "Datacenter Edition for Itanium-based Systems";
					else if (getOsSuiteMask() & VER_SUITE_ENTERPRISE)
						l_OS += "Enterprise Edition for Itanium-based Systems";
				}
				else if (getProcArch() == PROCESSOR_ARCHITECTURE_AMD64)
				{
					if (getOsSuiteMask() & VER_SUITE_DATACENTER)
						l_OS += "Datacenter x64 Edition";
					else if (getOsSuiteMask() & VER_SUITE_ENTERPRISE)
						l_OS += "Enterprise x64 Edition";
					else
						l_OS += "Standard x64 Edition";
				}
				else
				{
					if (getOsSuiteMask() & VER_SUITE_COMPUTE_SERVER)
						l_OS += "Compute Cluster Edition";
					else if (getOsSuiteMask() & VER_SUITE_DATACENTER)
						l_OS += "Datacenter Edition";
					else if (getOsSuiteMask() & VER_SUITE_ENTERPRISE)
						l_OS += "Enterprise Edition";
					else if (getOsSuiteMask() & VER_SUITE_BLADE)
						l_OS += "Web Edition";
					else
						l_OS += "Standard Edition";
				}
			}
		}
		if (getOsMinor() == 1)
		{
			l_OS += "XP ";
			if (getOsSuiteMask() & VER_SUITE_PERSONAL)
				l_OS += "Home Edition";
			else
				l_OS += "Professional";
		}
		if (getOsMinor() == 0)
		{
			l_OS += "2000 ";
			if (getOsType() == VER_NT_WORKSTATION)
			{
				l_OS += "Professional";
			}
			else
			{
				if (getOsSuiteMask() & VER_SUITE_DATACENTER)
					l_OS += "Datacenter Server";
				else if (getOsSuiteMask() & VER_SUITE_ENTERPRISE)
					l_OS += "Advanced Server";
				else
					l_OS += "Server";
			}
		}
	}
	// Include service pack (if any) and build number.
	if (g_osvi.szCSDVersion[0] != 0)
	{
		l_OS += ' ' + Text::fromT(g_osvi.szCSDVersion);
	}
	/*  l_OS = l_OS + " (build " + g_osvi.dwBuildNumber + ")";*/
	if (g_osvi.dwMajorVersion >= 6)
	{
		if ((getProcArch() == PROCESSOR_ARCHITECTURE_AMD64) || runningIsWow64())
			l_OS += ", 64-bit";
		else if (getProcArch() == PROCESSOR_ARCHITECTURE_INTEL)
			l_OS += ", 32-bit";
	}
	return l_OS;
}

void CompatibilityManager::generateSystemInfoForApp()
{
	g_startupInfo = getAppNameVer();
	
	g_startupInfo += " is starting\r\n"
	                 "\tNumber of processors: " + Util::toString(getProcessorsCount()) + ".\r\n" +
	                 + "\tProcessor type: ";
	g_startupInfo += getProcArchString();
	g_startupInfo += ".\r\n";
	
	g_startupInfo += generateGlobalMemoryStatusMessage();
	g_startupInfo += "\r\n";
	
	g_startupInfo += "\tRunning in ";
#ifndef _WIN64
	if (runningIsWow64())
	{
		g_startupInfo += "Windows WOW64 ";
		g_startupInfo += "\r\n\r\n\tPlease consider using the x64 version!";
	}
	else
#endif
		g_startupInfo += "Windows native ";
		
	g_startupInfo += "\r\n\t";
	g_startupInfo += getWindowsVersionName();
	g_startupInfo += "  (" + CompatibilityManager::getFormattedOsVersion() + ")";
	
	g_startupInfo += "\r\n\r\n";
}

LONG CompatibilityManager::getComCtlVersionFromOS()
{
	typedef HRESULT (CALLBACK* DLLGETVERSIONPROC)(DLLVERSIONINFO *);
	LONG result = 0;
	HINSTANCE hInstDLL = LoadLibrary(_T("comctl32.dll"));
	if (hInstDLL)
	{
		DLLGETVERSIONPROC proc = (DLLGETVERSIONPROC) GetProcAddress(hInstDLL, "DllGetVersion");
		if (proc)
		{
			DLLVERSIONINFO dvi;
			memset(&dvi, 0, sizeof(dvi));
			dvi.cbSize = sizeof(dvi);
			if (SUCCEEDED(proc(&dvi)))
				result = MAKELONG(dvi.dwMinorVersion, dvi.dwMajorVersion);
		}
		else
			result = MAKELONG(0, 4);
		FreeLibrary(hInstDLL);
	}
	return result;
}

bool CompatibilityManager::getFromSystemIsAppRunningIsWow64()
{
	// http://msdn.microsoft.com/en-us/library/windows/desktop/ms684139(v=vs.85).aspx
	
	auto kernel32 = GetModuleHandle(_T("kernel32"));
	
	if (kernel32)
	{
		typedef BOOL (WINAPI * LPFN_ISWOW64PROCESS)(HANDLE, PBOOL);
		LPFN_ISWOW64PROCESS fnIsWow64Process = (LPFN_ISWOW64PROCESS) GetProcAddress(kernel32, "IsWow64Process");
		BOOL bIsWow64;
		if (fnIsWow64Process)
		{
			if (fnIsWow64Process(GetCurrentProcess(), &bIsWow64))
			{
				return bIsWow64 != FALSE;
			}
		}
	}
	return false;
}

bool CompatibilityManager::getGlobalMemoryStatusFromOS(MEMORYSTATUSEX* p_MsEx)
{
	// http://msdn.microsoft.com/en-us/library/windows/desktop/aa366770(v=vs.85).aspx
	
	auto kernel32 = GetModuleHandle(_T("kernel32"));
	if (kernel32)
	{
		typedef BOOL (WINAPI * LPFN_GLOBALMEMORYSTATUSEX)(LPMEMORYSTATUSEX);
		
		LPFN_GLOBALMEMORYSTATUSEX fnGlobalMemoryStatusEx;
		
		fnGlobalMemoryStatusEx = (LPFN_GLOBALMEMORYSTATUSEX) GetProcAddress(kernel32, "GlobalMemoryStatusEx");
		
		if (fnGlobalMemoryStatusEx != nullptr)
		{
			return fnGlobalMemoryStatusEx(p_MsEx) != FALSE;
		}
	}
	return false;
}

string CompatibilityManager::generateGlobalMemoryStatusMessage()
{
	MEMORYSTATUSEX curMemoryInfo = {0};
	curMemoryInfo.dwLength = sizeof(curMemoryInfo);
	
	if (getGlobalMemoryStatusFromOS(&curMemoryInfo))
	{
		g_TotalPhysMemory = curMemoryInfo.ullTotalPhys;
		string memoryInfo = "\tMemory info:\r\n";
		memoryInfo += "\t\tMemory usage:\t" + Util::toString(curMemoryInfo.dwMemoryLoad) + "%\r\n";
		memoryInfo += "\t\tPhysical memory total:\t" + Util::formatBytes(curMemoryInfo.ullTotalPhys) + "\r\n";
		memoryInfo += "\t\tPhysical memory free:\t" + Util::formatBytes(curMemoryInfo.ullAvailPhys) + " \r\n";
		return memoryInfo;
	}
	return Util::emptyString;
}

string CompatibilityManager::generateFullSystemStatusMessage()
{
	string root;
	return
	    getStartupInfo() +
	    STRING(CURRENT_SYSTEM_STATE) + ":\r\n" + generateGlobalMemoryStatusMessage() +
	    getIncompatibleSoftwareMessage() + "\r\n" +
	    DatabaseManager::getInstance()->getDBInfo(root);
}

string CompatibilityManager::generateNetworkStats()
{
	char buf[1024];
	sprintf_s(buf, sizeof(buf),
	          "-=[ TCP: Received: %s. Sent: %s ]=-\r\n"
	          "-=[ UDP: Received: %s. Sent: %s ]=-\r\n"
	          "-=[ SSL: Received: %s. Sent: %s ]=-\r\n",
	          Util::formatBytes(Socket::g_stats.tcp.downloaded).c_str(), Util::formatBytes(Socket::g_stats.tcp.uploaded).c_str(),
	          Util::formatBytes(Socket::g_stats.udp.downloaded).c_str(), Util::formatBytes(Socket::g_stats.udp.uploaded).c_str(),
	          Util::formatBytes(Socket::g_stats.ssl.downloaded).c_str(), Util::formatBytes(Socket::g_stats.ssl.uploaded).c_str()
	         );
	return buf;
}

void CompatibilityManager::caclPhysMemoryStat()
{
	// Total RAM
	MEMORYSTATUSEX curMem = {0};
	curMem.dwLength = sizeof(curMem);
	g_FreePhysMemory = 0;
	g_TotalPhysMemory = 0;
	if (getGlobalMemoryStatusFromOS(&curMem))
	{
		g_TotalPhysMemory = curMem.ullTotalPhys;
		g_FreePhysMemory = curMem.ullAvailPhys;
	}
}

string CompatibilityManager::generateProgramStats() // moved from WinUtil
{
	char buf[1024 * 2];
	buf[0] = 0;
	const HINSTANCE hInstPsapi = LoadLibrary(_T("psapi"));
	if (hInstPsapi)
	{
		typedef bool (CALLBACK * LPFUNC)(HANDLE Process, PPROCESS_MEMORY_COUNTERS ppsmemCounters, DWORD cb);
		LPFUNC _GetProcessMemoryInfo = (LPFUNC)GetProcAddress(hInstPsapi, "GetProcessMemoryInfo");
		if (_GetProcessMemoryInfo)
		{
			PROCESS_MEMORY_COUNTERS pmc = {0};
			pmc.cb = sizeof(pmc);
			_GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc));
			extern int g_RAM_PeakWorkingSetSize;
			extern int g_RAM_WorkingSetSize;
			g_RAM_WorkingSetSize = pmc.WorkingSetSize >> 20;
			g_RAM_PeakWorkingSetSize = pmc.PeakWorkingSetSize >> 20;
			caclPhysMemoryStat();
#ifdef FLYLINKDC_USE_LASTIP_AND_USER_RATIO
			dcassert(DatabaseManager::isValidInstance());
			if (DatabaseManager::isValidInstance())
			{
				DatabaseManager::getInstance()->loadGlobalRatio();
			}
#endif
			sprintf_s(buf, sizeof(buf),
			          "\r\n\t-=[ %s "
			          "Compiled on: %s ]=-\r\n"
			          "\t-=[ OS: %s ]=-\r\n"
			          "\t-=[ Memory (free): %s (%s) ]=-\r\n"
			          "\t-=[ Uptime: %s. Client Uptime: %s ]=-\r\n"
			          "\t-=[ RAM (peak): %s (%s). Virtual (peak): %s (%s) ]=-\r\n"
			          "\t-=[ GDI objects (peak): %d (%d). Handles (peak): %d (%d) ]=-\r\n"
			          "\t-=[ Share: %s. Files in share: %u. Total users: %u on %u hubs ]=-\r\n"
#ifdef FLYLINKDC_USE_LASTIP_AND_USER_RATIO
			          "\t-=[ Total downloaded: %s. Total uploaded: %s ]=-\r\n"
#endif
			          "%s",
				getAppNameVer().c_str(),
				__DATE__,
				CompatibilityManager::getWindowsVersionName().c_str(),
				Util::formatBytes(g_TotalPhysMemory).c_str(),
				Util::formatBytes(g_FreePhysMemory).c_str(),
				getSysUptime().c_str(),
				Util::formatTime(Util::getUpTime()).c_str(),
				Util::formatBytes((uint64_t) pmc.WorkingSetSize).c_str(),
				Util::formatBytes((uint64_t) pmc.PeakWorkingSetSize).c_str(),
				Util::formatBytes((uint64_t) pmc.PagefileUsage).c_str(),
				Util::formatBytes((uint64_t) pmc.PeakPagefileUsage).c_str(),
				GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS),
				GetGuiResources(GetCurrentProcess(), 2/* GR_GDIOBJECTS_PEAK */),
				GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS),
				GetGuiResources(GetCurrentProcess(), 4 /*GR_USEROBJECTS_PEAK*/),
				Util::formatBytes(ShareManager::getInstance()->getTotalSharedSize()).c_str(),
				static_cast<unsigned>(ShareManager::getInstance()->getTotalSharedFiles()),
				static_cast<unsigned>(ClientManager::getTotalUsers()),
				Client::getTotalCounts(),
#ifdef FLYLINKDC_USE_LASTIP_AND_USER_RATIO
				Util::formatBytes(DatabaseManager::getInstance()->getGlobalRatio().download).c_str(),
				Util::formatBytes(DatabaseManager::getInstance()->getGlobalRatio().upload).c_str(),
#endif
				generateNetworkStats().c_str());
		}
		FreeLibrary(hInstPsapi);
	}
	return buf;
}

WORD CompatibilityManager::getDllPlatform(const string& fullpath)
{
	WORD bRet = IMAGE_FILE_MACHINE_UNKNOWN;
	PLOADED_IMAGE imgLoad = ::ImageLoad(Text::fromUtf8(fullpath).c_str(), Util::emptyString.c_str()); // TODO: IRainman: I don't know to use unicode here, Windows sucks.
	if (imgLoad && imgLoad->FileHeader)
	{
		bRet = imgLoad->FileHeader->FileHeader.Machine;
	}
	if (imgLoad)
	{
		::ImageUnload(imgLoad);
	}
	
	return bRet;
}

void CompatibilityManager::reduceProcessPriority()
{
	if (!g_oldPriorityClass || g_oldPriorityClass == GetPriorityClass(GetCurrentProcess()))
	{
		// TODO: refactoring this code to use step-up and step-down of the process priority change.
		g_oldPriorityClass = GetPriorityClass(GetCurrentProcess());
		SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);
		// [-] SetProcessWorkingSetSize(GetCurrentProcess(), -1, -1); [-] IRainman: this memory "optimization" for lemmings only.
	}
}

void CompatibilityManager::restoreProcessPriority()
{
	if (g_oldPriorityClass)
		SetPriorityClass(GetCurrentProcess(), g_oldPriorityClass);
}

// AirDC++ code
// ToDo:  Move all functions into Util
string CompatibilityManager::Speedinfo()
{
	string result = "\r\n\t";
	result += "-=[ ";
	result += "Dn. speed: ";
	result += Util::formatBytes(DownloadManager::getRunningAverage()) + "/s  (";
	result += Util::toString(DownloadManager::getDownloadCount()) + " fls.)";
	//result += " =- ";
	//result += " -= ";
	result += ". ";
	result += "Upl. speed: ";
	result += Util::formatBytes(UploadManager::getRunningAverage()) + "/s  (";
	result += Util::toString(UploadManager::getInstance()->getUploadCount()) + " fls.)";
	result += " ]=-";
	return result;
}

string CompatibilityManager::DiskSpaceInfo(bool onlyTotal /* = false */)
{
	string ret;
#ifdef FLYLINKDC_USE_DISK_SPACE_INFO
	int64_t free = 0, totalFree = 0, size = 0, totalSize = 0, netFree = 0, netSize = 0;
	const TStringList volumes = FindVolumes();
	for (auto i = volumes.cbegin(); i != volumes.cend(); ++i)
	{
		const auto l_drive_type = GetDriveType((*i).c_str());
		if (l_drive_type == DRIVE_CDROM || l_drive_type == DRIVE_REMOVABLE) // Not score USB flash, SD, SDMC, DVD, CD
			continue;
		if (GetDiskFreeSpaceEx((*i).c_str(), NULL, (PULARGE_INTEGER)&size, (PULARGE_INTEGER)&free))
		{
			totalFree += free;
			totalSize += size;
		}
	}
	
	//check for mounted Network drives
	DWORD drives = GetLogicalDrives();
	TCHAR drive[3] = { _T('C'), _T(':'), _T('\0') }; // TODO фиксануть copy-paste
	while (drives != 0)
	{
		const auto l_drive_type = GetDriveType(drive);
		if (drives & 1 && (l_drive_type != DRIVE_CDROM && l_drive_type != DRIVE_REMOVABLE && l_drive_type == DRIVE_REMOTE)) // only real drives, partitions
		{
			if (GetDiskFreeSpaceEx(drive, NULL, (PULARGE_INTEGER)&size, (PULARGE_INTEGER)&free))
			{
				netFree += free;
				netSize += size;
			}
			
		}
		++drive[0];
		drives = (drives >> 1);
	}
	if (totalSize != 0)
		if (!onlyTotal)
		{
			ret += "\r\n\t-=[ All HDD space (free/total): " + Util::formatBytes(totalFree) + "/" + Util::formatBytes(totalSize) + " ]=-";
			if (netSize != 0)
			{
				ret += "\r\n\t-=[ Network space (free/total): " + Util::formatBytes(netFree) + "/" + Util::formatBytes(netSize) + " ]=-";
				ret += "\r\n\t-=[ Network + HDD space (free/total): " + Util::formatBytes((netFree + totalFree)) + "/" + Util::formatBytes(netSize + totalSize) + " ]=-";
			}
		}
		else
		{
			ret += Util::formatBytes(totalFree) + "/" + Util::formatBytes(totalSize);
		}
#endif // FLYLINKDC_USE_DISK_SPACE_INFO
	return ret;
}

TStringList CompatibilityManager::FindVolumes()
{
	TCHAR   buf[MAX_PATH];
	buf[0] = 0;
	TStringList volumes;
	HANDLE hVol = FindFirstVolume(buf, MAX_PATH);
	if (hVol != INVALID_HANDLE_VALUE)
	{
		volumes.push_back(buf);
		BOOL found = FindNextVolume(hVol, buf, MAX_PATH);
		//while we find drive volumes.
		while (found)
		{
			volumes.push_back(buf);
			found = FindNextVolume(hVol, buf, MAX_PATH);
		}
		found = FindVolumeClose(hVol);
	}
	return volumes;
}

tstring CompatibilityManager::diskInfo()
{
	tstring result;
#ifdef FLYLINKDC_USE_DISK_SPACE_INFO
	int64_t free = 0, size = 0, totalFree = 0, totalSize = 0;
	int disk_count = 0;
	std::vector<tstring> results; //add in vector for sorting, nicer to look at :)
	// lookup drive volumes.
	TStringList volumes = FindVolumes();
	for (auto i = volumes.cbegin(); i != volumes.cend(); ++i)
	{
		const auto l_drive_type = GetDriveType((*i).c_str());
		if (l_drive_type == DRIVE_CDROM || l_drive_type == DRIVE_REMOVABLE) // Not score USB flash, SD, SDMC, DVD, CD
			continue;
		TCHAR   buf[MAX_PATH];
		buf[0] = 0;
		if ((GetVolumePathNamesForVolumeName((*i).c_str(), buf, 256, NULL) != 0) &&
		        (GetDiskFreeSpaceEx((*i).c_str(), NULL, (PULARGE_INTEGER)&size, (PULARGE_INTEGER)&free) != 0))
		{
			const tstring mountpath = buf;
			if (!mountpath.empty())
			{
				totalFree += free;
				totalSize += size;
				results.push_back((_T("\t-=[ Disk ") + mountpath + _T(" space (free/total): ") + Util::formatBytesW(free) + _T("/") + Util::formatBytesW(size) + _T(" ]=-")));
			}
		}
	}
	
	// and a check for mounted Network drives, todo fix a better way for network space
	DWORD drives = GetLogicalDrives();
	TCHAR drive[3] = { _T('C'), _T(':'), _T('\0') };
	
	while (drives != 0)
	{
		const auto l_drive_type = GetDriveType(drive);
		if (drives & 1 && (l_drive_type != DRIVE_CDROM && l_drive_type != DRIVE_REMOVABLE && l_drive_type == DRIVE_REMOTE)) // TODO фиксануть copy-paste
		{
			if (GetDiskFreeSpaceEx(drive, NULL, (PULARGE_INTEGER)&size, (PULARGE_INTEGER)&free))
			{
				totalFree += free;
				totalSize += size;
				results.push_back((_T("\t-=[ Network ") + (tstring)drive + _T(" space (free/total): ") + Util::formatBytesW(free) + _T("/") + Util::formatBytesW(size) + _T(" ]=-")));
			}
		}
		++drive[0];
		drives = (drives >> 1);
	}
	
	sort(results.begin(), results.end()); //sort it
	for (auto i = results.cbegin(); i != results.end(); ++i)
	{
		disk_count++;
		result += _T("\r\n ") + *i;
	}
	result += _T("\r\n\r\n\t-=[ All HDD space (free/total): ") + Util::formatBytesW((totalFree)) + _T("/") + Util::formatBytesW(totalSize) + _T(" ]=-");
	result += _T("\r\n\t-=[ Total Drives count: ") + Text::toT(Util::toString(disk_count)) + _T(" ]=-");
	results.clear();
#endif // FLYLINKDC_USE_DISK_SPACE_INFO
	return result;
}

string CompatibilityManager::CPUInfo()
{
	tstring result;
	HKEY key = nullptr;
	if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, _T("Hardware\\Description\\System\\CentralProcessor\\0"), 0, KEY_READ, &key) == ERROR_SUCCESS)
	{
		TCHAR buf[256];
		buf[0] = 0;
		DWORD type;
		DWORD size = sizeof(buf);
		if (RegQueryValueEx(key, _T("ProcessorNameString"), nullptr, &type, (BYTE *) buf, &size) == ERROR_SUCCESS && type == REG_SZ)
		{
			size /= sizeof(TCHAR);
			if (size && buf[size-1] == 0) size--;
			result.assign(buf, size);
		}
		DWORD speed;
		size = sizeof(DWORD);
		if (RegQueryValueEx(key, _T("~MHz"), nullptr, &type, (BYTE *) &speed, &size) == ERROR_SUCCESS && type == REG_DWORD)
		{
			result += _T(" (");
			result += Util::toStringT(speed);
			result += _T(" MHz)");
		}
		RegCloseKey(key);
	}
	return result.empty() ? "Unknown" : Text::fromT(result);
}

string CompatibilityManager::getSysUptime()
{
	static HINSTANCE kernel32lib = NULL;
	if (!kernel32lib)
		kernel32lib = LoadLibrary(_T("kernel32"));
		
	typedef ULONGLONG(CALLBACK * LPFUNC2)(void);
	LPFUNC2 _GetTickCount64 = (LPFUNC2)GetProcAddress(kernel32lib, "GetTickCount64");
	uint64_t sysUptime = (_GetTickCount64 ? _GetTickCount64() : GetTickCount()) / 1000;
	
	return Util::formatTime(sysUptime, false);
}
