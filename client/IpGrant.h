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

#ifndef IPGRANT_H
#define IPGRANT_H

#ifdef SSA_IPGRANT_FEATURE

#include "IpList.h"
#include "webrtc/rtc_base/synchronization/rw_lock_wrapper.h"

class IpGrant
{
	public:
		IpGrant();
		bool check(uint32_t addr) const noexcept;
		void load() noexcept;
		void clear() noexcept;
		static std::string getFileName();

	private:
		IpList ipList;
		mutable unique_ptr<webrtc::RWLockWrapper> cs;
};

extern IpGrant ipGrant;

#endif // SSA_IPGRANT_FEATURE

#endif // IPGRANT_H