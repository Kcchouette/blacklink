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

#ifndef FINISHED_DL_FRAME_H
#define FINISHED_DL_FRAME_H

#include "FinishedFrameBase.h"

class FinishedDLFrame :
	public FinishedFrame<FinishedDLFrame, ResourceManager::FINISHED_DOWNLOADS, IDC_FINISHED, IDR_FINISHED_DL>
{
	public:
		FinishedDLFrame(): FinishedFrame(e_TransferDownload)
		{
			boldFinished = SettingsManager::BOLD_FINISHED_DOWNLOADS;
			columnOrder = SettingsManager::FINISHED_DL_FRAME_ORDER;
			columnWidth = SettingsManager::FINISHED_DL_FRAME_WIDTHS;
			columnVisible = SettingsManager::FINISHED_DL_FRAME_VISIBLE;
			columnSort = SettingsManager::FINISHED_DL_FRAME_SORT;
		}
		
		DECLARE_FRAME_WND_CLASS_EX(_T("FinishedDLFrame"), IDR_FINISHED_DL, 0, COLOR_3DFACE);
		
	private:
		void on(AddedDl, const FinishedItemPtr& entry, bool isSqlite) noexcept override
		{
			if (isSqlite)
				SendMessage(WM_SPEAKER, SPEAK_ADD_ITEM, (LPARAM) new FinishedItemPtr(entry));
			else
				PostMessage(WM_SPEAKER, SPEAK_ADD_ITEM, (LPARAM) new FinishedItemPtr(entry));
		}
		
		void on(RemovedDl, const FinishedItemPtr& entry) noexcept override
		{
			SendMessage(WM_SPEAKER, SPEAK_REMOVE_ITEM, (LPARAM) new FinishedItemPtr(entry));
		}
};

#endif // FINISHED_DL_FRAME_H