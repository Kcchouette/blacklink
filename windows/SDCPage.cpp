/*
 * Copyright (C) 2001-2017 Jacek Sieka, j_s@telia.com
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
#include "SDCPage.h"
#include "../client/SettingsManager.h"
#include "WinUtil.h"

#ifdef OSVER_WIN_XP
#include "../client/CompatibilityManager.h"
#endif

static const WinUtil::TextItem texts[] =
{
	{ IDC_SETTINGS_B, ResourceManager::B },
	{ IDC_B1, ResourceManager::B },
	{ IDC_B2, ResourceManager::B },
	{ IDC_S2, ResourceManager::S },
	{ IDC_SETTINGS_WRITE_BUFFER, ResourceManager::SETTINGS_WRITE_BUFFER },
#ifdef OSVER_WIN_XP
	{ IDC_SETTINGS_SOCKET_IN_BUFFER, ResourceManager::SETTINGS_SOCKET_IN_BUFFER },
	{ IDC_SETTINGS_SOCKET_OUT_BUFFER, ResourceManager::SETTINGS_SOCKET_OUT_BUFFER },
#endif
	{ IDC_SETTINGS_KB, ResourceManager::KB },
	{ IDC_CAPTION_SHUTDOWN_TIMEOUT, ResourceManager::TIMEOUT },
	{ IDC_MAX_COMPRESSION, ResourceManager::SETTINGS_MAX_COMPRESS },
	{ IDC_CAPTION_DISABLE_COMP, ResourceManager::SETTINGS_DISABLE_COMP },
	{ IDC_SHUTDOWN_ACTION, ResourceManager::SHUTDOWN_ACTION },
	{ IDC_CAPTION_DOWNCONN, ResourceManager::SETTINGS_DOWNCONN },
	{ IDC_MIN_MULTI_CHUNK_SIZE, ResourceManager::SETTINGS_MIN_MULTI_CHUNK_SIZE },
	{ IDC_SETTINGS_MB, ResourceManager::MB },
	{ 0, ResourceManager::Strings() }
};

static const PropPage::Item items[] =
{
	{ IDC_BUFFERSIZE, SettingsManager::BUFFER_SIZE_FOR_DOWNLOADS, PropPage::T_INT },
#ifdef OSVER_WIN_XP
	{ IDC_SOCKET_IN_BUFFER, SettingsManager::SOCKET_IN_BUFFER, PropPage::T_INT },
	{ IDC_SOCKET_OUT_BUFFER, SettingsManager::SOCKET_OUT_BUFFER, PropPage::T_INT },
#endif
	{ IDC_SHUTDOWN_TIMEOUT, SettingsManager::SHUTDOWN_TIMEOUT, PropPage::T_INT },
	{ IDC_MAX_COMPRESSION, SettingsManager::MAX_COMPRESSION, PropPage::T_INT },
	{ IDC_COMPRESSED_PATTERN, SettingsManager::COMPRESSED_FILES, PropPage::T_STR },
	{ IDC_DOWNCONN, SettingsManager::DOWNCONN_PER_SEC, PropPage::T_INT },
	{ IDC_CAPTION_MIN_MULTI_CHUNK_SIZE, SettingsManager::MIN_MULTI_CHUNK_SIZE, PropPage::T_INT},
	{ 0, 0, PropPage::T_END }
};

LRESULT SDCPage::onInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
	WinUtil::translate(*this, texts);
	PropPage::read(*this, items);
	
	CUpDownCtrl updown;
	SET_MIN_MAX(IDC_BUFFER_SPIN, 0, 1024  * 1024);
	SET_MIN_MAX(IDC_READ_SPIN, 1024, 128 * 1024);
	SET_MIN_MAX(IDC_WRITE_SPIN, 1024, 128 * 1024);
	SET_MIN_MAX(IDC_SHUTDOWN_SPIN, 1, 3600);
	SET_MIN_MAX(IDC_MAX_COMP_SPIN, 0, 9);
	SET_MIN_MAX(IDC_DOWNCONN_SPIN, 0, 100);
	SET_MIN_MAX(IDC_MIN_MULTI_CHUNK_SIZE_SPIN, 0, 100);
	
	ctrlShutdownAction.Attach(GetDlgItem(IDC_COMBO1));
	ctrlShutdownAction.AddString(CTSTRING(POWER_OFF));
	ctrlShutdownAction.AddString(CTSTRING(LOG_OFF));
	ctrlShutdownAction.AddString(CTSTRING(REBOOT));
	ctrlShutdownAction.AddString(CTSTRING(SUSPEND));
	ctrlShutdownAction.AddString(CTSTRING(HIBERNATE));
	
	ctrlShutdownAction.SetCurSel(SETTING(SHUTDOWN_ACTION));
	fixControls();
	return TRUE;
}

LRESULT SDCPage::onFixControls(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	fixControls();
	return 0;
}

void SDCPage::fixControls()
{
#ifdef OSVER_WIN_XP
	::EnableWindow(GetDlgItem(IDC_SOCKET_IN_BUFFER), !CompatibilityManager::isOsVistaPlus());
	::EnableWindow(GetDlgItem(IDC_SOCKET_OUT_BUFFER), !CompatibilityManager::isOsVistaPlus());
#else
	::EnableWindow(GetDlgItem(IDC_SOCKET_IN_BUFFER), FALSE);
	::EnableWindow(GetDlgItem(IDC_SOCKET_OUT_BUFFER), FALSE);
#endif
}

void SDCPage::write()
{
	PropPage::write(*this, items);
	SET_SETTING(SHUTDOWN_ACTION, ctrlShutdownAction.GetCurSel());
}
