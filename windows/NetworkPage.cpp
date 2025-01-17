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

#include <comdef.h>
#include "Resource.h"

#include "NetworkPage.h"
#include "WinUtil.h"
#include "DialogLayout.h"
#include "../client/CryptoManager.h"
#include "../client/ConnectivityManager.h"
#include "../client/DownloadManager.h"
#include "../client/PortTest.h"
#include "../client/IpTest.h"
#include "../client/NetworkUtil.h"
#include "../client/webrtc/talk/base/winfirewall.h"

#ifdef OSVER_WIN_XP
#include "../client/CompatibilityManager.h"
#endif

using DialogLayout::FLAG_TRANSLATE;
using DialogLayout::UNSPEC;
using DialogLayout::AUTO;

NetworkSettings* NetworkPage::prevSettings = nullptr;

extern bool g_DisableTestPort;
extern int g_tlsOption;

enum
{
	IconFailure = 0,
	IconSuccess,
	IconWaiting,
	IconWarning,
	IconUnknown,
	IconQuestion,
	IconDisabled
};

static const DialogLayout::Align align1 = { 2, DialogLayout::SIDE_RIGHT, U_DU(6) };
static const DialogLayout::Align align2 = { 1, DialogLayout::SIDE_RIGHT, U_DU(6) };
static const DialogLayout::Align align3 = { 17, DialogLayout::SIDE_LEFT, U_DU(6) };
static const DialogLayout::Align align4 = { 21, DialogLayout::SIDE_RIGHT, U_DU(6) };
static const DialogLayout::Align align5 = { 15, DialogLayout::SIDE_LEFT, U_PX(-1) };
static const DialogLayout::Align align6 = { 16, DialogLayout::SIDE_RIGHT, U_PX(-1) };

static const DialogLayout::Item layoutItems1[] =
{
	{ IDC_SETTINGS_BIND_ADDRESS, FLAG_TRANSLATE, UNSPEC, UNSPEC },
	{ IDC_SETTINGS_BIND_ADDRESS_HELP, FLAG_TRANSLATE, AUTO, UNSPEC },
	{ IDC_BIND_ADDRESS, 0, UNSPEC, UNSPEC, 0, &align1, &align2 },
	{ IDC_SETTINGS_INCOMING, FLAG_TRANSLATE, UNSPEC, UNSPEC },
	{ IDC_CONNECTION_DETECTION, FLAG_TRANSLATE, AUTO, UNSPEC },
	{ IDC_DIRECT, FLAG_TRANSLATE, AUTO, UNSPEC },
	{ IDC_FIREWALL_UPNP, FLAG_TRANSLATE, AUTO, UNSPEC },
	{ IDC_FIREWALL_NAT, FLAG_TRANSLATE, AUTO, UNSPEC },
	{ IDC_FIREWALL_PASSIVE, FLAG_TRANSLATE, AUTO, UNSPEC },
	{ IDC_SETTINGS_IP, FLAG_TRANSLATE, UNSPEC, UNSPEC },
	{ IDC_WAN_IP_MANUAL, FLAG_TRANSLATE, AUTO, UNSPEC },
	{ IDC_NO_IP_OVERRIDE, FLAG_TRANSLATE, AUTO, UNSPEC },
	{ IDC_DEFAULT_GATEWAY_IP, 0, UNSPEC, UNSPEC },
	{ IDC_STATIC_GATEWAY, FLAG_TRANSLATE, AUTO, UNSPEC },
	{ IDC_SETTINGS_PORTS, FLAG_TRANSLATE, UNSPEC, UNSPEC },
	{ IDC_SETTINGS_PORTS_UPNP, 0, UNSPEC, UNSPEC },
	{ IDC_PORT_TCP, 0, UNSPEC, UNSPEC },
	{ IDC_SETTINGS_PORT_TCP, FLAG_TRANSLATE, AUTO, UNSPEC, 0, nullptr, &align3 },
	{ IDC_SETTINGS_PORT_UDP, FLAG_TRANSLATE, AUTO, UNSPEC, 0, nullptr, &align3 },
	{ IDC_SETTINGS_PORT_TLS, FLAG_TRANSLATE, AUTO, UNSPEC, 0, nullptr, &align3 },
	{ IDC_CAPTION_MAPPER, FLAG_TRANSLATE, AUTO, UNSPEC },
	{ IDC_MAPPER, 0, UNSPEC, UNSPEC, 0, &align4 },
	{ IDC_GETIP, FLAG_TRANSLATE, UNSPEC, UNSPEC, 0, &align5, &align6 }
};

static const DialogLayout::Item layoutItems2[] =
{
	{ IDC_AUTO_TEST_PORTS, FLAG_TRANSLATE, AUTO, UNSPEC },
	{ IDC_NATT, FLAG_TRANSLATE, AUTO, UNSPEC },
	{ IDC_USE_DHT, FLAG_TRANSLATE, AUTO, UNSPEC }
};

static const struct
{
	int id;
	SettingsManager::IntSetting setting;
} portSettings[] =
{
	{ IDC_PORT_TCP, SettingsManager::TCP_PORT },
	{ IDC_PORT_UDP, SettingsManager::UDP_PORT },
	{ IDC_PORT_TLS, SettingsManager::TLS_PORT }
};

static const struct
{
	SettingsManager::IntSetting setting;
	int edit;
	int protoIcon;
	int upnpIcon;
} controlInfo[] =
{
	{ SettingsManager::UDP_PORT, IDC_PORT_UDP, IDC_NETWORK_TEST_PORT_UDP_ICO, IDC_NETWORK_TEST_PORT_UDP_ICO_UPNP },
	{ SettingsManager::TCP_PORT, IDC_PORT_TCP, IDC_NETWORK_TEST_PORT_TCP_ICO, IDC_NETWORK_TEST_PORT_TCP_ICO_UPNP },
	{ SettingsManager::TLS_PORT, IDC_PORT_TLS, IDC_NETWORK_TEST_PORT_TLS_TCP_ICO, IDC_NETWORK_TEST_PORT_TLS_TCP_ICO_UPNP },
};

LRESULT NetworkIPTab::onInitDialog(UINT, WPARAM, LPARAM, BOOL&)
{
	DialogLayout::layout(m_hWnd, layoutItems1, _countof(layoutItems1));

	for (int i = 0; i < _countof(portSettings); ++i)
		SetDlgItemInt(portSettings[i].id, SettingsManager::get(portSettings[i].setting));

	int af;
	ResourceManager::Strings enableStr;
	if (v6)
	{
		enableStr = ResourceManager::ENABLE_IPV6;
		af = AF_INET6;
	}
	else
	{
		enableStr = ResourceManager::ENABLE_IPV4;
		af = AF_INET;
	}
	SettingsManager::IPSettings ips;
	SettingsManager::getIPSettings(ips, v6);

	switch (SettingsManager::get(ips.incomingConnections))
	{
		case SettingsManager::INCOMING_DIRECT:
			CheckDlgButton(IDC_DIRECT, BST_CHECKED);
			break;
		case SettingsManager::INCOMING_FIREWALL_UPNP:
			CheckDlgButton(IDC_FIREWALL_UPNP, BST_CHECKED);
			break;
		case SettingsManager::INCOMING_FIREWALL_NAT:
			CheckDlgButton(IDC_FIREWALL_NAT, BST_CHECKED);
			break;
		case SettingsManager::INCOMING_FIREWALL_PASSIVE:
			CheckDlgButton(IDC_FIREWALL_PASSIVE, BST_CHECKED);
			break;
		default:
			CheckDlgButton(IDC_DIRECT, BST_CHECKED);
			break;
	}

	CButton ctrlEnable(GetDlgItem(IDC_ENABLE));
	ctrlEnable.SetWindowText(CTSTRING_I(enableStr));
	if (v6)
		ctrlEnable.SetCheck(BOOLSETTING(ENABLE_IP6) ? BST_CHECKED : BST_UNCHECKED);
	else
	{
		ctrlEnable.SetCheck(BST_CHECKED);
		ctrlEnable.EnableWindow(FALSE);
	}

	CButton(GetDlgItem(IDC_CONNECTION_DETECTION)).SetCheck(SettingsManager::get(ips.autoDetect) ? BST_CHECKED : BST_UNCHECKED);
	CButton(GetDlgItem(IDC_WAN_IP_MANUAL)).SetCheck(SettingsManager::get(ips.manualIp) ? BST_CHECKED : BST_UNCHECKED);
	CButton(GetDlgItem(IDC_NO_IP_OVERRIDE)).SetCheck(SettingsManager::get(ips.noIpOverride) ? BST_CHECKED : BST_UNCHECKED);

	for (int i = 0; i < 3; ++i)
		CEdit(GetDlgItem(controlInfo[i].edit)).LimitText(5);

	CComboBox bindCombo(GetDlgItem(IDC_BIND_ADDRESS));
	WinUtil::fillAdapterList(af, bindCombo, SettingsManager::get(ips.bindAddress));

	CEdit ctrlExternalIP(GetDlgItem(IDC_EXTERNAL_IP));
	ctrlExternalIP.SetWindowText(Text::toT(SettingsManager::get(ips.externalIp)).c_str());
	CRect rc;
	if (v6)
	{
		ctrlExternalIP.GetWindowRect(&rc);
		ctrlExternalIP.SetWindowPos(nullptr, 0, 0, rc.Width()*2, rc.Height(), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
	}

	CEdit ctrlGatewayIP(GetDlgItem(IDC_DEFAULT_GATEWAY_IP));
	if (v6)
		ctrlGatewayIP.SetWindowPos(nullptr, 0, 0, rc.Width()*2, rc.Height(), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
	IpAddressEx gateway = Util::getDefaultGateway(af);
	ctrlGatewayIP.SetWindowText(Util::printIpAddressT(gateway).c_str());

	CComboBox mapperCombo(GetDlgItem(IDC_MAPPER));
	StringList mappers = ConnectivityManager::getInstance()->getMapper(af).getMappers();
	int selIndex = 0;
	for (size_t i = 0; i < mappers.size(); ++i)
	{
		mapperCombo.AddString(Text::toT(mappers[i]).c_str());
		if (mappers[i] == SettingsManager::get(ips.mapper))
			selIndex = i;
	}
	mapperCombo.SetCurSel(selIndex);

	if (v6)
	{
		for (int i = 0; i < 3; ++i)
		{
			CWindow wndIcon(GetDlgItem(controlInfo[i].protoIcon));
			CWindow wndEdit(GetDlgItem(controlInfo[i].edit));
			CRect rc1, rc2;
			wndIcon.GetWindowRect(&rc1);
			wndEdit.GetWindowRect(&rc2);
			wndIcon.ShowWindow(SW_HIDE);
			wndEdit.SetWindowPos(nullptr, 0, 0, rc1.left + 16 - rc2.left, rc2.Height(), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
		}
	}

	fixControls();
	return 0;
}

LRESULT NetworkIPTab::onEnable(WORD /*wNotifyCode*/, WORD /*wID*/, HWND hWndCtl, BOOL& /*bHandled*/)
{
	if (v6)
	{
		CButton cb(hWndCtl);
		if (cb.GetCheck() == BST_CHECKED && !ConnectivityManager::isIP6Supported())
		{
			MessageBox(CTSTRING(IPV6_NOT_DETECTED), getAppNameVerT().c_str(), MB_OK | MB_ICONERROR);
			cb.SetCheck(BST_UNCHECKED);
			return 0;
		}
	}
	fixControls();
	return 0;
}

LRESULT NetworkIPTab::onTestPorts(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	if (v6)
		parent->runIpTest();
	else
		parent->testPorts();
	return 0;
}

void NetworkIPTab::copyPortSettings(NetworkIPTab& copyTo) const
{
	tstring s;
	for (int i = 0; i < 3; ++i)
	{
		WinUtil::getWindowText(GetDlgItem(controlInfo[i].edit), s);
		copyTo.GetDlgItem(controlInfo[i].edit).SetWindowText(s.c_str());
	}
}

void NetworkIPTab::fixControls()
{
	const bool useTLS = parent->useTLS;
	const BOOL enabled = IsDlgButtonChecked(IDC_ENABLE) == BST_CHECKED;
	const BOOL autoDetect = enabled && IsDlgButtonChecked(IDC_CONNECTION_DETECTION) == BST_CHECKED;
	const BOOL upnp = enabled && IsDlgButtonChecked(IDC_FIREWALL_UPNP) == BST_CHECKED;
	const BOOL nat = enabled && IsDlgButtonChecked(IDC_FIREWALL_NAT) == BST_CHECKED;
	const BOOL passive = enabled && IsDlgButtonChecked(IDC_FIREWALL_PASSIVE) == BST_CHECKED;
	const BOOL manualIP = enabled && IsDlgButtonChecked(IDC_WAN_IP_MANUAL) == BST_CHECKED;

	GetDlgItem(IDC_DIRECT).EnableWindow(enabled && !autoDetect);
	GetDlgItem(IDC_FIREWALL_UPNP).EnableWindow(enabled && !autoDetect);
	GetDlgItem(IDC_FIREWALL_NAT).EnableWindow(enabled && !autoDetect);
	GetDlgItem(IDC_FIREWALL_PASSIVE).EnableWindow(enabled && !autoDetect);

	GetDlgItem(IDC_EXTERNAL_IP).EnableWindow(manualIP);
	GetDlgItem(IDC_SETTINGS_IP).EnableWindow(enabled && !autoDetect);

	GetDlgItem(IDC_NO_IP_OVERRIDE).EnableWindow(manualIP);
#if 0
	const BOOL portEnabled = enabled && !autoDetect;
#else
	const BOOL portEnabled = enabled && !parent->applyingSettings;
#endif
	GetDlgItem(IDC_PORT_TCP).EnableWindow(portEnabled);
	GetDlgItem(IDC_PORT_UDP).EnableWindow(portEnabled);
	GetDlgItem(IDC_PORT_TLS).EnableWindow(portEnabled && useTLS);
	GetDlgItem(IDC_BIND_ADDRESS).EnableWindow(enabled && !autoDetect);

	GetDlgItem(IDC_CONNECTION_DETECTION).EnableWindow(enabled);
	GetDlgItem(IDC_WAN_IP_MANUAL).EnableWindow(enabled);
	GetDlgItem(IDC_DEFAULT_GATEWAY_IP).EnableWindow(enabled);
	GetDlgItem(IDC_MAPPER).EnableWindow(enabled);
}

LRESULT NetworkIPTab::onKillFocusExternalIp(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	tstring tmp;
	CWindow externalIp(GetDlgItem(IDC_EXTERNAL_IP));
	WinUtil::getWindowText(externalIp, tmp);
	string ipStr = Text::fromT(tmp);
	if (!ipStr.empty())
	{
		IpAddress ip;
		if (!Util::parseIpAddress(ip, ipStr) || ip.type != (v6 ? AF_INET6 : AF_INET))
		{
			ipStr = v6 ? SETTING(EXTERNAL_IP6) : SETTING(EXTERNAL_IP);
			MessageBox(CTSTRING(BAD_IP_ADDRESS), getAppNameVerT().c_str(), MB_OK | MB_ICONWARNING);
			externalIp.SetWindowText(Text::toT(ipStr).c_str());
		}
	}
	return 0;
}

int NetworkIPTab::getConnectionType() const
{
	int ct = SettingsManager::INCOMING_DIRECT;
	if (IsDlgButtonChecked(IDC_FIREWALL_UPNP))
		ct = SettingsManager::INCOMING_FIREWALL_UPNP;
	else if (IsDlgButtonChecked(IDC_FIREWALL_NAT))
		ct = SettingsManager::INCOMING_FIREWALL_NAT;
	else if (IsDlgButtonChecked(IDC_FIREWALL_PASSIVE))
		ct = SettingsManager::INCOMING_FIREWALL_PASSIVE;
	return ct;
}

static void setIcon(HWND hwnd, int stateIcon)
{
	static HIconWrapper g_hModeActiveIco(IDR_ICON_SUCCESS_ICON);
	static HIconWrapper g_hModePassiveIco(IDR_ICON_WARN_ICON);
	static HIconWrapper g_hModeQuestionIco(IDR_ICON_QUESTION_ICON);
	static HIconWrapper g_hModeFailIco(IDR_ICON_FAIL_ICON);
	static HIconWrapper g_hModePauseIco(IDR_ICON_PAUSE_ICON);
	static HIconWrapper g_hModeProcessIco(IDR_NETWORK_STATISTICS_ICON);
	//static HIconWrapper g_hModeDisableTestIco(IDR_SKULL_RED_ICO);

	HICON icon;
	switch (stateIcon)
	{
		case IconFailure:
			icon = (HICON) g_hModeFailIco;
			break;
		case IconSuccess:
			icon = (HICON) g_hModeActiveIco;
			break;
		case IconWarning:
			icon = (HICON) g_hModePassiveIco;
			break;
		case IconUnknown:
			icon = (HICON) g_hModePauseIco;
			break;
		case IconQuestion:
			icon = (HICON) g_hModeQuestionIco;
			break;
		case IconDisabled:
			icon = nullptr;
			break;
		case IconWaiting:
		default:
			icon = (HICON) g_hModeProcessIco;
	}
	CWindow wnd(hwnd);
	if (icon)
	{
		wnd.SendMessage(STM_SETICON, (WPARAM) icon, 0);
		wnd.ShowWindow(SW_SHOW);
	}
	else
		wnd.ShowWindow(SW_HIDE);
}

static int getIconForPortState(int state)
{
	if (g_DisableTestPort) return IconDisabled;
	if (state == PortTest::STATE_SUCCESS) return IconSuccess;
	if (state == PortTest::STATE_FAILURE) return IconFailure;
	if (state == PortTest::STATE_RUNNING) return IconWaiting;
	return IconQuestion;
}

static int getIconForMappingState(int state)
{
	if (state == MappingManager::STATE_SUCCESS) return IconSuccess;
	if (state == MappingManager::STATE_FAILURE) return IconFailure;
	if (state == MappingManager::STATE_RENEWAL_FAILURE) return IconWarning;
	if (state == MappingManager::STATE_RUNNING) return IconWaiting;
	return IconQuestion;
}

void NetworkIPTab::updateState()
{
	static_assert(PortTest::PORT_UDP == 0 && PortTest::PORT_TCP == 1 && PortTest::PORT_TLS == 2, "PortTest constants mismatch");
	int af = v6 ? AF_INET6 : AF_INET;
	const MappingManager& mapper = ConnectivityManager::getInstance()->getMapper(af);
	bool running = false;
	for (int type = 0; type < PortTest::MAX_PORTS; type++)
	{
		const auto& ci = controlInfo[type];
		int port, portIcon, mappingIcon;
		int mappingState = mapper.getState(type);
		int portState = PortTest::STATE_UNKNOWN;
		if (!v6)
		{
			portState = g_portTest.getState(type, port, nullptr);
			if (portState == PortTest::STATE_RUNNING) running = true;
		}
		else
		{
			int getIpState = g_ipTest.getState(IpTest::REQ_IP6, nullptr);
			if (getIpState == IpTest::STATE_RUNNING) running = true;
		}
		if (type == PortTest::PORT_TLS && !parent->useTLS)
		{
			portIcon = IconDisabled;
			mappingIcon = IconDisabled;
		}
		else
		{
			portIcon = getIconForPortState(portState);
			mappingIcon = getIconForMappingState(mappingState);
			if (mappingIcon == IconFailure && portIcon == IconSuccess)
				mappingIcon = IconUnknown;
		}
		if (!v6) setIcon(GetDlgItem(ci.protoIcon), portIcon);
		setIcon(GetDlgItem(ci.upnpIcon), mappingIcon);
	}

	CButton ctrl(GetDlgItem(IDC_GETIP));
	if (running)
	{
		ctrl.SetWindowText(v6 ? CTSTRING(GETTING_IP) : CTSTRING(TESTING_PORTS));
		ctrl.EnableWindow(FALSE);
	}
	else if (ConnectivityManager::getInstance()->isSetupInProgress())
	{
		ctrl.SetWindowText(CTSTRING(APPLYING_SETTINGS));
		ctrl.EnableWindow(FALSE);
	}
	else
	{
		ctrl.SetWindowText(v6 ? CTSTRING(GET_IP) : CTSTRING(TEST_PORTS_AND_GET_IP));
		ctrl.EnableWindow(IsDlgButtonChecked(IDC_ENABLE) == BST_CHECKED);
	}

	CWindow externalIp(GetDlgItem(IDC_EXTERNAL_IP));
	if (!externalIp.IsWindowEnabled())
	{
		IpAddress ipAddr = ConnectivityManager::getInstance()->getReflectedIP(af);
		externalIp.SetWindowText(Util::printIpAddressT(ipAddr).c_str());
	}
}

void NetworkIPTab::updatePortNumbers()
{
	bool updatePrevSettings = false;
	tstring oldText, newText;
	for (int type = 0; type < PortTest::MAX_PORTS; type++)
	{
		const auto& ci = controlInfo[type];
		CEdit edit(GetDlgItem(ci.edit));
		WinUtil::getWindowText(edit, oldText);
		newText = Util::toStringT(SettingsManager::get(ci.setting));
		if (oldText != newText)
		{
			edit.SetWindowText(newText.c_str());
			updatePrevSettings = true;
		}
	}
	if (updatePrevSettings && NetworkPage::prevSettings)
		NetworkPage::prevSettings->get();
}

void NetworkIPTab::updateTLSOption()
{
	const BOOL enabled = IsDlgButtonChecked(IDC_ENABLE) == BST_CHECKED;
#if 0
	const BOOL autoDetect = IsDlgButtonChecked(IDC_CONNECTION_DETECTION) == BST_CHECKED;
	GetDlgItem(IDC_PORT_TLS).EnableWindow(enabled && !autoDetect && parent->useTLS);
#else
	GetDlgItem(IDC_PORT_TLS).EnableWindow(enabled && parent->useTLS);
#endif
	updateState();
}

void NetworkIPTab::writePortSettings()
{
	tstring buf;
	for (int i = 0; i < 3; ++i)
	{
		WinUtil::getWindowText(GetDlgItem(portSettings[i].id), buf);
		SettingsManager::set(portSettings[i].setting, Util::toInt(buf));
	}
}

void NetworkIPTab::writeOtherSettings()
{
	int af = v6 ? AF_INET6 : AF_INET;
	SettingsManager::IPSettings ips;
	SettingsManager::getIPSettings(ips, v6);

	if (v6) SettingsManager::set(SettingsManager::ENABLE_IP6, IsDlgButtonChecked(IDC_ENABLE) == BST_CHECKED);
	SettingsManager::set(ips.autoDetect, IsDlgButtonChecked(IDC_CONNECTION_DETECTION) == BST_CHECKED);
	SettingsManager::set(ips.manualIp, IsDlgButtonChecked(IDC_WAN_IP_MANUAL) == BST_CHECKED);
	SettingsManager::set(ips.noIpOverride, IsDlgButtonChecked(IDC_NO_IP_OVERRIDE) == BST_CHECKED);

	CWindow externalIp(GetDlgItem(IDC_EXTERNAL_IP));
	if (externalIp.IsWindowEnabled())
	{
		tstring str;
		WinUtil::getWindowText(externalIp, str);
		SettingsManager::set(ips.externalIp, Text::fromT(str));
	}
	else
		SettingsManager::set(ips.externalIp, Util::emptyString);

	SettingsManager::set(ips.bindAddress, WinUtil::getSelectedAdapter(CComboBox(GetDlgItem(IDC_BIND_ADDRESS))));
	SettingsManager::set(ips.incomingConnections, getConnectionType());

	CComboBox mapperCombo(GetDlgItem(IDC_MAPPER));
	int selIndex = mapperCombo.GetCurSel();
	StringList mappers = ConnectivityManager::getInstance()->getMapper(af).getMappers();
	if (selIndex >= 0 && selIndex < (int) mappers.size())
		SettingsManager::set(ips.mapper, mappers[selIndex]);
}

void NetworkIPTab::getPortSettingsFromUI(NetworkSettings& settings) const
{
	tstring buf;
	WinUtil::getWindowText(GetDlgItem(IDC_PORT_TCP), buf);
	settings.portTCP = Util::toInt(buf);
	if (parent->useTLS)
	{
		WinUtil::getWindowText(GetDlgItem(IDC_PORT_TLS), buf);
		settings.portTLS = Util::toInt(buf);
	}
	else
		settings.portTLS = -1;
	WinUtil::getWindowText(GetDlgItem(IDC_PORT_UDP), buf);
	settings.portUDP = Util::toInt(buf);
}

void NetworkIPTab::getOtherSettingsFromUI(NetworkSettings& settings) const
{
	int index = v6 ? 1 : 0;
	if (v6) settings.enableV6 = IsDlgButtonChecked(IDC_ENABLE) == BST_CHECKED;
	settings.autoDetect[index] = IsDlgButtonChecked(IDC_CONNECTION_DETECTION) == BST_CHECKED;
	settings.incomingConn[index] = getConnectionType();
	settings.bindAddr[index] = WinUtil::getSelectedAdapter(CComboBox(GetDlgItem(IDC_BIND_ADDRESS)));

	CComboBox mapperCombo(GetDlgItem(IDC_MAPPER));
	int selIndex = mapperCombo.GetCurSel();
	StringList mappers = ConnectivityManager::getInstance()->getMapper(v6 ? AF_INET6 : AF_INET).getMappers();
	if (selIndex >= 0 && selIndex < (int) mappers.size())
		settings.mapper[index] = mappers[selIndex];
}

LRESULT NetworkFirewallTab::onInitDialog(UINT, WPARAM, LPARAM, BOOL&)
{
	appPath = Util::getModuleFileName();
	ctrlButton.Attach(GetDlgItem(IDC_ADD_FIREWALL_EXCEPTION));
	ctrlButton.SetWindowText(CTSTRING(ADD_FIREWALL_EXCEPTION));
	ctrlText.Attach(GetDlgItem(IDC_FIREWALL_STATUS));
	ctrlIcon.Attach(GetDlgItem(IDC_NETWORK_WINFIREWALL_ICO));
#ifdef OSVER_WIN_XP
	if (CompatibilityManager::isOsVistaPlus())
#endif
		ctrlButton.SendMessage(BCM_FIRST + 0x000C, 0, 0xFFFFFFFF);

	testWinFirewall();
	return 0;
}

LRESULT NetworkFirewallTab::onAddWinFirewallException(WORD /* wNotifyCode */, WORD /*wID*/, HWND /* hWndCtl */, BOOL& /* bHandled */)
{
	const tstring appPath = Util::getModuleFileName();
	if (IsUserAnAdmin())
	{
		talk_base::WinFirewall fw;
		HRESULT hr;
		fw.Initialize(&hr);
		const auto res = fw.AddApplicationW(appPath.c_str(), getAppNameT().c_str(), true, &hr);
		if (res)
		{
			MessageBox(CTSTRING(FIREWALL_EXCEPTION_ADDED), getAppNameVerT().c_str(), MB_OK | MB_ICONINFORMATION);
		}
		else
		{
			_com_error msg(hr);
			MessageBox(CTSTRING_F(FIREWALL_EXCEPTION_ERROR, msg.ErrorMessage() % Util::toHexStringT(hr)),
				getAppNameVerT().c_str(), MB_OK | MB_ICONERROR);
		}
		testWinFirewall();
	} else
	{
		if (WinUtil::runElevated(m_hWnd, appPath.c_str(), _T("/addFw"), nullptr, -1) && testWinFirewall())
			MessageBox(CTSTRING(FIREWALL_EXCEPTION_ADDED), getAppNameVerT().c_str(), MB_OK | MB_ICONINFORMATION);
	}
	return 0;
}

bool NetworkFirewallTab::testWinFirewall()
{
	talk_base::WinFirewall fw;
	HRESULT hr;
	fw.Initialize(&hr);
	if (!fw.Enabled())
	{
		ctrlText.SetWindowText(CTSTRING(WINDOWS_FIREWALL_DISABLED));
		ctrlButton.EnableWindow(FALSE);
		setIcon(ctrlIcon, IconUnknown);
		return false;
	}

	bool authorized = false;
	bool res = fw.QueryAuthorizedW(appPath.c_str(), &authorized);
	if (res)
	{
		if (authorized)
		{
			ctrlText.SetWindowText(CTSTRING(FIREWALL_EXCEPTION_ADDED));
			setIcon(ctrlIcon, IconSuccess);
		}
		else
		{
			ctrlText.SetWindowText(CTSTRING(FIREWALL_EXCEPTION_NOT_ADDED));
			setIcon(ctrlIcon, IconFailure);
		}
	}
	else
	{
		ctrlText.SetWindowText(CTSTRING(FIREWALL_EXCEPTION_NOT_ADDED));
		setIcon(ctrlIcon, IconQuestion);
	}
	ctrlButton.EnableWindow(TRUE);
	return authorized;
}

static const PropPage::Item items[] =
{
	{ IDC_AUTO_TEST_PORTS, SettingsManager::AUTO_TEST_PORTS,     PropPage::T_BOOL },
	{ IDC_NATT,            SettingsManager::ALLOW_NAT_TRAVERSAL, PropPage::T_BOOL },
	{ IDC_USE_DHT,         SettingsManager::USE_DHT,             PropPage::T_BOOL },
	{ 0,                   0,                                    PropPage::T_END  }
};

#define ADD_TAB(name, type, text) \
	tcItem.pszText = const_cast<TCHAR*>(CTSTRING(text)); \
	name.Create(m_hWnd, type::IDD); \
	ctrlTabs.InsertItem(n++, &tcItem);

LRESULT NetworkPage::onInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
	useTLS = BOOLSETTING(USE_TLS);
	applyingSettings = ConnectivityManager::getInstance()->isSetupInProgress();
	DialogLayout::layout(m_hWnd, layoutItems2, _countof(layoutItems2));

	ctrlTabs.Attach(GetDlgItem(IDC_TABS));
	TCITEM tcItem;
	tcItem.mask = TCIF_TEXT | TCIF_PARAM;
	tcItem.iImage = -1;

	int n = 0;
	ADD_TAB(tabIP[0], NetworkIPTab, IPV4);
	ADD_TAB(tabIP[1], NetworkIPTab, IPV6);
	ADD_TAB(tabFirewall, NetworkFirewallTab, WINDOWS_FIREWALL);

	ctrlTabs.SetCurSel(0);
	changeTab();
	updatePortState();

	PropPage::read(*this, items);

	return TRUE;
}

void NetworkPage::write()
{
	PropPage::write(*this, items);
	int currentTab = ctrlTabs.GetCurSel();
	if (!(currentTab == 0 || currentTab == 1)) currentTab = 0;
	tabIP[currentTab].writePortSettings();
	tabIP[0].writeOtherSettings();
	tabIP[1].writeOtherSettings();
	SET_SETTING(USE_TLS, useTLS);
}

void NetworkPage::updatePortState()
{
	tabIP[0].updateState();
	tabIP[1].updateState();
	applyingSettings = ConnectivityManager::getInstance()->isSetupInProgress();
	if (applyingSettings)
	{
		int currentTab = ctrlTabs.GetCurSel();
		if (!(currentTab == 0 || currentTab == 1)) currentTab = 0;
		tabIP[currentTab].updatePortNumbers();
	}
}

void NetworkPage::changeTab()
{
	int pos = ctrlTabs.GetCurSel();
	tabIP[0].ShowWindow(SW_HIDE);
	tabIP[1].ShowWindow(SW_HIDE);
	tabFirewall.ShowWindow(SW_HIDE);

	CRect rc;
	ctrlTabs.GetClientRect(&rc);
	ctrlTabs.AdjustRect(FALSE, &rc);
	ctrlTabs.MapWindowPoints(m_hWnd, &rc);
	HWND hwnd;

	switch (pos)
	{
		case 0:
		case 1:
			hwnd = tabIP[pos];
			if (prevTab == 0 || prevTab == 1)
				tabIP[prevTab].copyPortSettings(tabIP[pos]);
			break;
		case 2:
			hwnd = tabFirewall;
			break;
		default:
			return;
	}

	CWindow wnd(hwnd);
	wnd.MoveWindow(&rc);
	wnd.ShowWindow(SW_SHOW);
	prevTab = pos;
}

void NetworkPage::onShow()
{
	if (!g_tlsOption) return;
	useTLS = g_tlsOption == 1;
	tabIP[0].updateTLSOption();
	tabIP[1].updateTLSOption();
	Invalidate();
}

bool NetworkPage::runPortTest()
{
	int portTCP = SETTING(TCP_PORT);
	g_portTest.setPort(PortTest::PORT_TCP, portTCP);
	int portUDP = SETTING(UDP_PORT);
	g_portTest.setPort(PortTest::PORT_UDP, portUDP);
	int mask = 1<<PortTest::PORT_UDP | 1<<PortTest::PORT_TCP;
	if (useTLS)
	{
		int portTLS = SETTING(TLS_PORT);
		g_portTest.setPort(PortTest::PORT_TLS, portTLS);
		mask |= 1<<PortTest::PORT_TLS;
	}
	if (!g_portTest.runTest(mask)) return false;
	tabIP[0].updateState();
	return true;
}

bool NetworkPage::runIpTest()
{
	if (!g_ipTest.runTest(IpTest::REQ_IP6)) return false;
	tabIP[1].updateState();
	return true;
}

void NetworkPage::testPorts()
{
	tabFirewall.testWinFirewall();
	updatePortState();
	NetworkSettings currentSettings;
	currentSettings.get();
	NetworkSettings newSettings;
	getFromUI(newSettings);
	if (!currentSettings.compare(newSettings))
	{
	    if (MessageBox(CTSTRING(NETWORK_SETTINGS_CHANGED), getAppNameVerT().c_str(), MB_YESNO | MB_ICONQUESTION) != IDYES)
			return;
		write();
		ConnectivityManager::getInstance()->setupConnections(true);
		if (prevSettings)
			prevSettings->get(); // save current settings so we don't call setupConnections twice
	}
	if (!ConnectivityManager::getInstance()->isSetupInProgress())
		runPortTest();
}

void NetworkPage::getFromUI(NetworkSettings& settings) const
{
	int currentTab = ctrlTabs.GetCurSel();
	if (!(currentTab == 0 || currentTab == 1)) currentTab = 0;
	tabIP[currentTab].getPortSettingsFromUI(settings);
	tabIP[0].getOtherSettingsFromUI(settings);
	tabIP[1].getOtherSettingsFromUI(settings);
}

void NetworkSettings::get()
{
	portTCP = SETTING(TCP_PORT);
	portTLS = BOOLSETTING(USE_TLS) ? SETTING(TLS_PORT) : -1;
	portUDP = SETTING(UDP_PORT);
	enableV6 = BOOLSETTING(ENABLE_IP6);
	autoDetect[0] = BOOLSETTING(AUTO_DETECT_CONNECTION);
	autoDetect[1] = BOOLSETTING(AUTO_DETECT_CONNECTION6);
	incomingConn[0] = SETTING(INCOMING_CONNECTIONS);
	incomingConn[1] = SETTING(INCOMING_CONNECTIONS6);
	bindAddr[0] = SETTING(BIND_ADDRESS);
	bindAddr[1] = SETTING(BIND_ADDRESS6);
	mapper[0] = SETTING(MAPPER);
	mapper[1] = SETTING(MAPPER6);
}

static bool isEmptyAddress(const string& s, int af)
{
	switch (af)
	{
		case AF_INET:
		{
			Ip4Address ip;
			return Util::parseIpAddress(ip, s) && ip == 0;
		}
		case AF_INET6:
		{
			Ip6Address ip;
			return Util::parseIpAddress(ip, s) && Util::isEmpty(ip);
		}
	}
	return false;
}

static bool compareBindAddress(const string& s1, const string& s2, int af)
{
	if (s1 == s2) return true;
	if (s1.empty() && isEmptyAddress(s2, af)) return true;
	if (s2.empty() && isEmptyAddress(s1, af)) return true;
	return false;
}

bool NetworkSettings::compare(const NetworkSettings& other) const
{
	if (!(portTCP == other.portTCP &&
          portTLS == other.portTLS &&
          portUDP == other.portUDP &&
          enableV6 == other.enableV6 &&
          autoDetect[0] == other.autoDetect[0] &&
          incomingConn[0] == other.incomingConn[0] &&
          compareBindAddress(bindAddr[0],  other.bindAddr[0], AF_INET) &&
          mapper[0] == other.mapper[0])) return false;
	if (enableV6 &&
	    !(autoDetect[1] == other.autoDetect[1] &&
          incomingConn[1] == other.incomingConn[1] &&
          compareBindAddress(bindAddr[1],  other.bindAddr[1], AF_INET6) &&
          mapper[1] == other.mapper[1])) return false;
	return true;
}
