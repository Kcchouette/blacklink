/*
 * Copyright (C) 2011-2017 FlylinkDC++ Team http://flylinkdc.com
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
#include "MessagePanel.h"
#include "Resource.h"
#include "../client/ClientManager.h"
#include "../client/CompatibilityManager.h"

#ifdef IRAINMAN_INCLUDE_SMILE
#include "Emoticons.h"
#include "EmoticonsDlg.h"
#include "WinUtil.h"
#endif

static const int BUTTON_WIDTH = 26;
static const int BUTTON_HEIGHT = 26;
static const int EDIT_HEIGHT = 22;

HIconWrapper MessagePanel::g_hSendMessageIco(IDR_SENDMESSAGES_ICON);
HIconWrapper MessagePanel::g_hMultiChatIco(IDR_MULTI_CHAT_ICON);
#ifdef IRAINMAN_INCLUDE_SMILE
HIconWrapper MessagePanel::g_hEmoticonIco(IDR_SMILE_ICON);
#endif
HIconWrapper MessagePanel::g_hBoldIco(IDR_BOLD_ICON);
HIconWrapper MessagePanel::g_hUndelineIco(IDR_UNDERLINE_ICON);
HIconWrapper MessagePanel::g_hStrikeIco(IDR_STRIKE_ICON);
HIconWrapper MessagePanel::g_hItalicIco(IDR_ITALIC_ICON);
HIconWrapper MessagePanel::g_hTransCodeIco(IDR_TRANSCODE_ICON);
HIconWrapper MessagePanel::g_hColorIco(IDR_COLOR_ICON);

#ifdef IRAINMAN_INCLUDE_SMILE
OMenu MessagePanel::g_emoMenu;
int MessagePanel::emoMenuItemCount = 0;
#endif

MessagePanel::MessagePanel(CEdit& ctrlMessage)
	: m_hWnd(nullptr), ctrlMessage(ctrlMessage), initialized(false)
{
}

void MessagePanel::destroyPanel()
{
	for (int i = 0; i < MAX_BUTTONS; ++i)
		ctrlButtons[i].DestroyWindow();
#ifdef FLYLINKDC_USE_BB_SIZE_CODE
	ctrlSizeSel.DestroyWindow();
#endif
	tooltip.DestroyWindow();
	initialized = false;
}

void MessagePanel::initPanel(HWND hWnd)
{
	initialized = true;
	m_hWnd = hWnd;
	RECT rcDefault = {};
	tooltip.Create(m_hWnd, rcDefault, NULL, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP | TTS_BALLOON, WS_EX_TOPMOST);
	tooltip.SetDelayTime(TTDT_AUTOPOP, 15000);
	dcassert(tooltip.IsWindow());

	createButton(BUTTON_SEND, g_hSendMessageIco, IDC_SEND_MESSAGE, ResourceManager::BBCODE_PANEL_SENDMESSAGE);
	createButton(BUTTON_MULTILINE, g_hMultiChatIco, IDC_MESSAGEPANEL, ResourceManager::BBCODE_PANEL_MESSAGEPANELSIZE);
#ifdef IRAINMAN_INCLUDE_SMILE
	createButton(BUTTON_EMOTICONS, g_hEmoticonIco, IDC_EMOT, ResourceManager::BBCODE_PANEL_EMOTICONS);
#endif
	createButton(BUTTON_TRANSCODE, g_hTransCodeIco, ID_TEXT_TRANSCODE, ResourceManager::BBCODE_PANEL_TRANSLATE);
	createButton(BUTTON_BOLD, g_hBoldIco, IDC_BOLD, ResourceManager::BBCODE_PANEL_BOLD);
	createButton(BUTTON_ITALIC, g_hItalicIco, IDC_ITALIC, ResourceManager::BBCODE_PANEL_ITALIC);
	createButton(BUTTON_UNDERLINE, g_hUndelineIco, IDC_UNDERLINE, ResourceManager::BBCODE_PANEL_UNDERLINE);
	createButton(BUTTON_STRIKETHROUGH, g_hStrikeIco, IDC_STRIKE, ResourceManager::BBCODE_PANEL_STRIKE);
	createButton(BUTTON_COLOR, g_hColorIco, IDC_COLOR, ResourceManager::BBCODE_PANEL_COLOR);

#ifdef FLYLINKDC_USE_BB_SIZE_CODE
	ctrlSizeSel.Create(m_hWnd, rcDefault, NULL, WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS |
	                   WS_HSCROLL | WS_VSCROLL | CBS_DROPDOWNLIST, WS_EX_CLIENTEDGE);
	ctrlSizeSel.SetFont(Fonts::g_font);
	
	ctrlSizeSel.AddString(L"-2");
	ctrlSizeSel.AddString(L"-1");
	ctrlSizeSel.AddString(L"+1");
	ctrlSizeSel.AddString(L"+2");
	ctrlSizeSel.SetCurSel(2);
#endif
	
	tooltip.SetMaxTipWidth(200);
	if (BOOLSETTING(CHAT_PANEL_SHOW_INFOTIPS))
		tooltip.Activate(TRUE);
}

void MessagePanel::createButton(int index, HICON icon, int idc, ResourceManager::Strings caption)
{
	RECT rc = {};
	ctrlButtons[index].Create(m_hWnd, rc, NULL, WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS | BS_ICON | BS_CENTER, 0, idc);
#ifdef OSVER_WIN_XP
	if (!CompatibilityManager::isOsVistaPlus())
		imageButtons[index].SubclassWindow(ctrlButtons[index]);
#endif
	ctrlButtons[index].SetIcon(icon);
	tooltip.AddTool(ctrlButtons[index], caption);
}

void MessagePanel::updateButton(HDWP dwp, bool show, int index, CRect& rc)
{
	if (show)
	{
		rc.left = rc.right;
		rc.right += BUTTON_WIDTH;
		DeferWindowPos(dwp, ctrlButtons[index], nullptr,
			rc.left, rc.top, rc.Width(), rc.Height(), SWP_NOZORDER | SWP_SHOWWINDOW);
	}
	else
	{
		DeferWindowPos(dwp, ctrlButtons[index], nullptr,
			0, 0, 0, 0, SWP_NOZORDER | SWP_HIDEWINDOW);
	}
}

void MessagePanel::updatePanel(const CRect& rect)
{
	dcassert(!ClientManager::isBeforeShutdown());
	tooltip.Activate(FALSE);
	if (m_hWnd == NULL)
		return;
		
	CRect rc = rect;

	rc.right = rc.left + 2;
	rc.bottom = rc.top + BUTTON_HEIGHT;

	HDWP dwp = BeginDeferWindowPos(MAX_BUTTONS);
	updateButton(dwp, BOOLSETTING(SHOW_SEND_MESSAGE_BUTTON), BUTTON_SEND, rc);
	updateButton(dwp, BOOLSETTING(SHOW_MULTI_CHAT_BTN), BUTTON_MULTILINE, rc);
#ifdef IRAINMAN_INCLUDE_SMILE
	updateButton(dwp, BOOLSETTING(SHOW_EMOTICONS_BTN), BUTTON_EMOTICONS, rc);
#endif
	if (BOOLSETTING(SHOW_BBCODE_PANEL))
	{
		updateButton(dwp, BOOLSETTING(SHOW_TRANSCODE_BTN), BUTTON_TRANSCODE, rc);
		for (int i = BUTTON_TRANSCODE + 1; i < BUTTON_COLOR; ++i)
		{
			rc.left = rc.right;
			rc.right += BUTTON_WIDTH;
			ctrlButtons[i].ShowWindow(SW_SHOW);
			ctrlButtons[i].MoveWindow(rc);
		}
		updateButton(dwp, BOOLSETTING(FORMAT_BB_CODES_COLORS), BUTTON_COLOR, rc);
#ifdef FLYLINKDC_USE_BB_SIZE_CODE
		// Size Selection
		//rc.left = rc.right + 1;
		//rc.right += 40;
		//ctrlSizeSel.ShowWindow(SW_SHOW);
		//ctrlSizeSel.MoveWindow(rc);
		ctrlSizeSel.ShowWindow(SW_HIDE);// [!] SSA - Will enable on implementation of size-BBCode
#endif // FLYLINKDC_USE_BB_SIZE_CODE
	}
	else
	{
		for (int i = BUTTON_TRANSCODE; i < MAX_BUTTONS; ++i)
			updateButton(dwp, false, i, rc);
#ifdef FLYLINKDC_USE_BB_SIZE_CODE
		ctrlSizeSel.ShowWindow(SW_HIDE);
#endif
	}
	EndDeferWindowPos(dwp);
	if (BOOLSETTING(CHAT_PANEL_SHOW_INFOTIPS))
		tooltip.Activate(TRUE);
}

int MessagePanel::getPanelWidth()
{
	int width = 4;
	width += BOOLSETTING(SHOW_MULTI_CHAT_BTN) ? BUTTON_WIDTH : 0;
#ifdef IRAINMAN_INCLUDE_SMILE
	width += BOOLSETTING(SHOW_EMOTICONS_BTN) ? BUTTON_WIDTH : 0;
#endif
	width += BOOLSETTING(SHOW_SEND_MESSAGE_BUTTON) ? BUTTON_WIDTH : 0;
	if (BOOLSETTING(SHOW_BBCODE_PANEL))
		width += BUTTON_WIDTH * (BOOLSETTING(SHOW_TRANSCODE_BTN) ? 6 : 5);
	return width;
}

#if 0
bool MessagePanel::setFocusToControl()
{
	if (!initialized) return false;
	for (int i = 0; i < MAX_BUTTONS; ++i)
		if (ctrlButtons[i] && ctrlButtons[i].IsWindowVisible())
		{
			ctrlButtons[i].SetFocus();
			return true;
		}
	return false;
}
#endif

#ifdef IRAINMAN_INCLUDE_SMILE
void MessagePanel::pasteText(const tstring& text)
{
	int start, end;
	ctrlMessage.GetSel(start, end);
	tstring message;
	WinUtil::getWindowText(ctrlMessage, message);
	message.replace(start, end - start, text);
	ctrlMessage.SetWindowText(message.c_str());
	start += text.length();
	ctrlMessage.SetSel(start, start);
}

LRESULT MessagePanel::onPasteText(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/)
{
	if (lParam)
	{
		tstring* text = reinterpret_cast<tstring*>(lParam);
		pasteText(*text);
		delete text;
	}
	return 0;
}

LRESULT MessagePanel::onEmoticons(WORD /*wNotifyCode*/, WORD /*wID*/, HWND hWndCtl, BOOL& bHandled)
{
	tooltip.Activate(FALSE);
	if (hWndCtl != ctrlButtons[BUTTON_EMOTICONS].m_hWnd)
	{
		bHandled = FALSE;
		return 0;
	}
	if (SETTING(EMOTICONS_FILE) == "Disabled")
	{
		MessageBox(m_hWnd, CTSTRING(EMOTICONS_DISABLED), getAppNameVerT().c_str(), MB_ICONINFORMATION | MB_OK);
		return 0;
	}
	EmoticonsDlg dlg;
	ctrlButtons[BUTTON_EMOTICONS].GetWindowRect(dlg.pos);
	dlg.hWndNotif = m_hWnd;
	dlg.DoModal(m_hWnd);
	if (dlg.isError)
	{
		MessageBox(m_hWnd, CTSTRING(EMOTICONS_NOT_AVAILABLE), getAppNameVerT().c_str(), MB_ICONWARNING | MB_OK);
		return 0;
	}
	if (!dlg.result.empty())
		pasteText(dlg.result);
	ctrlMessage.SetFocus();
	if (BOOLSETTING(CHAT_PANEL_SHOW_INFOTIPS))
	{
		if (tooltip.IsWindow())
			tooltip.Activate(TRUE);
	}
	return 0;
}

LRESULT MessagePanel::onEmoPackChange(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled)
{
	tooltip.Activate(FALSE);
	TCHAR buf[256];
	CMenuItemInfo mii;
	mii.fMask = MIIM_STRING;
	mii.cch = _countof(buf);
	mii.dwTypeData = buf;
	if (!g_emoMenu.GetMenuItemInfo(wID, FALSE, &mii))
		return -1;
	string emoticonsFile = Text::fromT(buf);
	if (SETTING(EMOTICONS_FILE) != emoticonsFile)
	{
		SET_SETTING(EMOTICONS_FILE, emoticonsFile);
		if (!EmoticonPack::reCreate())
			return -1;
	}
	if (BOOLSETTING(CHAT_PANEL_SHOW_INFOTIPS))
		tooltip.Activate(TRUE);
	return 0;
}
#endif // IRAINMAN_INCLUDE_SMILE

BOOL MessagePanel::onContextMenu(POINT& pt, WPARAM& wParam)
{
#ifdef IRAINMAN_INCLUDE_SMILE
	if (reinterpret_cast<HWND>(wParam) == ctrlButtons[BUTTON_EMOTICONS])
	{
		showEmoticonsMenu(g_emoMenu, pt, m_hWnd, IDC_EMOMENU, emoMenuItemCount);
		return TRUE;
	}
#endif
	return FALSE;
}

#ifdef IRAINMAN_INCLUDE_SMILE
void MessagePanel::showEmoticonsMenu(OMenu& menu, const POINT& pt, HWND hWnd, int idc, int& count)
{
	count = 0;
	if (menu.m_hMenu)
		menu.DestroyMenu();
	menu.SetOwnerDraw(OMenu::OD_NEVER);
	menu.CreatePopupMenu();
	CMenuItemInfo mii;
	mii.fMask = MIIM_ID | MIIM_TYPE | MIIM_STATE;
	mii.wID = idc;
	mii.fType = MFT_STRING | MFT_RADIOCHECK;
	mii.dwTypeData = const_cast<TCHAR*>(CTSTRING(DISABLED));
	mii.fState = MFS_ENABLED | (SETTING(EMOTICONS_FILE) == "Disabled" ? MFS_CHECKED : 0);
	int menuPos = 0;
	menu.InsertMenuItem(menuPos++, TRUE, &mii);

	WIN32_FIND_DATA data;
	HANDLE hFind = FindFirstFileEx(Text::toT(Util::getEmoPacksPath() + "*.xml").c_str(),
	                               CompatibilityManager::findFileLevel,
	                               &data,
	                               FindExSearchNameMatch,
	                               nullptr,
	                               0);
	if (hFind != INVALID_HANDLE_VALUE)
	{
		do
		{
			tstring name = data.cFileName;
			tstring::size_type i = name.rfind('.');
			name.erase(i);
			count++;

			if (menuPos == 1)
			{
				mii.fType = MFT_SEPARATOR;
				mii.fState = MFS_ENABLED;
				mii.wID = 0;
				menu.InsertMenuItem(menuPos++, TRUE, &mii);
				mii.fType = MFT_STRING | MFT_RADIOCHECK;
			}

			mii.wID = idc + count;
			mii.dwTypeData = const_cast<TCHAR*>(name.c_str());
			mii.fState = MFS_ENABLED;
			if (name == Text::toT(SETTING(EMOTICONS_FILE)))
				mii.fState |= MFS_CHECKED;
			menu.InsertMenuItem(menuPos++, TRUE, &mii);
		}
		while (FindNextFile(hFind, &data));
		FindClose(hFind);
	}
	if (!count)
	{
		MessageBox(hWnd, CTSTRING(EMOTICONS_NOT_AVAILABLE), getAppNameVerT().c_str(), MB_ICONWARNING | MB_OK);
		return;
	}
	menu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, pt.x, pt.y, hWnd);
}
#endif
