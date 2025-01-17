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

#include "stdafx.h"

#include "../client/File.h"
#include "Resource.h"

#include <atlwin.h>
#include <shlobj.h>
#include <mmsystem.h>

#define COMPILE_MULTIMON_STUBS 1
#include <MultiMon.h>
#include <powrprof.h>

#include "WinUtil.h"
#include "LineDlg.h"

#include "../client/SimpleStringTokenizer.h"
#include "../client/ShareManager.h"
#include "../client/UploadManager.h"
#include "../client/HashManager.h"
#include "../client/File.h"
#include "../client/DownloadManager.h"
#include "../client/QueueManager.h"
#include "../client/ParamExpander.h"
#include "../client/MagnetLink.h"
#include "../client/NetworkUtil.h"
#include "Colors.h"
#include "Fonts.h"
#include "MagnetDlg.h"
#include "UserInfoBaseHandler.h"
#include "PreviewMenu.h"
#include "FlatTabCtrl.h"
#include "HubFrame.h"
#include "SearchFrm.h"
#include "DirectoryListingFrm.h"

const WinUtil::FileMaskItem WinUtil::fileListsMask[] =
{
	{ ResourceManager::FILEMASK_FILE_LISTS, _T("*.xml.bz2;*.dcls;*.dclst") },
	{ ResourceManager::FILEMASK_XML_BZ2,    _T("*.xml.bz2")                },
	{ ResourceManager::FILEMASK_DCLST,      _T("*.dcls;*.dclst")           },
	{ ResourceManager::FILEMASK_ALL,        _T("*.*")                      },
	{ ResourceManager::Strings(),           nullptr                        }
};

const WinUtil::FileMaskItem WinUtil::allFilesMask[] =
{
	{ ResourceManager::FILEMASK_ALL, _T("*.*") },
	{ ResourceManager::Strings(),    nullptr   }
};

CMenu WinUtil::g_mainMenu;

OMenu WinUtil::g_copyHubMenu;

HIconWrapper WinUtil::g_banIconOnline(IDR_BANNED_ONLINE);
HIconWrapper WinUtil::g_banIconOffline(IDR_BANNED_OFF);

TStringList LastDir::dirs;
HWND WinUtil::g_mainWnd = nullptr;
HWND WinUtil::g_mdiClient = nullptr;
FlatTabCtrl* WinUtil::g_tabCtrl = nullptr;
HHOOK WinUtil::g_hook = nullptr;
bool WinUtil::hubUrlHandlersRegistered = false;
bool WinUtil::magnetHandlerRegistered = false;
bool WinUtil::dclstHandlerRegistered = false;
bool WinUtil::g_isAppActive = false;

const GUID WinUtil::guidGetTTH = { 0xbc034ae0, 0x40d8, 0x465d, { 0xb1, 0xf0, 0x01, 0xd9, 0xd8, 0x83, 0x7f, 0x96 } };

HLSCOLOR RGB2HLS(COLORREF rgb)
{
	unsigned char minval = min(GetRValue(rgb), min(GetGValue(rgb), GetBValue(rgb)));
	unsigned char maxval = max(GetRValue(rgb), max(GetGValue(rgb), GetBValue(rgb)));
	float mdiff  = float(maxval) - float(minval);
	float msum   = float(maxval) + float(minval);
	
	float luminance = msum / 510.0f;
	float saturation = 0.0f;
	float hue = 0.0f;
	
	if (maxval != minval)
	{
		float rnorm = (maxval - GetRValue(rgb)) / mdiff;
		float gnorm = (maxval - GetGValue(rgb)) / mdiff;
		float bnorm = (maxval - GetBValue(rgb)) / mdiff;
		
		saturation = (luminance <= 0.5f) ? (mdiff / msum) : (mdiff / (510.0f - msum));
		
		if (GetRValue(rgb) == maxval) hue = 60.0f * (6.0f + bnorm - gnorm);
		if (GetGValue(rgb) == maxval) hue = 60.0f * (2.0f + rnorm - bnorm);
		if (GetBValue(rgb) == maxval) hue = 60.0f * (4.0f + gnorm - rnorm);
		if (hue > 360.0f) hue = hue - 360.0f;
	}
	return HLS((hue * 255) / 360, luminance * 255, saturation * 255);
}

static BYTE _ToRGB(float rm1, float rm2, float rh)
{
	if (rh > 360.0f) rh -= 360.0f;
	else if (rh <   0.0f) rh += 360.0f;
	
	if (rh <  60.0f) rm1 = rm1 + (rm2 - rm1) * rh / 60.0f;
	else if (rh < 180.0f) rm1 = rm2;
	else if (rh < 240.0f) rm1 = rm1 + (rm2 - rm1) * (240.0f - rh) / 60.0f;
	
	return (BYTE)(rm1 * 255);
}

static inline bool EqualD(double a, double b)
{
	return fabs(a - b) <= 1e-6;
}

COLORREF HLS2RGB(HLSCOLOR hls)
{
	float hue        = ((int)HLS_H(hls) * 360) / 255.0f;
	float luminance  = HLS_L(hls) / 255.0f;
	float saturation = HLS_S(hls) / 255.0f;
	
	if (EqualD(saturation, 0))
	{
		return RGB(HLS_L(hls), HLS_L(hls), HLS_L(hls));
	}
	float rm1, rm2;
	
	if (luminance <= 0.5f) rm2 = luminance + luminance * saturation;
	else                     rm2 = luminance + saturation - luminance * saturation;
	rm1 = 2.0f * luminance - rm2;
	BYTE red   = _ToRGB(rm1, rm2, hue + 120.0f);
	BYTE green = _ToRGB(rm1, rm2, hue);
	BYTE blue  = _ToRGB(rm1, rm2, hue - 120.0f);
	
	return RGB(red, green, blue);
}

COLORREF HLS_TRANSFORM(COLORREF rgb, int percent_L, int percent_S)
{
	HLSCOLOR hls = RGB2HLS(rgb);
	hls = HLS_TRANSFORM2(hls, percent_L, percent_S);
	return HLS2RGB(hls);
}

COLORREF HLS_TRANSFORM2(HLSCOLOR hls, int percent_L, int percent_S)
{
	BYTE h = HLS_H(hls);
	BYTE l = HLS_L(hls);
	BYTE s = HLS_S(hls);
	
	if (percent_L > 0)
	{
		l = BYTE(l + ((255 - l) * percent_L) / 100);
	}
	else if (percent_L < 0)
	{
		l = BYTE((l * (100 + percent_L)) / 100);
	}
	if (percent_S > 0)
	{
		s = BYTE(s + ((255 - s) * percent_S) / 100);
	}
	else if (percent_S < 0)
	{
		s = BYTE((s * (100 + percent_S)) / 100);
	}
	return HLS(h, l, s);
}

dcdrun(bool WinUtil::g_staticMenuUnlinked = true;)
void WinUtil::unlinkStaticMenus(OMenu& menu)
{
	dcdrun(g_staticMenuUnlinked = true;)
	MENUITEMINFO mif = { sizeof MENUITEMINFO };
	mif.fMask = MIIM_SUBMENU;
	for (int i = menu.GetMenuItemCount()-1; i >= 0; i--)
	{
		menu.GetMenuItemInfo(i, TRUE, &mif);
		if (UserInfoGuiTraits::isUserInfoMenu(mif.hSubMenu) ||
		    mif.hSubMenu == g_copyHubMenu.m_hMenu ||
		    PreviewMenu::isPreviewMenu(mif.hSubMenu))
		{
			menu.RemoveMenu(i, MF_BYPOSITION);
		}
	}
}

void WinUtil::escapeMenu(tstring& text)
{
	string::size_type i = 0;
	while ((i = text.find(_T('&'), i)) != string::npos)
	{
		text.insert(i, 1, _T('&'));
		i += 2;
	}
}

const tstring& WinUtil::escapeMenu(const tstring& text, tstring& tmp)
{
	string::size_type i = text.find(_T('&'));
	if (i == string::npos) return text;
	tmp = text;
	tmp.insert(i, 1, _T('&'));
	i += 2;
	while ((i = tmp.find(_T('&'), i)) != string::npos)
	{
		tmp.insert(i, 1, _T('&'));
		i += 2;
	}
	return tmp;
}

void WinUtil::appendSeparator(HMENU hMenu)
{
	CMenuHandle menu(hMenu);
	int count = menu.GetMenuItemCount();
	if (!count) return;
	UINT state = menu.GetMenuState(count-1, MF_BYPOSITION);
	if ((state & MF_POPUP) || !(state & MF_SEPARATOR))
		menu.AppendMenu(MF_SEPARATOR);
}

void WinUtil::appendSeparator(OMenu& menu)
{
	int count = menu.GetMenuItemCount();
	if (!count) return;
	UINT state = menu.GetMenuState(count-1, MF_BYPOSITION);
	if ((state & MF_POPUP) || !(state & MF_SEPARATOR))
		menu.AppendMenu(MF_SEPARATOR);
}

static LRESULT CALLBACK KeyboardProc(int code, WPARAM wParam, LPARAM lParam)
{
	if (code == HC_ACTION)
	{
		if (WinUtil::g_tabCtrl) //[+]PPA
			if (wParam == VK_CONTROL && LOWORD(lParam) == 1)
			{
				if (lParam & 0x80000000)
				{
					WinUtil::g_tabCtrl->endSwitch();
				}
				else
				{
					WinUtil::g_tabCtrl->startSwitch();
				}
			}
	}
	return CallNextHookEx(WinUtil::g_hook, code, wParam, lParam);
}

void WinUtil::init(HWND hWnd)
{
	g_mainWnd = hWnd;
	
	PreviewMenu::init();
	
	g_mainMenu.CreateMenu();
	
	CMenuHandle file;
	file.CreatePopupMenu();
	
	file.AppendMenu(MF_STRING, IDC_OPEN_FILE_LIST, CTSTRING(MENU_OPEN_FILE_LIST));
	file.AppendMenu(MF_STRING, IDC_ADD_MAGNET, CTSTRING(MENU_ADD_MAGNET));
#ifdef FLYLINKDC_USE_TORRENT
	file.AppendMenu(MF_STRING, IDC_OPEN_TORRENT_FILE, CTSTRING(MENU_OPEN_TORRENT_FILE));
#endif
	file.AppendMenu(MF_STRING, IDC_OPEN_MY_LIST, CTSTRING(MENU_OPEN_OWN_LIST));
	file.AppendMenu(MF_STRING, IDC_REFRESH_FILE_LIST, CTSTRING(MENU_REFRESH_FILE_LIST));
	file.AppendMenu(MF_STRING, IDC_MATCH_ALL, CTSTRING(MENU_OPEN_MATCH_ALL));
#if 0
	file.AppendMenu(MF_STRING, IDC_REFRESH_FILE_LIST_PURGE, CTSTRING(MENU_REFRESH_FILE_LIST_PURGE));
#endif
	file.AppendMenu(MF_STRING, IDC_OPEN_DOWNLOADS, CTSTRING(MENU_OPEN_DOWNLOADS_DIR));
	file.AppendMenu(MF_SEPARATOR);
	file.AppendMenu(MF_STRING, IDC_OPEN_LOGS, CTSTRING(MENU_OPEN_LOGS_DIR));
	file.AppendMenu(MF_STRING, IDC_OPEN_CONFIGS, CTSTRING(MENU_OPEN_CONFIG_DIR));
	file.AppendMenu(MF_SEPARATOR);
	file.AppendMenu(MF_STRING, ID_GET_TTH, CTSTRING(MENU_TTH));
	file.AppendMenu(MF_SEPARATOR);
	file.AppendMenu(MF_STRING, IDC_RECONNECT, CTSTRING(MENU_RECONNECT));
	file.AppendMenu(MF_STRING, IDC_RECONNECT_DISCONNECTED, CTSTRING(MENU_RECONNECT_DISCONNECTED));
	file.AppendMenu(MF_STRING, IDC_FOLLOW, CTSTRING(MENU_FOLLOW_REDIRECT));
	file.AppendMenu(MF_STRING, IDC_QUICK_CONNECT, CTSTRING(MENU_QUICK_CONNECT));
	file.AppendMenu(MF_SEPARATOR);
	file.AppendMenu(MF_STRING, ID_FILE_SETTINGS, CTSTRING(MENU_SETTINGS));
	file.AppendMenu(MF_SEPARATOR);
	file.AppendMenu(MF_STRING, ID_APP_EXIT, CTSTRING(MENU_EXIT));
	
	g_mainMenu.AppendMenu(MF_POPUP, (UINT_PTR)(HMENU)file, CTSTRING(MENU_FILE));
	
	CMenuHandle view;
	view.CreatePopupMenu();
	view.AppendMenu(MF_STRING, IDC_PUBLIC_HUBS, CTSTRING(MENU_PUBLIC_HUBS));
	view.AppendMenu(MF_STRING, IDC_RECENTS, CTSTRING(MENU_FILE_RECENT_HUBS));
	view.AppendMenu(MF_STRING, IDC_FAVORITES, CTSTRING(MENU_FAVORITE_HUBS));
	view.AppendMenu(MF_SEPARATOR);
	view.AppendMenu(MF_STRING, IDC_FAVUSERS, CTSTRING(MENU_FAVORITE_USERS));
	view.AppendMenu(MF_SEPARATOR);
	view.AppendMenu(MF_STRING, ID_FILE_SEARCH, CTSTRING(MENU_SEARCH));
	view.AppendMenu(MF_STRING, IDC_FILE_ADL_SEARCH, CTSTRING(MENU_ADL_SEARCH));
	view.AppendMenu(MF_STRING, IDC_SEARCH_SPY, CTSTRING(MENU_SEARCH_SPY));
	view.AppendMenu(MF_SEPARATOR);
	view.AppendMenu(MF_STRING, IDC_DHT, CTSTRING(DHT_TITLE));
#ifdef IRAINMAN_INCLUDE_PROTO_DEBUG_FUNCTION
	view.AppendMenu(MF_STRING, IDC_CDMDEBUG_WINDOW, CTSTRING(MENU_CDMDEBUG_MESSAGES));
#endif
	view.AppendMenu(MF_STRING, IDC_NOTEPAD, CTSTRING(MENU_NOTEPAD));
	view.AppendMenu(MF_STRING, IDC_HASH_PROGRESS, CTSTRING(MENU_HASH_PROGRESS));
	view.AppendMenu(MF_SEPARATOR);
	view.AppendMenu(MF_STRING, IDC_TOPMOST, CTSTRING(MENU_TOPMOST));
	view.AppendMenu(MF_STRING, ID_VIEW_TOOLBAR, CTSTRING(MENU_TOOLBAR));
	view.AppendMenu(MF_STRING, ID_VIEW_STATUS_BAR, CTSTRING(MENU_STATUS_BAR));
	view.AppendMenu(MF_STRING, ID_VIEW_TRANSFER_VIEW, CTSTRING(MENU_TRANSFER_VIEW));
	view.AppendMenu(MF_STRING, ID_TOGGLE_TOOLBAR, CTSTRING(TOGGLE_TOOLBAR));
	view.AppendMenu(MF_STRING, ID_TOGGLE_QSEARCH, CTSTRING(TOGGLE_QSEARCH));
	
	g_mainMenu.AppendMenu(MF_POPUP, (UINT_PTR)(HMENU)view, CTSTRING(MENU_VIEW));
	
	CMenuHandle transfers;
	transfers.CreatePopupMenu();
	
	transfers.AppendMenu(MF_STRING, IDC_QUEUE, CTSTRING(MENU_DOWNLOAD_QUEUE));
	transfers.AppendMenu(MF_STRING, IDC_FINISHED, CTSTRING(MENU_FINISHED_DOWNLOADS));
	transfers.AppendMenu(MF_SEPARATOR);
	transfers.AppendMenu(MF_STRING, IDC_UPLOAD_QUEUE, CTSTRING(MENU_WAITING_USERS));
	transfers.AppendMenu(MF_STRING, IDC_FINISHED_UL, CTSTRING(MENU_FINISHED_UPLOADS));
	transfers.AppendMenu(MF_SEPARATOR);
	transfers.AppendMenu(MF_STRING, IDC_NET_STATS, CTSTRING(MENU_NETWORK_STATISTICS));
	
	g_mainMenu.AppendMenu(MF_POPUP, (UINT_PTR)(HMENU)transfers, CTSTRING(MENU_TRANSFERS));
	
	CMenuHandle window;
	window.CreatePopupMenu();
	
	window.AppendMenu(MF_STRING, ID_WINDOW_CASCADE, CTSTRING(MENU_CASCADE));
	window.AppendMenu(MF_STRING, ID_WINDOW_TILE_HORZ, CTSTRING(MENU_HORIZONTAL_TILE));
	window.AppendMenu(MF_STRING, ID_WINDOW_TILE_VERT, CTSTRING(MENU_VERTICAL_TILE));
	window.AppendMenu(MF_STRING, ID_WINDOW_ARRANGE, CTSTRING(MENU_ARRANGE));
	window.AppendMenu(MF_STRING, ID_WINDOW_MINIMIZE_ALL, CTSTRING(MENU_MINIMIZE_ALL));
	window.AppendMenu(MF_STRING, ID_WINDOW_RESTORE_ALL, CTSTRING(MENU_RESTORE_ALL));
	window.AppendMenu(MF_SEPARATOR);
	window.AppendMenu(MF_STRING, IDC_CLOSE_DISCONNECTED, CTSTRING(MENU_CLOSE_DISCONNECTED));
	window.AppendMenu(MF_STRING, IDC_CLOSE_ALL_HUBS, CTSTRING(MENU_CLOSE_ALL_HUBS));
	window.AppendMenu(MF_STRING, IDC_CLOSE_HUBS_BELOW, CTSTRING(MENU_CLOSE_HUBS_BELOW));
	window.AppendMenu(MF_STRING, IDC_CLOSE_HUBS_NO_USR, CTSTRING(MENU_CLOSE_HUBS_EMPTY));
	window.AppendMenu(MF_STRING, IDC_CLOSE_ALL_PM, CTSTRING(MENU_CLOSE_ALL_PM));
	window.AppendMenu(MF_STRING, IDC_CLOSE_ALL_OFFLINE_PM, CTSTRING(MENU_CLOSE_ALL_OFFLINE_PM));
	window.AppendMenu(MF_STRING, IDC_CLOSE_ALL_DIR_LIST, CTSTRING(MENU_CLOSE_ALL_DIR_LIST));
	window.AppendMenu(MF_STRING, IDC_CLOSE_ALL_SEARCH_FRAME, CTSTRING(MENU_CLOSE_ALL_SEARCHFRAME));
	
	g_mainMenu.AppendMenu(MF_POPUP, (UINT_PTR)(HMENU)window, CTSTRING(MENU_WINDOW));
	
	CMenuHandle help;
	help.CreatePopupMenu();
	
	help.AppendMenu(MF_STRING, ID_APP_ABOUT, CTSTRING(MENU_ABOUT));
	
	g_mainMenu.AppendMenu(MF_POPUP, (UINT_PTR)(HMENU)help, CTSTRING(MENU_HLP));
	
	g_fileImage.init();
	g_videoImage.init();
	g_flagImage.init();
	g_userImage.init();
	g_userStateImage.init();
	g_trackerImage.init();
	g_genderImage.init();
	g_favImage.init();
	g_favUserImage.init();
	
	Colors::init();
	Fonts::init();
	
	if (BOOLSETTING(REGISTER_URL_HANDLER))
		registerHubUrlHandlers();
	
	if (BOOLSETTING(REGISTER_MAGNET_HANDLER))
		registerMagnetHandler();

	if (BOOLSETTING(REGISTER_DCLST_HANDLER))
		registerDclstHandler();
	
	g_hook = SetWindowsHookEx(WH_KEYBOARD, &KeyboardProc, NULL, GetCurrentThreadId());
	
	g_copyHubMenu.CreatePopupMenu();
	g_copyHubMenu.AppendMenu(MF_STRING, IDC_COPY_HUBNAME, CTSTRING(HUB_NAME));
	g_copyHubMenu.AppendMenu(MF_STRING, IDC_COPY_HUBADDRESS, CTSTRING(HUB_ADDRESS));
	g_copyHubMenu.AppendMenu(MF_STRING, IDC_COPY_HUB_IP, CTSTRING(IP_ADDRESS));

	UserInfoGuiTraits::init();
}

void WinUtil::uninit()
{
	UnhookWindowsHookEx(g_hook);
	g_hook = nullptr;
	
	g_tabCtrl = nullptr;
	g_mainWnd = nullptr;
	g_fileImage.uninit();
	g_userImage.uninit();
	g_trackerImage.uninit();
	g_userStateImage.uninit();
	g_genderImage.uninit();
	g_TransferTreeImage.uninit();
	g_videoImage.uninit();

	Fonts::uninit();
	Colors::uninit();
	
	g_mainMenu.DestroyMenu();
	g_copyHubMenu.DestroyMenu();
	
	UserInfoGuiTraits::uninit();
}

#ifdef OSVER_WIN_XP

int CALLBACK WinUtil::browseCallbackProc(HWND hwnd, UINT uMsg, LPARAM /*lp*/, LPARAM pData)
{
	switch (uMsg)
	{
		case BFFM_INITIALIZED:
			SendMessage(hwnd, BFFM_SETSELECTION, TRUE, pData);
			break;
	}
	return 0;
}

bool WinUtil::browseDirectory(tstring& target, HWND owner, const GUID*)
{
	TCHAR buf[MAX_PATH];
	BROWSEINFO bi = {0};
	bi.hwndOwner = owner;
	bi.pszDisplayName = buf;
	bi.lpszTitle = CTSTRING(CHOOSE_FOLDER);
	bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_USENEWUI;
	bi.lParam = (LPARAM)target.c_str();
	bi.lpfn = &browseCallbackProc;
	LPITEMIDLIST pidl = SHBrowseForFolder(&bi);
	if (!pidl) return false;
	bool result = false;
	if (SHGetPathFromIDList(pidl, buf))
	{
		target = buf;
		Util::appendPathSeparator(target);
		result = true;
	}
	CoTaskMemFree(pidl);
	return result;
}

bool WinUtil::browseFile(tstring& target, HWND owner, bool save, const tstring& initialDir, const TCHAR* types, const TCHAR* defExt, const GUID* id)
{
	OPENFILENAME ofn = { 0 }; // common dialog box structure
	target = Text::toT(Util::validateFileName(Text::fromT(target)));
	unique_ptr<TCHAR[]> buf(new TCHAR[FULL_MAX_PATH]);
	size_t len = min<size_t>(target.length(), FULL_MAX_PATH-1);
	memcpy(buf.get(), target.data(), len*sizeof(TCHAR));
	buf[len] = 0;
	// Initialize OPENFILENAME
	ofn.lStructSize = OPENFILENAME_SIZE_VERSION_400;
	ofn.hwndOwner = owner;
	ofn.lpstrFile = buf.get();
	ofn.lpstrFilter = types;
	ofn.lpstrDefExt = defExt;
	ofn.nFilterIndex = 1;
	
	if (!initialDir.empty())
	{
		ofn.lpstrInitialDir = initialDir.c_str();
	}
	ofn.nMaxFile = FULL_MAX_PATH;
	ofn.Flags = (save ? 0 : OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST);
	
	// Display the Open dialog box.
	if ((save ? GetSaveFileName(&ofn) : GetOpenFileName(&ofn)) != FALSE)
	{
		target = ofn.lpstrFile;
		return true;
	}
	else
	{
		dcdebug("Error WinUtil::browseFile CommDlgExtendedError() = %x\n", CommDlgExtendedError());
	}
	return false;
}

#else

static bool addFileDialogOptions(IFileOpenDialog* fileOpen, FILEOPENDIALOGOPTIONS options)
{
	FILEOPENDIALOGOPTIONS fos;
	if (FAILED(fileOpen->GetOptions(&fos))) return false;
	fos |= options;
	return SUCCEEDED(fileOpen->SetOptions(fos));
}

bool WinUtil::browseDirectory(tstring& target, HWND owner, const GUID* id)
{
	bool result = false;
	IFileOpenDialog *pFileOpen = nullptr;

	HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL, 
		IID_IFileOpenDialog, reinterpret_cast<void**>(&pFileOpen));

	if (SUCCEEDED(hr))
	{
		if (id) pFileOpen->SetClientGuid(*id);
		if (addFileDialogOptions(pFileOpen, FOS_PICKFOLDERS | FOS_NOCHANGEDIR))
		{
			if (!target.empty())
			{
				IShellItem* shellItem;
				hr = SHCreateItemFromParsingName(target.c_str(), nullptr, IID_IShellItem, (void **) &shellItem);
				if (SUCCEEDED(hr) && shellItem)
				{
					pFileOpen->SetFolder(shellItem);
					shellItem->Release();
				}
			}
			pFileOpen->SetTitle(CWSTRING(CHOOSE_FOLDER));
			hr = pFileOpen->Show(owner);
			if (SUCCEEDED(hr))
			{
				IShellItem *pItem;
				hr = pFileOpen->GetResult(&pItem);
				if (SUCCEEDED(hr))
				{
					PWSTR pszFilePath;
					hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath);
					if (SUCCEEDED(hr))
					{
						target = pszFilePath;
						Util::appendPathSeparator(target);
						CoTaskMemFree(pszFilePath);
						result = true;
					}
					pItem->Release();
				}
			}
		}
	}
	if (pFileOpen) pFileOpen->Release();
	return result;
}

static void setFolder(IFileOpenDialog* fileOpen, const tstring& path)
{
	IShellItem* shellItem;
	HRESULT hr = SHCreateItemFromParsingName(path.c_str(), nullptr, IID_IShellItem, (void **) &shellItem);
	if (SUCCEEDED(hr) && shellItem)
	{
		fileOpen->SetFolder(shellItem);
		shellItem->Release();
	}
}

bool WinUtil::browseFile(tstring& target, HWND owner, bool save, const tstring& initialDir, const TCHAR* types, const TCHAR* defExt, const GUID* id)
{
	bool result = false;
	IFileOpenDialog *pFileOpen = nullptr;

	HRESULT hr = CoCreateInstance(
		save ? CLSID_FileSaveDialog : CLSID_FileOpenDialog, nullptr, CLSCTX_ALL, 
		save ? IID_IFileSaveDialog : IID_IFileOpenDialog, reinterpret_cast<void**>(&pFileOpen));

	if (SUCCEEDED(hr))
	{
		if (id) pFileOpen->SetClientGuid(*id);
		if (addFileDialogOptions(pFileOpen, FOS_FORCEFILESYSTEM | FOS_NOCHANGEDIR/* | FOS_DONTADDTORECENT*/))
		{
			if (types)
			{
				vector<COMDLG_FILTERSPEC> filters;
				while (true)
				{
					const WCHAR* name = types;
					if (!*name) break;
					const WCHAR* next = wcschr(name, 0);
					const WCHAR* spec = next + 1;
					filters.emplace_back(COMDLG_FILTERSPEC{name, spec});
					next = wcschr(spec, 0);
					types = next + 1;
				}
				pFileOpen->SetFileTypes(filters.size(), filters.data());
			}
			if (defExt) pFileOpen->SetDefaultExtension(defExt);
			if (!initialDir.empty())
				setFolder(pFileOpen, initialDir);
			if (!target.empty())
			{
				auto pos = target.rfind(PATH_SEPARATOR);
				if (pos != tstring::npos && initialDir.empty())
					setFolder(pFileOpen, target.substr(0, pos));
				if (pos != tstring::npos)
					target.erase(0, pos + 1);
				pFileOpen->SetFileName(target.c_str());
				if (!defExt)
				{
					tstring ext = Util::getFileExtWithoutDot(target);
					if (!ext.empty()) pFileOpen->SetDefaultExtension(ext.c_str());
				}
			}
			hr = pFileOpen->Show(owner);
			if (SUCCEEDED(hr))
			{
				IShellItem *pItem;
				hr = pFileOpen->GetResult(&pItem);
				if (SUCCEEDED(hr))
				{
					PWSTR pszFilePath;
					hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath);
					if (SUCCEEDED(hr))
					{
						target = pszFilePath;
						CoTaskMemFree(pszFilePath);
						result = true;
					}
					pItem->Release();
				}
			}
		}
	}
	if (pFileOpen) pFileOpen->Release();
	return result;
}

#endif

tstring WinUtil::encodeFont(const LOGFONT& font)
{
	tstring res(font.lfFaceName);
	res += _T(',');
	res += Util::toStringT(font.lfHeight);
	res += _T(',');
	res += Util::toStringT(font.lfWeight);
	res += _T(',');
	res += Util::toStringT(font.lfItalic);
	return res;
}

void WinUtil::setClipboard(const tstring& str)
{
	if (!::OpenClipboard(g_mainWnd))
	{
		return;
	}
	
	EmptyClipboard();
	
	// Allocate a global memory object for the text.
	HGLOBAL hglbCopy = GlobalAlloc(GMEM_MOVEABLE, (str.size() + 1) * sizeof(TCHAR));
	if (hglbCopy == NULL)
	{
		CloseClipboard();
		return;
	}
	
	// Lock the handle and copy the text to the buffer.
	TCHAR* lptstrCopy = (TCHAR*)GlobalLock(hglbCopy);
	_tcscpy(lptstrCopy, str.c_str());
	GlobalUnlock(hglbCopy);
	
	// Place the handle on the clipboard.
	SetClipboardData(CF_UNICODETEXT, hglbCopy);
	
	CloseClipboard();
}

int WinUtil::splitTokensWidth(int* result, const string& tokens, int maxItems, int defaultValue) noexcept
{
	int count = splitTokens(result, tokens, maxItems);
	for (int k = 0; k < count; ++k)
		if (result[k] <= 0 || result[k] > 2000)
			result[k] = defaultValue;
	return count;
}

int WinUtil::splitTokens(int* result, const string& tokens, int maxItems) noexcept
{
	SimpleStringTokenizer<char> t(tokens, ',');
	string tok;
	int k = 0;
	while (k < maxItems && t.getNextToken(tok))
		result[k++] = Util::toInt(tok);
	return k;
}

bool WinUtil::getUCParams(HWND parent, const UserCommand& uc, StringMap& sm)
{
	string::size_type i = 0;
	StringSet done;
	
	while ((i = uc.getCommand().find("%[line:", i)) != string::npos)
	{
		i += 7;
		string::size_type j = uc.getCommand().find(']', i);
		if (j == string::npos)
			break;
			
		string name = uc.getCommand().substr(i, j - i);
		if (done.find(name) == done.end())
		{
			LineDlg dlg;
			dlg.title = Text::toT(Util::toString(" > ", uc.getDisplayName()));
			dlg.description = Text::toT(name);
			dlg.line = Text::toT(sm["line:" + name]);

			if (uc.isSet(UserCommand::FLAG_FROM_ADC_HUB))
			{
				Util::replace(_T("\\\\"), _T("\\"), dlg.description);
				Util::replace(_T("\\s"), _T(" "), dlg.description);
			}

			if (dlg.DoModal(parent) != IDOK) return false;

			string str = Text::fromT(dlg.line);
			sm["line:" + name] = str;
			done.insert(name);
		}
		i = j + 1;
	}
	i = 0;
	while ((i = uc.getCommand().find("%[kickline:", i)) != string::npos)
	{
		i += 11;
		string::size_type j = uc.getCommand().find(']', i);
		if (j == string::npos)
			break;
			
		string name = uc.getCommand().substr(i, j - i);
		if (done.find(name) == done.end())
		{
			KickDlg dlg;
			dlg.title = Text::toT(Util::toString(" > ", uc.getDisplayName()));
			dlg.description = Text::toT(name);

			if (uc.isSet(UserCommand::FLAG_FROM_ADC_HUB))
			{
				Util::replace(_T("\\\\"), _T("\\"), dlg.description);
				Util::replace(_T("\\s"), _T(" "), dlg.description);
			}
			
			if (dlg.DoModal(parent) != IDOK) return false;

			string str = Text::fromT(dlg.line);
			sm["kickline:" + name] = str;
			done.insert(name);
		}
		i = j + 1;
	}
	return true;
}

void WinUtil::copyMagnet(const TTHValue& aHash, const string& aFile, int64_t aSize)
{
	if (!aFile.empty())
	{
		setClipboard(Text::toT(Util::getMagnet(aHash, aFile, aSize)));
	}
}

void WinUtil::searchFile(const string& file)
{
	SearchFrame::openWindow(Text::toT(file), 0, SIZE_DONTCARE, FILE_TYPE_ANY);
}

void WinUtil::searchHash(const TTHValue& hash)
{
	SearchFrame::openWindow(Text::toT(hash.toBase32()), 0, SIZE_DONTCARE, FILE_TYPE_TTH);
}

static bool regReadString(HKEY key, const TCHAR* name, tstring& value)
{
	TCHAR buf[512];
	DWORD size = sizeof(buf);
	DWORD type;
	if (RegQueryValueEx(key, name, nullptr, &type, (BYTE *) buf, &size) != ERROR_SUCCESS ||
	    (type != REG_SZ && type != REG_EXPAND_SZ))
	{
		value.clear();
		return false;
	}
	size /= sizeof(TCHAR);
	if (size && buf[size-1] == 0) size--;
	value.assign(buf, size);
	return true;
}

static bool regWriteString(HKEY key, const TCHAR* name, const TCHAR* value, DWORD len)
{
	return RegSetValueEx(key, name, 0, REG_SZ, (const BYTE *) value, (len + 1) * sizeof(TCHAR)) == ERROR_SUCCESS;
}

static inline bool regWriteString(HKEY key, const TCHAR* name, const tstring& str)
{
	return regWriteString(key, name, str.c_str(), str.length());
}

static bool registerUrlHandler(const TCHAR* proto, const TCHAR* description)
{
	HKEY key = nullptr;
	tstring value;
	tstring exePath = Util::getModuleFileName();
	tstring app = _T('\"') + exePath + _T("\" /magnet \"%1\"");
	tstring pathProto = _T("SOFTWARE\\Classes\\");
	pathProto += proto;
	tstring pathCommand = pathProto + _T("\\Shell\\Open\\Command");
	
	if (RegOpenKeyEx(HKEY_CURRENT_USER, pathCommand.c_str(), 0, KEY_READ, &key) == ERROR_SUCCESS)
	{
		regReadString(key, nullptr, value);
		RegCloseKey(key);
	}
	
	if (stricmp(app, value) == 0)
		return true;

	if (RegCreateKeyEx(HKEY_CURRENT_USER, pathProto.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &key, nullptr) != ERROR_SUCCESS)
		return false;
		
	bool result = false;
	do
	{
		if (!regWriteString(key, nullptr, description, _tcslen(description))) break;
		if (!regWriteString(key, _T("URL Protocol"), Util::emptyStringT)) break;
		RegCloseKey(key);
		key = nullptr;
		
		if (RegCreateKeyEx(HKEY_CURRENT_USER, pathCommand.c_str(), 0, nullptr,
		    REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &key, nullptr) != ERROR_SUCCESS) break;
		if (!regWriteString(key, nullptr, app)) break;
		RegCloseKey(key);
		key = nullptr;
		
		tstring pathIcon = pathProto + _T("\\DefaultIcon");
		if (RegCreateKeyEx(HKEY_CURRENT_USER, pathIcon.c_str(), 0, nullptr,
		    REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &key, nullptr) != ERROR_SUCCESS) break;
		if (!regWriteString(key, nullptr, exePath)) break;
		result = true;
	} while (0);

	if (key) RegCloseKey(key);
	return result;
}

void WinUtil::registerHubUrlHandlers()
{
	if (registerUrlHandler(_T("dchub"), _T("URL:Direct Connect Protocol")))
		hubUrlHandlersRegistered = true;
	else
		LogManager::message(STRING_F(ERROR_REGISTERING_PROTOCOL_HANDLER, "dchub"));

	if (registerUrlHandler(_T("nmdcs"), _T("URL:Direct Connect Protocol")))
		hubUrlHandlersRegistered = true;
	else
		LogManager::message(STRING_F(ERROR_REGISTERING_PROTOCOL_HANDLER, "nmdcs"));

	if (registerUrlHandler(_T("adc"), _T("URL:Advanced Direct Connect Protocol")))
		hubUrlHandlersRegistered = true;
	else
		LogManager::message(STRING_F(ERROR_REGISTERING_PROTOCOL_HANDLER, "adc"));

	if (registerUrlHandler(_T("adcs"), _T("URL:Advanced Direct Connect Protocol")))
		hubUrlHandlersRegistered = true;
	else
		LogManager::message(STRING_F(ERROR_REGISTERING_PROTOCOL_HANDLER, "adcs"));
}

static void internalDeleteRegistryKey(const tstring& key)
{
	tstring path = _T("SOFTWARE\\Classes\\") + key;
	if (SHDeleteKey(HKEY_CURRENT_USER, path.c_str()) != ERROR_SUCCESS)
		LogManager::message(STRING_F(ERROR_DELETING_REGISTRY_KEY, Text::fromT(path) % Util::translateError()));
}

void WinUtil::unregisterHubUrlHandlers()
{
	internalDeleteRegistryKey(_T("dchub"));
	internalDeleteRegistryKey(_T("nmdcs"));
	internalDeleteRegistryKey(_T("adc"));
	internalDeleteRegistryKey(_T("adcs"));
	hubUrlHandlersRegistered = false;
}

void WinUtil::registerMagnetHandler()
{
	if (registerUrlHandler(_T("magnet"), _T("URL:Magnet Link")))
		magnetHandlerRegistered = true;
	else
		LogManager::message(STRING_F(ERROR_REGISTERING_PROTOCOL_HANDLER, "magnet"));
}

void WinUtil::unregisterMagnetHandler()
{
	internalDeleteRegistryKey(_T("magnet"));
	magnetHandlerRegistered = false;
}

static bool registerFileHandler(const TCHAR* names[], const TCHAR* description)
{
	HKEY key = nullptr;
	tstring value;
	tstring exePath = Util::getModuleFileName();
	tstring app = _T('\"') + exePath + _T("\" /open \"%1\"");
	tstring pathExt = _T("SOFTWARE\\Classes\\");
	pathExt += names[0];
	tstring pathCommand = pathExt + _T("\\Shell\\Open\\Command");
	
	if (RegOpenKeyEx(HKEY_CURRENT_USER, pathCommand.c_str(), 0, KEY_READ, &key) == ERROR_SUCCESS)
	{
		regReadString(key, nullptr, value);
		RegCloseKey(key);
	}
	
	if (stricmp(app, value) == 0)
		return true;

	if (RegCreateKeyEx(HKEY_CURRENT_USER, pathExt.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &key, nullptr) != ERROR_SUCCESS)
		return false;
		
	bool result = false;
	do
	{
		if (!regWriteString(key, nullptr, description, _tcslen(description))) break;
		RegCloseKey(key);
		key = nullptr;
		
		if (RegCreateKeyEx(HKEY_CURRENT_USER, pathCommand.c_str(), 0, nullptr,
		    REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &key, nullptr) != ERROR_SUCCESS) break;
		if (!regWriteString(key, nullptr, app)) break;
		RegCloseKey(key);
		key = nullptr;
		
		tstring pathIcon = pathExt + _T("\\DefaultIcon");
		if (RegCreateKeyEx(HKEY_CURRENT_USER, pathIcon.c_str(), 0, nullptr,
		    REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &key, nullptr) != ERROR_SUCCESS) break;
		if (!regWriteString(key, nullptr, exePath)) break;
		RegCloseKey(key);
		key = nullptr;

		int i = 1;
		DWORD len = _tcslen(names[0]);
		while (true)
		{
			if (!names[i])
			{
				result = true;
				break;
			}
			tstring path = _T("SOFTWARE\\Classes\\");
			path += names[i];
			if (RegCreateKeyEx(HKEY_CURRENT_USER, path.c_str(), 0, nullptr,
			    REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &key, nullptr) != ERROR_SUCCESS) break;
			if (!regWriteString(key, nullptr, names[0], len)) break;
			RegCloseKey(key);
			key = nullptr;
			i++;
		}
	} while (0);

	if (key) RegCloseKey(key);
	return result;
}

static const TCHAR* dcLstNames[] = { _T("DcLst download list"), _T(".dclst"), _T(".dcls"), nullptr };

void WinUtil::registerDclstHandler()
{
	if (registerFileHandler(dcLstNames, CTSTRING(DCLST_DESCRIPTION)))
		dclstHandlerRegistered = true;
	else
		LogManager::message(STRING(ERROR_CREATING_REGISTRY_KEY_DCLST));
}

void WinUtil::unregisterDclstHandler()
{
	int i = 0;
	while (dcLstNames[i])
	{
		internalDeleteRegistryKey(dcLstNames[i]);
		i++;
	}
	dclstHandlerRegistered = false;
}

void WinUtil::playSound(const string& soundFile, bool beep /* = false */)
{
	if (!soundFile.empty())
		PlaySound(Text::toT(soundFile).c_str(), NULL, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
	else if (beep)
		MessageBeep(MB_OK);
}

void WinUtil::openFile(const tstring& file)
{
	openFile(file.c_str());
}

void WinUtil::openFile(const TCHAR* file)
{
	::ShellExecute(NULL, _T("open"), file, NULL, NULL, SW_SHOWNORMAL);
}

static void shellExecute(const tstring& url)
{
#ifdef FLYLINKDC_USE_TORRENT
	if (url.find(_T("magnet:?xt=urn:btih")) != tstring::npos)
	{
		DownloadManager::getInstance()->add_torrent_file(_T(""), url);
		return;
	}
#endif
	::ShellExecute(NULL, NULL, url.c_str(), NULL, NULL, SW_SHOWNORMAL);
}

bool WinUtil::openLink(const tstring& uri)
{
	if (parseMagnetUri(uri) || parseDchubUrl(uri))
		return true;

	static const tstring extLinks[] =
	{
		_T("http://"),
		_T("https://"),
		_T("ftp://"),
		_T("irc://"),
		_T("skype:"),
		_T("ed2k://"),
		_T("mms://"),
		_T("xmpp://"),
		_T("nfs://"),
		_T("mailto:"),
		_T("www.")
	};
	for (const tstring& link : extLinks)
	{
		if (strnicmp(uri, link, link.length()) == 0)
		{
			shellExecute(uri);
			return true;
		}
	}
	return false;
}

bool WinUtil::parseDchubUrl(const tstring& url)
{
	uint16_t port;
	string proto, host, file, query, fragment;
	Util::decodeUrl(Text::fromT(url), proto, host, port, file, query, fragment);
	if (!Util::getHubProtocol(proto) || host.empty() || port == 0) return false;

	const string formattedUrl = Util::formatDchubUrl(proto, host, port);
	
	RecentHubEntry r;
	r.setOpenTab("+");
	r.setServer(formattedUrl);
	FavoriteManager::getInstance()->addRecent(r);
	HubFrame::openHubWindow(formattedUrl);

	if (!file.empty())
	{
		if (file[0] == '/') // Remove any '/' in from of the file
			file.erase(0, 1);
				
		string nick;
		const string::size_type i = file.find('/');
		if (i != string::npos)
		{
			nick = file.substr(0, i);
			file.erase(0, i);
		}
		else
		{
			nick = std::move(file);
			file = "/";
		}
		if (!nick.empty())
		{
			const UserPtr user = ClientManager::findLegacyUser(nick, formattedUrl);
			if (user)
			{
				try
				{
					QueueManager::getInstance()->addList(user, QueueItem::FLAG_CLIENT_VIEW, file);
				}
				catch (const Exception&)
				{
					// Ignore for now...
				}
			}
		}
	}
	return true;
}

static WinUtil::DefinedMagnetAction getMagnetAction(int setting)
{
	switch (setting)
	{
		case SettingsManager::MAGNET_ACTION_DOWNLOAD:
			return WinUtil::MA_DOWNLOAD;
		case SettingsManager::MAGNET_ACTION_SEARCH:
			return WinUtil::MA_SEARCH;
		case SettingsManager::MAGNET_ACTION_DOWNLOAD_AND_OPEN:
			return WinUtil::MA_OPEN;
	}
	return WinUtil::MA_ASK;
}

bool WinUtil::parseMagnetUri(const tstring& aUrl, DefinedMagnetAction action /* = MA_DEFAULT */)
{
	if (Util::isMagnetLink(aUrl))
	{
#ifdef FLYLINKDC_USE_TORRENT
		if (Util::isTorrentLink(aUrl))
		{
			DownloadManager::getInstance()->add_torrent_file(_T(""), aUrl);
		}
		else
#endif
		{
			const string url = Text::fromT(aUrl);
			LogManager::message(STRING(MAGNET_DLG_TITLE) + ": " + url);
			MagnetLink magnet;
			const char* fhash;
			if (!magnet.parse(url) || (fhash = magnet.getTTH()) == nullptr)
			{
				MessageBox(g_mainWnd, CTSTRING(MAGNET_DLG_TEXT_BAD), CTSTRING(MAGNET_DLG_TITLE), MB_OK | MB_ICONEXCLAMATION);
				return false;
			}
			string fname = magnet.getFileName();
			if (!magnet.exactSource.empty())
				WinUtil::parseDchubUrl(Text::toT(magnet.exactSource));

			const bool isDclst = Util::isDclstFile(fname);
			if (action == MA_DEFAULT)
			{
				if (!isDclst)
				{
					if (BOOLSETTING(MAGNET_ASK))
						action = MA_ASK;
					else
						action = getMagnetAction(SETTING(MAGNET_ACTION));
				}
				else
				{
					if (BOOLSETTING(DCLST_ASK))
						action = MA_ASK;
					else
						action = getMagnetAction(SETTING(DCLST_ACTION));
				}
			}
			if (action == MA_ASK)
			{
				tstring filename = Text::toT(fname);
				action = MagnetDlg::showDialog(g_mainWnd, TTHValue(fhash), filename, magnet.exactLength, magnet.dirSize, isDclst);
				if (action == WinUtil::MA_DEFAULT) return true;
				fname = Text::fromT(filename);
			}
			switch (action)
			{
				case MA_DOWNLOAD:
				case MA_OPEN:
					try
					{
						bool getConnFlag = true;
						QueueItem::MaskType flags = isDclst ? QueueItem::FLAG_DCLST_LIST : 0;
						if (action == MA_OPEN)
							flags |= QueueItem::FLAG_CLIENT_VIEW;
						else if (isDclst)
							flags |= QueueItem::FLAG_DOWNLOAD_CONTENTS;
						QueueManager::getInstance()->add(fname, magnet.exactLength, TTHValue(fhash), HintedUser(),
							flags, QueueItem::DEFAULT, true, getConnFlag);
					}
					catch (const Exception& e)
					{
						LogManager::message("QueueManager::getInstance()->add Error = " + e.getError());
					}
					break;

				case MA_SEARCH:
					SearchFrame::openWindow(Text::toT(fhash), 0, SIZE_DONTCARE, FILE_TYPE_TTH);
					break;
			}
		}
		return true;
	}
	return false;
}

void WinUtil::openFileList(const tstring& filename, DefinedMagnetAction Action /* = MA_DEFAULT */)
{
	const UserPtr u = DirectoryListing::getUserFromFilename(Text::fromT(filename));
	DirectoryListingFrame::openWindow(filename, Util::emptyStringT, HintedUser(u, Util::emptyString), 0, Util::isDclstFile(Text::fromT(filename)));
}

int WinUtil::textUnderCursor(POINT p, CEdit& ctrl, tstring& x)
{
	const int i = ctrl.CharFromPos(p);
	const int line = ctrl.LineFromChar(i);
	const int c = LOWORD(i) - ctrl.LineIndex(line);
	const int len = ctrl.LineLength(i) + 1;
	if (len < 3)
	{
		return 0;
	}
	
	x.resize(len + 1);
	x.resize(ctrl.GetLine(line, &x[0], len + 1));
	
	string::size_type start = x.find_last_of(_T(" <\t\r\n"), c);
	if (start == string::npos)
		start = 0;
	else
		start++;
		
	return start;
}

// !SMT!-UI (todo: disable - this routine does not save column visibility)
void WinUtil::saveHeaderOrder(CListViewCtrl& ctrl, SettingsManager::StrSetting order,
                              SettingsManager::StrSetting widths, int n,
                              int* indexes, int* sizes) noexcept
{
	string tmp;
	
	ctrl.GetColumnOrderArray(n, indexes);
	int i;
	for (i = 0; i < n; ++i)
	{
		tmp += Util::toString(indexes[i]);
		tmp += ',';
	}
	tmp.erase(tmp.size() - 1, 1);
	SettingsManager::set(order, tmp);
	tmp.clear();
	const int nHeaderItemsCount = ctrl.GetHeader().GetItemCount();
	for (i = 0; i < n; ++i)
	{
		sizes[i] = ctrl.GetColumnWidth(i);
		if (i >= nHeaderItemsCount) // Not exist column
			sizes[i] = 0;
		tmp += Util::toString(sizes[i]);
		tmp += ',';
	}
	tmp.erase(tmp.size() - 1, 1);
	SettingsManager::set(widths, tmp);
}

static pair<tstring, bool> formatHubNames(const StringList& hubs)
{
	if (hubs.empty())
	{
		return pair<tstring, bool>(TSTRING(OFFLINE), false);
	}
	else
	{
		return pair<tstring, bool>(Text::toT(Util::toString(hubs)), true);
	}
}

pair<tstring, bool> WinUtil::getHubNames(const CID& cid, const string& hintUrl)
{
	return formatHubNames(ClientManager::getHubNames(cid, hintUrl));
}

pair<tstring, bool> WinUtil::getHubNames(const UserPtr& u, const string& hintUrl)
{
	return getHubNames(u->getCID(), hintUrl);
}

pair<tstring, bool> WinUtil::getHubNames(const CID& cid, const string& hintUrl, bool priv)
{
	return formatHubNames(ClientManager::getHubNames(cid, hintUrl, priv));
}

pair<tstring, bool> WinUtil::getHubNames(const HintedUser& user)
{
	return getHubNames(user.user, user.hint);
}

void WinUtil::getContextMenuPos(const CListViewCtrl& list, POINT& pt)
{
	int pos = list.GetNextItem(-1, LVNI_SELECTED | LVNI_FOCUSED);
	if (pos >= 0)
	{
		CRect rc;
		list.GetItemRect(pos, &rc, LVIR_LABEL);
		pt.x = rc.left;
		pt.y = rc.top + rc.Height() / 2;
	}
	else
		pt.x = pt.y = 0;
	list.ClientToScreen(&pt);
}

void WinUtil::getContextMenuPos(const CTreeViewCtrl& tree, POINT& pt)
{
	CRect rc;
	HTREEITEM ht = tree.GetSelectedItem();
	if (ht)
	{
		tree.GetItemRect(ht, &rc, TRUE);
		pt.x = rc.left;
		pt.y = rc.top + rc.Height() / 2;
	}
	else
		pt.x = pt.y = 0;
	tree.ClientToScreen(&pt);
}

void WinUtil::getContextMenuPos(const CEdit& edit, POINT& pt)
{
	CRect rc;
	edit.GetRect(&rc);
	pt.x = rc.Width() / 2;
	pt.y = rc.Height() / 2;
	edit.ClientToScreen(&pt);
}

void WinUtil::openFolder(const tstring& file)
{
	if (File::isExist(file))
		::ShellExecute(NULL, NULL, _T("explorer.exe"), (_T("/e, /select, \"") + file + _T('\"')).c_str(), NULL, SW_SHOWNORMAL);
	else
		::ShellExecute(NULL, NULL, _T("explorer.exe"), (_T("/e, \"") + Util::getFilePath(file) + _T('\"')).c_str(), NULL, SW_SHOWNORMAL);
}

void WinUtil::openLog(const string& dir, const StringMap& params, const tstring& noLogMessage)
{
	const auto file = Text::toT(Util::validateFileName(SETTING(LOG_DIRECTORY) + Util::formatParams(dir, params, true)));
	if (File::isExist(file))
		WinUtil::openFile(file);
	else
		MessageBox(nullptr, noLogMessage.c_str(), getAppNameVerT().c_str(), MB_OK | MB_ICONINFORMATION);
}

bool WinUtil::shutDown(int action)
{
	// Prepare for shutdown
	static const UINT forceIfHung = 0x00000010;
	HANDLE hToken;
	OpenProcessToken(GetCurrentProcess(), (TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY), &hToken);

	LUID luid;
	LookupPrivilegeValue(NULL, SE_SHUTDOWN_NAME, &luid);

	TOKEN_PRIVILEGES tp;
	tp.PrivilegeCount = 1;
	tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
	tp.Privileges[0].Luid = luid;
	AdjustTokenPrivileges(hToken, FALSE, &tp, 0, (PTOKEN_PRIVILEGES)NULL, (PDWORD)NULL);
	CloseHandle(hToken);

	// Shutdown
	switch (action)
	{
		case 0:
		{
			action = EWX_POWEROFF;
			break;
		}
		case 1:
		{
			action = EWX_LOGOFF;
			break;
		}
		case 2:
		{
			action = EWX_REBOOT;
			break;
		}
		case 3:
		{
			SetSuspendState(false, false, false);
			return true;
		}
		case 4:
		{
			SetSuspendState(true, false, false);
			return true;
		}
		case 5:
		{
			typedef bool (CALLBACK * LPLockWorkStation)(void);
			LPLockWorkStation _d_LockWorkStation = (LPLockWorkStation)GetProcAddress(LoadLibrary(_T("user32")), "LockWorkStation");
			if (_d_LockWorkStation)
			{
				_d_LockWorkStation();
			}
			return true;
		}
	}
	
	return ExitWindowsEx(action | forceIfHung, 0) != 0;
}

void WinUtil::activateMDIChild(HWND hWnd)
{
	::SendMessage(g_mdiClient, WM_SETREDRAW, FALSE, 0);
	::SendMessage(g_mdiClient, WM_MDIACTIVATE, (WPARAM) hWnd, 0);
	::SendMessage(g_mdiClient, WM_SETREDRAW, TRUE, 0);
	::RedrawWindow(g_mdiClient, nullptr, nullptr, RDW_ERASE | RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
}

tstring WinUtil::getNicks(const CID& cid, const string& hintUrl)
{
	const auto nicks = ClientManager::getNicks(cid, hintUrl);
	if (nicks.empty())
		return Util::emptyStringT;
	else
		return Text::toT(Util::toString(nicks));
}

tstring WinUtil::getNicks(const UserPtr& u, const string& hintUrl)
{
	dcassert(u);
	if (u)
		return getNicks(u->getCID(), hintUrl);
	return Util::emptyStringT;
}

tstring WinUtil::getNicks(const CID& cid, const string& hintUrl, bool priv)
{
	const auto nicks = ClientManager::getNicks(cid, hintUrl, priv);
	if (nicks.empty())
		return Util::emptyStringT;
	else
		return Text::toT(Util::toString(nicks));
}

tstring WinUtil::getNicks(const HintedUser& user)
{
	return getNicks(user.user, user.hint);
}

void WinUtil::fillAdapterList(int af, CComboBox& bindCombo, const string& bindAddress)
{
	vector<Util::AdapterInfo> adapters;
	Util::getNetworkAdapters(af, adapters);
	IpAddressEx defaultAdapter;
	memset(&defaultAdapter, 0, sizeof(defaultAdapter));
	defaultAdapter.type = af;
	if (std::find_if(adapters.cbegin(), adapters.cend(),
		[&defaultAdapter](const auto &v) { return v.ip == defaultAdapter; }) == adapters.cend())
	{
		adapters.insert(adapters.begin(), Util::AdapterInfo(TSTRING(DEFAULT_ADAPTER), defaultAdapter, 0, 0));
	}
	int selected = -1;
	for (size_t i = 0; i < adapters.size(); ++i)
	{
		string address = Util::printIpAddress(adapters[i].ip);
		tstring text = Text::toT(address);
		if (!adapters[i].adapterName.empty())
		{
			text += _T(" (");
			text += adapters[i].adapterName;
			text += _T(')');
		}
		bindCombo.AddString(text.c_str());
		if (address == bindAddress)
			selected = i;
	}
	if (bindAddress.empty()) selected = 0;
	if (selected == -1)
		selected = bindCombo.InsertString(-1, Text::toT(bindAddress).c_str());
	bindCombo.SetCurSel(selected);
}

string WinUtil::getSelectedAdapter(const CComboBox& bindCombo)
{
	tstring ts;
	WinUtil::getWindowText(bindCombo, ts);
	string str = Text::fromT(ts);
	boost::trim(str);
	string::size_type pos = str.find(' ');
	if (pos != string::npos) str.erase(pos);
	return str;
}

void WinUtil::fillCharsetList(CComboBox& comboBox, int selected, bool onlyUTF8, bool inFavs)
{
	static const ResourceManager::Strings charsetNames[Text::NUM_SUPPORTED_CHARSETS] =
	{
		ResourceManager::ENCODING_CP1250,
		ResourceManager::ENCODING_CP1251,
		ResourceManager::ENCODING_CP1252,
		ResourceManager::ENCODING_CP1253,
		ResourceManager::ENCODING_CP1254,
		ResourceManager::ENCODING_CP1255,
		ResourceManager::ENCODING_CP1256,
		ResourceManager::ENCODING_CP1257,
		ResourceManager::ENCODING_CP1258,
		ResourceManager::ENCODING_CP936,
		ResourceManager::ENCODING_CP950
	};

	int index, selIndex = -1;
	if (!onlyUTF8)
	{
		tstring str;
		if (inFavs)
		{
			int defaultCharset = Text::charsetFromString(SETTING(DEFAULT_CODEPAGE));
			if (defaultCharset == Text::CHARSET_SYSTEM_DEFAULT) defaultCharset = Text::getDefaultCharset();
			str = TSTRING_F(ENCODING_DEFAULT, defaultCharset);
		}
		else
			str = TSTRING_F(ENCODING_SYSTEM_DEFAULT, Text::getDefaultCharset());
		index = comboBox.AddString(str.c_str());
		if (selected == Text::CHARSET_SYSTEM_DEFAULT) selIndex = index;
		comboBox.SetItemData(index, Text::CHARSET_SYSTEM_DEFAULT);
	}
	index = comboBox.AddString(CTSTRING(ENCODING_UTF8));
	if (selected == Text::CHARSET_UTF8) selIndex = index;
	comboBox.SetItemData(index, Text::CHARSET_UTF8);	
	if (!onlyUTF8)
		for (int i = 0; i < Text::NUM_SUPPORTED_CHARSETS; i++)
		{
			int charset = Text::supportedCharsets[i];
			tstring str = TSTRING_I(charsetNames[i]);
			str += _T(" (");
			str += Util::toStringT(charset);
			str += _T(')');
			index = comboBox.AddString(str.c_str());
			if (selected == charset) selIndex = index;
			comboBox.SetItemData(index, charset);
		}

	if (selIndex < 0 || onlyUTF8) selIndex = 0;
	comboBox.SetCurSel(selIndex);
}

int WinUtil::getSelectedCharset(const CComboBox& comboBox)
{
	int selIndex = comboBox.GetCurSel();
	return comboBox.GetItemData(selIndex);
}

void WinUtil::fillTimeValues(CComboBox& comboBox)
{
	tm t;
	memset(&t, 0, sizeof(t));
	TCHAR buf[64];
	tstring ts;
	comboBox.AddString(CTSTRING(MIDNIGHT));
	for (int i = 1; i < 24; ++i)
	{
		if (i == 12)
		{
			comboBox.AddString(CTSTRING(NOON));
			continue;
		}
		t.tm_hour = i;
		_tcsftime(buf, _countof(buf), _T("%X"), &t);
		ts.assign(buf);
		auto pos = ts.find(_T(":00:00"));
		if (pos != tstring::npos) ts.erase(pos + 3, 3);
		comboBox.AddString(ts.c_str());
	}
}

#ifdef SSA_SHELL_INTEGRATION
tstring WinUtil::getShellExtDllPath()
{
	static const auto filePath = Text::toT(Util::getExePath()) + _T("BLShellExt")
#if defined(_WIN64)
	                             _T("_x64")
#endif
	                             _T(".dll");
	                             
	return filePath;
}

bool WinUtil::registerShellExt(bool unregister)
{
	typedef HRESULT (WINAPIV *Registration)();
	
	bool result = false;
	HINSTANCE hModule = nullptr;
	try
	{
		const auto filePath = WinUtil::getShellExtDllPath();
		hModule = LoadLibrary(filePath.c_str());
		if (hModule != nullptr)
		{
			Registration reg = nullptr;
			reg = (Registration) GetProcAddress((HMODULE)hModule, unregister ? "DllUnregisterServer" : "DllRegisterServer");
			if (reg)
				result = SUCCEEDED(reg());
			FreeLibrary(hModule);
		}
	}
	catch (...)
	{
		if (hModule)
			FreeLibrary(hModule);
	}
	return result;
}
#endif // SSA_SHELL_INTEGRATION

bool WinUtil::runElevated(HWND hwnd, const TCHAR* path, const TCHAR* parameters, const TCHAR* directory, int waitTime)
{
	SHELLEXECUTEINFO shex = { sizeof(SHELLEXECUTEINFO) };	
	shex.fMask        = waitTime ? SEE_MASK_NOCLOSEPROCESS : 0;
	shex.hwnd         = hwnd;
	shex.lpVerb       = _T("runas");
	shex.lpFile       = path;
	shex.lpParameters = parameters;
	shex.lpDirectory  = directory;
	shex.nShow        = SW_NORMAL;
	
	if (!ShellExecuteEx(&shex)) return false;
	if (!waitTime) return true;
	bool result = WaitForSingleObject(shex.hProcess, waitTime) == WAIT_OBJECT_0;
	CloseHandle(shex.hProcess);
	return result;
}

bool WinUtil::createShortcut(const tstring& targetFile, const tstring& targetArgs,
                             const tstring& linkFile, const tstring& description,
                             int showMode, const tstring& workDir,
                             const tstring& iconFile, int iconIndex)
{
	if (targetFile.empty() || linkFile.empty()) return false;

	bool result = false;
	IShellLink*   pShellLink = nullptr;
	IPersistFile* pPersistFile = nullptr;

	do
	{
		if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLink, (void**) &pShellLink))
			|| !pShellLink) break;

		if (FAILED(pShellLink->SetPath(targetFile.c_str()))) break;
		if (FAILED(pShellLink->SetArguments(targetArgs.c_str()))) break;
		if (!description.empty() && FAILED(pShellLink->SetDescription(description.c_str()))) break;
		if (showMode > 0 && FAILED(pShellLink->SetShowCmd(showMode))) break;
		if (!workDir.empty() && FAILED(pShellLink->SetWorkingDirectory(workDir.c_str()))) break;
		if (!iconFile.empty() && iconIndex >= 0 && FAILED(pShellLink->SetIconLocation(iconFile.c_str(), iconIndex))) break;

		if (FAILED(pShellLink->QueryInterface(IID_IPersistFile, (void**) &pPersistFile))
			|| !pPersistFile) break;
		if (FAILED(pPersistFile->Save(linkFile.c_str(), TRUE))) break;

		result = true;	
	} while (0);
	
	if (pPersistFile)
		pPersistFile->Release();
	if (pShellLink)
		pShellLink->Release();
	
	return result;
}

bool WinUtil::autoRunShortcut(bool create)
{
	tstring linkFile = getAutoRunShortcutName();
	if (create)
	{
		if (!File::isExist(linkFile))
		{
			tstring targetFile = Util::getModuleFileName();
			tstring workDir = Util::getFilePath(targetFile);
			tstring description = getAppNameVerT();
			Util::appendPathSeparator(workDir);
			return createShortcut(targetFile, _T(""), linkFile, description, 0, workDir, targetFile, 0);
		}
	}
	else
	{
		if (File::isExist(linkFile))
			return File::deleteFile(linkFile);
	}	
	return true;
}

bool WinUtil::isAutoRunShortcutExists()
{
	return File::isExist(getAutoRunShortcutName());
}

tstring WinUtil::getAutoRunShortcutName()
{
	TCHAR startupPath[MAX_PATH];
	if (!SHGetSpecialFolderPath(NULL, startupPath, CSIDL_STARTUP, TRUE))
		return Util::emptyStringT;

	tstring result = startupPath;
	Util::appendPathSeparator(result);
	result += getAppNameT();
#if defined(_WIN64)
	result += _T("_x64");
#endif
	result += _T(".lnk");
	
	return result;
}

void WinUtil::getWindowText(HWND hwnd, tstring& text)
{
	int len = GetWindowTextLength(hwnd);
	if (len <= 0)
	{
		text.clear();
		return;
	}
	text.resize(len + 1);
	len = GetWindowText(hwnd, &text[0], len + 1);
	text.resize(len);
}

bool WinUtil::setExplorerTheme(HWND hWnd)
{
	if (!IsAppThemed()) return false;
	SetWindowTheme(hWnd, L"explorer", nullptr);
	return true;
}

unsigned WinUtil::getListViewExStyle(bool checkboxes)
{
	unsigned style = LVS_EX_LABELTIP | LVS_EX_HEADERDRAGDROP | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_INFOTIP;
	if (checkboxes) style |= LVS_EX_CHECKBOXES;
	if (BOOLSETTING(SHOW_GRIDLINES)) style |= LVS_EX_GRIDLINES;
	return style;
}

unsigned WinUtil::getTreeViewStyle()
{
	return TVS_SHOWSELALWAYS | TVS_DISABLEDRAGDROP | TVS_HASBUTTONS | TVS_LINESATROOT;
}

#ifdef IRAINMAN_ENABLE_WHOIS
bool WinUtil::processWhoisMenu(WORD wID, const tstring& ip)
{
	if (!ip.empty())
	{
		tstring link;
		switch (wID)
		{
			case IDC_WHOIS_IP:
				link = _T("http://www.ripe.net/perl/whois?form_type=simple&full_query_string=&searchtext=") + ip;
				break;
			case IDC_WHOIS_IP2:
				link = _T("http://bgp.he.net/ip/") + ip+ _T("#_whois");
				break;
		}
		if (!link.empty())
		{
			WinUtil::openLink(link);
			return true;
		}
	}
	return false;
}

void WinUtil::appendWhoisMenu(OMenu& menu, const tstring& ip, bool useSubMenu)
{
	CMenu subMenu;
	if (useSubMenu)
	{
		subMenu.CreateMenu();
		menu.AppendMenu(MF_STRING, subMenu, CTSTRING(WHOIS_LOOKUP));
	}

	tstring text = TSTRING(WHO_IS) + _T(" Ripe.net  ") + ip;
	if (useSubMenu)
		subMenu.AppendMenu(MF_STRING, IDC_WHOIS_IP, text.c_str());
	else
		menu.AppendMenu(MF_STRING, IDC_WHOIS_IP, text.c_str());

	text = TSTRING(WHO_IS) + _T(" Bgp.He  ") + ip;
	if (useSubMenu)
		subMenu.AppendMenu(MF_STRING, IDC_WHOIS_IP2, text.c_str());
	else
		menu.AppendMenu(MF_STRING, IDC_WHOIS_IP2, text.c_str());
	subMenu.Detach();
}
#endif

void WinUtil::appendPrioItems(OMenu& menu, int idFirst)
{
	static const ResourceManager::Strings names[] =
	{
		ResourceManager::PAUSED,
		ResourceManager::LOWEST,
		ResourceManager::LOWER,
		ResourceManager::LOW,
		ResourceManager::NORMAL,
		ResourceManager::HIGH,
		ResourceManager::HIGHER,
		ResourceManager::HIGHEST
	};
	static_assert(_countof(names) == QueueItem::LAST, "priority list mismatch");
	MENUITEMINFO mii = { sizeof(mii) };
	mii.fMask = MIIM_STRING | MIIM_FTYPE | MIIM_ID;
	mii.fType = MFT_RADIOCHECK;
	int index = menu.GetMenuItemCount();
	for (int i = 0; i < _countof(names); i++)
	{
		mii.wID = idFirst + i;
		mii.dwTypeData = const_cast<TCHAR*>(CTSTRING_I(names[i]));
		menu.InsertMenuItem(index + i, TRUE, &mii);
	}
}

bool WinUtil::getDialogUnits(HWND hwnd, HFONT font, int& cx, int& cy)
{
	if (!font)
	{
		font = (HFONT) ::SendMessage(hwnd, WM_GETFONT, 0, 0);
		if (!font) font = Fonts::g_systemFont;
	}
	HDC hdc = GetDC(hwnd);
	if (!hdc) return false;
	bool res = false;
	HGDIOBJ prevFont = SelectObject(hdc, font);
	TEXTMETRIC tm;
	if (GetTextMetrics(hdc, &tm))
	{
		TCHAR ch[52];
		for (int i = 0; i < 52; i++)
			ch[i] = i < 26 ? 'a' + i : 'A' + i - 26;
		SIZE size;
		if (GetTextExtentPoint(hdc, ch, 52, &size))
		{
			cx = (size.cx / 26 + 1) / 2;
			cy = tm.tmHeight;
			res = true;
		}
	}
	SelectObject(hdc, prevFont);
	ReleaseDC(hwnd, hdc);
	return res;
}

int WinUtil::getComboBoxHeight(HWND hwnd, HFONT font)
{
	if (!font)
	{
		font = (HFONT) ::SendMessage(hwnd, WM_GETFONT, 0, 0);
		if (!font) font = Fonts::g_systemFont;
	}
	HDC hdc = GetDC(hwnd);
	if (!hdc) return 0;
	int res = 0;
	HGDIOBJ prevFont = SelectObject(hdc, font);
	SIZE size;
	TCHAR ch = _T('0');
	if (GetTextExtentPoint(hdc, &ch, 1, &size))
		res = size.cy + GetSystemMetrics(SM_CYEDGE) + 2*GetSystemMetrics(SM_CYFIXEDFRAME);
	SelectObject(hdc, prevFont);
	ReleaseDC(hwnd, hdc);
	return res;	
}

tstring WinUtil::getFileMaskString(const FileMaskItem* items)
{
	tstring result;
	while (items->ext)
	{
		result += TSTRING_I(items->stringId);
		result += _T('\0');
		result += items->ext;
		result += _T('\0');
		items++;
	}
	if (result.empty()) result += _T('\0');
	return result;
}
