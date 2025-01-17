/*
 *
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

#ifndef RECENTS_FRAME_H_
#define RECENTS_FRAME_H_

#include "FlatTabCtrl.h"
#include "StaticFrame.h"
#include "ExListViewCtrl.h"
#include "../client/HubEntry.h"
#include "../client/FavoriteManagerListener.h"

class RecentHubsFrame : public MDITabChildWindowImpl<RecentHubsFrame>,
	public StaticFrame<RecentHubsFrame, ResourceManager::RECENT_HUBS, IDC_RECENTS>,
	private FavoriteManagerListener,
	private SettingsManagerListener,
	public CMessageFilter
{
	public:
		typedef MDITabChildWindowImpl<RecentHubsFrame> baseClass;

		RecentHubsFrame(): xdu(0), ydu(0) {}

		RecentHubsFrame(const RecentHubsFrame &) = delete;
		RecentHubsFrame& operator=(const RecentHubsFrame &) = delete;
		
		static CFrameWndClassInfo& GetWndClassInfo();

		BEGIN_MSG_MAP(RecentHubsFrame)
		MESSAGE_HANDLER(WM_CREATE, onCreate)
		MESSAGE_HANDLER(WM_DESTROY, onDestroy)
		MESSAGE_HANDLER(WM_CLOSE, onClose)
		MESSAGE_HANDLER(WM_CONTEXTMENU, onContextMenu)
		MESSAGE_HANDLER(WM_SETFOCUS, onSetFocus)
		MESSAGE_HANDLER(FTM_GETOPTIONS, onTabGetOptions)
		COMMAND_ID_HANDLER(IDC_CONNECT, onClickedConnect)
		COMMAND_ID_HANDLER(IDC_ADD, onAddFav)
		COMMAND_ID_HANDLER(IDC_REM_AS_FAVORITE, onRemoveFav)
		COMMAND_ID_HANDLER(IDC_REMOVE, onRemove)
		COMMAND_ID_HANDLER(IDC_REMOVE_ALL, onRemoveAll)
		COMMAND_ID_HANDLER(IDC_EDIT, onEdit)
		COMMAND_ID_HANDLER(IDC_CLOSE_WINDOW, onCloseWindow)
		NOTIFY_HANDLER(IDC_RECENTS, LVN_COLUMNCLICK, onColumnClickHublist)
		NOTIFY_HANDLER(IDC_RECENTS, NM_DBLCLK, onDoubleClickHublist)
		NOTIFY_HANDLER(IDC_RECENTS, NM_RETURN, onEnter)
		NOTIFY_HANDLER(IDC_RECENTS, LVN_KEYDOWN, onKeyDown)
		NOTIFY_HANDLER(IDC_RECENTS, LVN_ITEMCHANGED, onItemchangedDirectories)
		//NOTIFY_HANDLER(IDC_RECENTS, NM_CUSTOMDRAW, ctrlHubs.onCustomDraw) // [+] IRainman
		NOTIFY_HANDLER(IDC_RECENTS, NM_CUSTOMDRAW, onCustomDraw)
		CHAIN_MSG_MAP(baseClass)
		END_MSG_MAP()
		
		LRESULT onCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
		LRESULT onDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
		LRESULT onClose(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled);
		LRESULT onDoubleClickHublist(int idCtrl, LPNMHDR pnmh, BOOL& bHandled);
		LRESULT onEnter(int idCtrl, LPNMHDR pnmh, BOOL& bHandled);
		LRESULT onAddFav(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
		LRESULT onRemoveFav(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
		LRESULT onClickedConnect(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
		LRESULT onRemove(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
		LRESULT onRemoveAll(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
		LRESULT onEdit(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
		LRESULT onCustomDraw(int /*idCtrl*/, LPNMHDR pnmh, BOOL& bHandled);
		LRESULT onTabGetOptions(UINT, WPARAM, LPARAM lParam, BOOL&);
		
		LRESULT onCloseWindow(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
		{
			PostMessage(WM_CLOSE);
			return 0;
		}
		
		LRESULT onItemchangedDirectories(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/);
		void UpdateLayout(BOOL bResizeBars = TRUE);
		
		LRESULT onContextMenu(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/);
		
		LRESULT onSetFocus(UINT /* uMsg */, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
		{
			ctrlHubs.SetFocus();
			return 0;
		}
		
		LRESULT onColumnClickHublist(int /*idCtrl*/, LPNMHDR pnmh, BOOL& /*bHandled*/);

		virtual BOOL PreTranslateMessage(MSG* pMsg) override;

	private:
		enum
		{
			COLUMN_FIRST,
			COLUMN_NAME = COLUMN_FIRST,
			COLUMN_DESCRIPTION,
			COLUMN_USERS,
			COLUMN_SHARED,
			COLUMN_SERVER,
			COLUMN_LAST_SEEN,
			COLUMN_OPEN_TAB,
			COLUMN_LAST
		};
		
		CButton ctrlConnect;
		CButton ctrlRemove;
		CButton ctrlRemoveAll;
		OMenu hubsMenu;
		ExListViewCtrl ctrlHubs;

		int xdu, ydu;
		int buttonWidth, buttonHeight, buttonSpace;
		int vertMargin, horizMargin;

		static int columnSizes[COLUMN_LAST];
		static int columnIndexes[COLUMN_LAST];
		
		void updateList(const RecentHubEntry::List& fl);
		
		void addEntry(const RecentHubEntry* entry, int pos);
		void openHubWindow(RecentHubEntry* entry);
		
		LRESULT onKeyDown(int /*idCtrl*/, LPNMHDR pnmh, BOOL& /*bHandled*/);
		
		const string getRecentServer(int pos)const
		{
			return ctrlHubs.ExGetItemText(pos, COLUMN_SERVER);
		}
		
		void on(RecentAdded, const RecentHubEntry* entry) noexcept override
		{
			addEntry(entry, ctrlHubs.GetItemCount());
		}
		void on(RecentRemoved, const RecentHubEntry* entry) noexcept override
		{
			ctrlHubs.DeleteItem(ctrlHubs.find((LPARAM)entry));
		}
		void on(RecentUpdated, const RecentHubEntry* entry) noexcept override;
		
		void on(SettingsManagerListener::Repaint) override;
};

#endif
