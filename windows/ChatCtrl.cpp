/*
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
#include "ChatCtrl.h"
#include "WinUtil.h"
#include "../client/OnlineUser.h"
#include "../client/Client.h"
#include "../client/MagnetLink.h"
#include "../client/CompatibilityManager.h"
#include <boost/algorithm/string.hpp>
#include <tom.h>

#ifdef IRAINMAN_INCLUDE_SMILE
#include "../GdiOle/GDIImageOle.h"
#include "Emoticons.h"
#include "MainFrm.h"

static const unsigned MAX_EMOTICONS_PER_MESSAGE = 48;
#endif // IRAINMAN_INCLUDE_SMILE

#ifdef _UNICODE
#define HIDDEN_TEXT_SEP L'\x241F'
#else
#define HIDDEN_TEXT_SEP '\x05'
#endif

extern "C" const GUID IID_ITextDocument =
{
	0x8CC497C0, 0xA1DF, 0x11CE, { 0x80, 0x98, 0x00, 0xAA, 0x00, 0x47, 0xBE, 0x5D }
};

static const tstring badUrlChars(_T("\r\n \"<>[]"));
static const tstring urlDelimChars(_T(",.;!?"));
static const tstring nickBoundaryChars(_T("\r\n \"<>,.:;()!?*%+-"));

static const tstring linkPrefixes[] =
{
	_T("magnet:?"),
	_T("dchub://"),
	_T("nmdc://"),
	_T("nmdcs://"),
	_T("adc://"),
	_T("adcs://"),
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

static const int LINK_TYPE_MAGNET = 0;
static const int LINK_TYPE_HTTP   = 6;
static const int LINK_TYPE_WWW    = _countof(linkPrefixes)-1;

struct SubstringInfo
{
	uint64_t value;
	uint64_t mask;
};

static SubstringInfo substringInfo[_countof(linkPrefixes)];

enum
{
	BBCODE_CODE,
	BBCODE_BOLD,
	BBCODE_ITALIC,
	BBCODE_UNDERLINE,
	BBCODE_STRIKEOUT,
	BBCODE_IMAGE,
	BBCODE_COLOR
};

static const tstring bbCodes[] =
{
	_T("code"),
	_T("b"),
	_T("i"),
	_T("u"),
	_T("s"),
	_T("img"), 
	_T("color"),
};

static int findBBCode(const tstring& tag)
{
	for (int i = 0; i < _countof(bbCodes); ++i)
		if (bbCodes[i] == tag) return i;
	return -1;
}

static void makeSubstringInfo()
{
	for (int i = 0; i < _countof(linkPrefixes); ++i)
	{
		uint64_t mask = std::numeric_limits<uint64_t>::max();
		uint64_t val = 0;
		const TCHAR* c = linkPrefixes[i].data();
		size_t len = linkPrefixes[i].length();
		if (len > 8)
		{
			c += len - 8;
			len = 8;
		}
		else
			mask >>= (8 - len) * 8;
		for (size_t i = 0; i < len; ++i)
			val = val<<8 | (uint8_t) c[i];
		substringInfo[i].mask = mask;
		substringInfo[i].value = val;
	}
}

tstring ChatCtrl::g_sSelectedText;
tstring ChatCtrl::g_sSelectedIP;
tstring ChatCtrl::g_sSelectedUserName;
tstring ChatCtrl::g_sSelectedURL;

ChatCtrl::ChatCtrl() : autoScroll(true), disableChatCacheFlag(false), chatCacheSize(0),
	ignoreLinkStart(0), ignoreLinkEnd(0), selectedLine(-1), pRichEditOle(nullptr)
#ifdef IRAINMAN_INCLUDE_SMILE
	,outOfMemory(false), totalEmoticons(0), pStorage(nullptr), refs(0)
#endif
{
}

ChatCtrl::~ChatCtrl()
{
	if (pRichEditOle)
		pRichEditOle->Release();
#ifdef IRAINMAN_INCLUDE_SMILE
	if (pStorage)
		pStorage->Release();
#endif
}

void ChatCtrl::adjustTextSize()
{
	int maxLines = SETTING(CHAT_BUFFER_SIZE);
	if (maxLines <= 0) return;
	int lineCount = GetLineCount();
	if (lineCount > maxLines)	
	{
		dcdebug("Removing %d lines\n", lineCount - maxLines);
		int pos = LineIndex(lineCount - maxLines);
		SetSel(0, pos);
		ReplaceSel(_T(""));
	}
}

static void normalizeLineBreaks(tstring& text)
{
	boost::replace_all(text, _T("\r\n"), _T("\n"));
	boost::replace_all(text, _T("\n\r"), _T("\n"));
	std::replace(text.begin(), text.end(), _T('\r'), _T('\n'));
}

ChatCtrl::Message::Message(const Identity* id, bool myMessage, bool thirdPerson, const tstring& extra, const tstring& text, const CHARFORMAT2& cf, bool useEmoticons, bool removeLineBreaks /*= true*/) :
	nick(id ? Text::toT(id->getNick()) : Util::emptyStringT), myMessage(myMessage),
	isRealUser(id ? !id->isBotOrHub() : false), msg(text), cf(cf),
	thirdPerson(thirdPerson), extra(extra), useEmoticons(useEmoticons)
{
	dcassert(!ClientManager::isBeforeShutdown());
	if (!ClientManager::isBeforeShutdown())
	{
		if (removeLineBreaks)
			normalizeLineBreaks(msg);
		msg += _T('\n');

#ifdef IRAINMAN_INCLUDE_SMILE
		if (!EmoticonPack::current || CompatibilityManager::isWine())
			useEmoticons = false;
#endif
		
		isFavorite = isBanned = isOp = false;
		if (id)
		{
			isOp = id->isOp();
			if (!thirdPerson && !myMessage)
			{
				auto flags = id->getUser()->getFlags();
				if (flags & User::FAVORITE)
				{
					isFavorite = true;
					if (flags & User::BANNED) isBanned = true;
				}
			}
		}
	}
}

void ChatCtrl::restoreChatCache()
{
	CWaitCursor waitCursor;
	{
#if 0
		for (int i = 0; i < 3000; ++i)
		{
			Message message(nullptr, false, true,
			                _T('[') + Text::toT(Util::getShortTimeString()) + _T("] "),
			                Util::toStringT(i) + _T(" ]Test!Test!Test!Test!Test!Test!Test!Test!Test!Test!Test!Test!Test!Test!Test!Test!Test!Test!Test!Test!Test!Test!Test!Test!Test!Test!Test!Test!Test!Test!Test!"),
			                Colors::g_ChatTextOldHistory, false);
			message.nick = _T("FlylinkDC-Debug-TEST");
			chatCache.push_back(message);
		}
#endif
		std::list<Message> tempChatCache;
		bool cacheDisabled = true;
		{
			LOCK(csChatCache);
			std::swap(disableChatCacheFlag, cacheDisabled);
			tempChatCache.swap(chatCache);
			chatCacheSize = 0;
		}
		int count = tempChatCache.size();
		tstring oldText;
		for (auto i = tempChatCache.begin(); i != tempChatCache.end(); ++i)
		{
			if (ClientManager::isBeforeShutdown())
				return;
			if (count-- > 40) // Disable styles for old messages
			{
				//i->m_bUseEmo = false;
				//i->m_is_disable_style = true;
				oldText += _T("* ") + i->extra + _T(' ') + i->nick + _T(' ') + i->msg + _T("\r\n");
				continue;
			}
			if (!oldText.empty())
			{
				LONG selBegin = 0;
				LONG selEnd = 0;
				insertAndFormat(oldText, i->cf, selBegin, selEnd);
				oldText.clear();
			}
			if (!cacheDisabled)
				appendText(*i, 0);
		}
	}
#ifdef IRAINMAN_INCLUDE_SMILE
	outOfMemory = false; // ???
#endif
}

void ChatCtrl::insertAndFormat(const tstring& text, CHARFORMAT2 cf, LONG& startPos, LONG& endPos)
{
	if (ClientManager::isBeforeShutdown())
		return;
	dcassert(!text.empty());
	if (!text.empty())
	{
		startPos = endPos = GetTextLengthEx(GTL_NUMCHARS);
		SetSel(endPos, endPos);
		ReplaceSel(text.c_str());
		endPos = GetTextLengthEx(GTL_NUMCHARS);
		SetSel(startPos, endPos);
		SetSelectionCharFormat(cf);
	}
}

void ChatCtrl::appendText(const Message& message, unsigned maxSmiles)
{
	dcassert(!ClientManager::isBeforeShutdown());
	if (ClientManager::isBeforeShutdown())
		return;
	{
		LOCK(csChatCache);
		if (!disableChatCacheFlag)
		{
			chatCache.push_back(message);
			chatCacheSize += message.length();
			if (chatCacheSize + 1000 > SETTING(CHAT_BUFFER_SIZE))
			{
				chatCacheSize -= chatCache.front().length();
				chatCache.pop_front();
			}
			return;
		}
	}
	LONG selBeginSaved = 0, selEndSaved = 0;
	GetSel(selBeginSaved, selEndSaved);
	POINT cr = { 0 };
	GetScrollPos(&cr);
	
	LONG selBegin = 0;
	LONG selEnd = 0;
	
	// Insert extra info and format with default style
	if (!message.extra.empty())
	{
		insertAndFormat(message.extra, Colors::g_TextStyleTimestamp, selBegin, selEnd);
		PARAFORMAT2 pf;
		memset(&pf, 0, sizeof(PARAFORMAT2));
		pf.dwMask = PFM_STARTINDENT;
		pf.dxStartIndent = 0;
		SetParaFormat(pf);
	}
	
	tstring text = message.msg;
	if (message.nick.empty())
	{
		// Local message
	}
	else if (message.thirdPerson)
	{
		const CHARFORMAT2& currentCF =
		    message.myMessage ? Colors::g_ChatTextMyOwn :
		    BOOLSETTING(BOLD_MSG_AUTHOR) ? Colors::g_TextStyleBold :
		    message.cf;
		insertAndFormat(_T("* "), message.cf, selBegin, selEnd);
		insertAndFormat(message.nick, currentCF, selBegin, selEnd);
		insertAndFormat(_T(" "), message.cf, selBegin, selEnd);
	}
	else
	{
		static const tstring g_open = _T("<");
		static const tstring g_close = _T("> ");
		const CHARFORMAT2& currentCF =
		    message.myMessage ? Colors::g_TextStyleMyNick :
		    message.isFavorite ? (message.isBanned ? Colors::g_TextStyleFavUsersBan : Colors::g_TextStyleFavUsers) :
		    message.isOp ? Colors::g_TextStyleOPs :
		    BOOLSETTING(BOLD_MSG_AUTHOR) ? Colors::g_TextStyleBold :
		    message.cf;
		insertAndFormat(g_open, message.cf, selBegin, selEnd);
		insertAndFormat(message.nick, currentCF, selBegin, selEnd);
		insertAndFormat(g_close, message.cf, selBegin, selEnd);
	}
	
	appendTextInternal(text, message, maxSmiles);
	SetSel(selBeginSaved, selEndSaved);
	goToEnd(cr, false);
}

void ChatCtrl::appendTextInternal(tstring& text, const Message& message, unsigned maxSmiles)
{
	dcassert(!ClientManager::isBeforeShutdown());
	if (!ClientManager::isBeforeShutdown())
		parseText(text, message, maxSmiles);
}

void ChatCtrl::appendTextInternal(tstring&& text, const Message& message, unsigned maxSmiles)
{
	dcassert(!ClientManager::isBeforeShutdown());
	if (!ClientManager::isBeforeShutdown())
		parseText(text, message, maxSmiles);
}

static inline void shiftPos(tstring::size_type& pos, tstring::size_type movePos, int shift)
{
	if (pos != tstring::npos && pos > movePos)
		pos += shift;
}

void ChatCtrl::applyShift(size_t tagsStartIndex, size_t linksStartIndex, tstring::size_type start, int shift)
{
	for (size_t j = tagsStartIndex; j < tags.size(); ++j)
	{
		shiftPos(tags[j].openTagStart, start, shift);
		shiftPos(tags[j].openTagEnd, start, shift);
	}
	for (size_t j = 0; j < tags.size(); ++j)
	{
		shiftPos(tags[j].closeTagStart, start, shift);
		shiftPos(tags[j].closeTagEnd, start, shift);
	}
	for (size_t j = linksStartIndex; j < links.size(); ++j)
	{
		shiftPos(links[j].start, start, shift);
		shiftPos(links[j].end, start, shift);
	}
}

void ChatCtrl::parseText(tstring& text, const Message& message, unsigned maxSmiles)
{
	SetRedraw(FALSE);
	const auto& cf = message.myMessage ? Colors::g_ChatTextMyOwn : message.cf;
	const bool formatBBCodes = BOOLSETTING(FORMAT_BB_CODES) && (message.isRealUser || BOOLSETTING(FORMAT_BOT_MESSAGE));
	static std::atomic_bool substringInfoInitialized = false;
	if (!substringInfoInitialized)
	{
		makeSubstringInfo();
		substringInfoInitialized = true;
	}

	tags.clear();
	links.clear();
	uint64_t hash = 0;
	tstring tagData;
	tstring::size_type tagStart = tstring::npos;
	TagItem ti;
	LinkItem li;
	li.start = tstring::npos;
	TCHAR linkPrevChar = 0;
	int ipv6Link = 0;
	for (tstring::size_type i = 0; i < text.length(); ++i)
	{
		TCHAR c = text[i];
		if (c >= _T('A') && c <= _T('Z')) c = c - _T('A') + _T('a');
		hash = hash << 8 | (uint8_t) c;
		if (li.start != tstring::npos)
		{
			if (c == _T('[') && ipv6Link == 0 && i-li.start <= 8 && text[i-1] == _T('/'))
			{
				ipv6Link = 1;
				continue;
			}
			if (c == _T(']') && ipv6Link == 1)
			{
				ipv6Link = 2;
				continue;
			}
			if (badUrlChars.find(c) == tstring::npos)
				continue;
			li.end = i;
			TCHAR pairChar = 0;
			switch (linkPrevChar)
			{
				case _T('('): pairChar = _T(')'); break;
				case _T('{'): pairChar = _T('}'); break;
				case _T('\''): case _T('*'): pairChar = linkPrevChar;
			}
			TCHAR lastChar = text[li.end-1];
			if ((pairChar && lastChar == pairChar) || urlDelimChars.find(lastChar) != tstring::npos)
				--li.end;
			links.push_back(li);
			li.start = tstring::npos;
			ipv6Link = 0;
		}
		if (c == _T('[') && formatBBCodes)
		{
			tagStart = i;
			tagData.clear();
			continue;
		}
		if (c == _T(']'))
		{
			if (!tagData.empty())
			{
				if (tagData[0] == _T('/'))
				{
					tagData.erase(0, 1);
					int type = findBBCode(tagData);
					if (type != -1)
					{
						int index = tags.size()-1;
						while (index >= 0)
						{
							if (tags[index].type == type && tags[index].closeTagStart == tstring::npos)
							{
								tags[index].closeTagStart = tagStart;
								tags[index].closeTagEnd = i + 1;
							}
							index--;
						}
					}
					tagStart = tstring::npos;
					tagData.clear();
					continue;
				}
				const CHARFORMAT2& prevFmt = tags.empty() ? cf : tags.back().fmt;
				if (processTag(ti, tagData, tagStart, i + 1, prevFmt))
					tags.push_back(ti);
			}
			continue;
		}
		if (tagStart != tstring::npos && tagData.length() < 32)
			tagData += c;
		for (int j = 0; j < _countof(linkPrefixes); ++j)
			if ((hash & substringInfo[j].mask) == substringInfo[j].value)
			{
				const tstring& link = linkPrefixes[j];
				tstring::size_type start = i+1-link.length();
				if (i >= link.length()-1 && text.compare(start, link.length(), link) == 0 &&
				    (start == 0 || !_istalpha(text[start-1])))
				{
					li.type = j;
					li.start = start;
					li.end = tstring::npos;
					li.hiddenTextLen = 0;
					linkPrevChar = start == 0 ? 0 : text[start-1];
				}
				break;
			}
	}

	if (li.start != tstring::npos)
	{
		li.end = text.length();
		links.push_back(li);
	}

	for (size_t i = 0; i < tags.size(); ++i)
	{
		const TagItem& ti = tags[i];
		if (ti.openTagStart != tstring::npos && ti.openTagEnd != tstring::npos &&
		    ti.closeTagStart != tstring::npos && ti.closeTagEnd != tstring::npos)
		{
			tstring::size_type start = ti.openTagStart;
			tstring::size_type len = ti.openTagEnd - ti.openTagStart;
			applyShift(i + 1, 0, start, -(int) len);
			text.erase(start, len);
			
			start = ti.closeTagStart;
			len = ti.closeTagEnd - ti.closeTagStart;
			applyShift(i + 1, 0, start, -(int) len);
			text.erase(start, len);
		}
	}

	for (size_t i = 0; i < links.size(); ++i)
	{
		LinkItem& li = links[i];
		processLink(text, li);
		if (!li.updatedText.empty())
		{
			tstring::size_type len = li.end - li.start;
			int shift = (int) li.updatedText.length() - (int) len;
			applyShift(0, i, li.start, shift);
			text.replace(li.start, len, li.updatedText);
		}
	}

	LONG startPos, endPos;
	insertAndFormat(text, cf, startPos, endPos);
	for (TagItem& ti : tags)
	{
		if (ti.openTagStart == tstring::npos || ti.closeTagEnd == tstring::npos) continue;
		SetSel(startPos + ti.openTagStart, startPos + ti.closeTagEnd);
		SetSelectionCharFormat(ti.fmt);
	}
	tags.clear();

	for (const LinkItem& li : links)
	{
		if (li.start == tstring::npos || li.end == tstring::npos) continue;
		auto tmp = Colors::g_TextStyleURL;
		SetSel(startPos + li.start, startPos + li.end - li.hiddenTextLen);
		SetSelectionCharFormat(tmp);
		if (li.hiddenTextLen)
		{
			tmp.dwMask |= CFM_HIDDEN;
			tmp.dwEffects |= CFE_HIDDEN;
			SetSel(startPos + li.end - li.hiddenTextLen, startPos + li.end);
			SetSelectionCharFormat(tmp);
		}
	}

#ifdef IRAINMAN_INCLUDE_SMILE
	extern DWORD g_GDI_count;
	if (g_GDI_count >= 8000)
	{
		LogManager::message("[!] GDI count >= 8000 - disable smiles!");
	}
	if (message.useEmoticons && !outOfMemory && g_GDI_count < 8000)
	{		
		HWND mainFrameWnd = MainFrame::getMainFrame()->m_hWnd;
		const vector<Emoticon*>& emoticons = EmoticonPack::current->getSortedEmoticons();
		const tstring replacement(1, HIDDEN_TEXT_SEP);
		size_t imageIndex = 0;
		size_t currentLink = 0;
		tstring::size_type pos = 0;
		unsigned messageEmoticons = 0;
		while (imageIndex < emoticons.size() && !outOfMemory && messageEmoticons < MAX_EMOTICONS_PER_MESSAGE && (maxSmiles == 0 || totalEmoticons < maxSmiles))
		{
			const tstring& emoticonText = emoticons[imageIndex]->getText();
			findSubstringAvoidingLinks(pos, text, emoticonText, currentLink);
			if (pos != tstring::npos)
			{
				SetSel(startPos + pos, startPos + pos + emoticonText.length());
				ReplaceSel(Util::emptyStringT.c_str());
				text.replace(pos, emoticonText.length(), replacement);
				applyShift(0, 0, pos, -(int) emoticonText.length() + 1);
				
				IOleClientSite *pOleClientSite = nullptr;
					
				if (!pRichEditOle)
					initRichEditOle();

				if (pRichEditOle && SUCCEEDED(pRichEditOle->GetClientSite(&pOleClientSite)) && pOleClientSite)
				{
					IOleObject *pObject = emoticons[imageIndex]->getImageObject(BOOLSETTING(CHAT_ANIM_SMILES) ? Emoticon::FLAG_PREFER_GIF : 0,
						pOleClientSite, pStorage, mainFrameWnd, WM_ANIM_CHANGE_FRAME,
						message.myMessage ? Colors::g_ChatTextMyOwn.crBackColor : Colors::g_ChatTextGeneral.crBackColor,
						emoticonText);
					if (pObject)
					{
						if (CImageDataObject::InsertObject(m_hWnd, pRichEditOle, pOleClientSite, pStorage, pObject))
						{
							messageEmoticons++;
							totalEmoticons++;
						}
						pObject->Release();
					}
					pOleClientSite->Release();
				}
				// TODO: handle failures
			}
			else
			{
				pos = 0;
				currentLink = 0;
				imageIndex++;
			}
		}
	}
#endif // IRAINMAN_INCLUDE_SMILE
	
	if (!myNick.empty())
	{
		bool nickFound = false;
		size_t currentLink = 0;
		tstring::size_type pos = 0;
		while (true)
		{
			findSubstringAvoidingLinks(pos, text, myNick, currentLink);
			if (pos == tstring::npos) break;
			if ((pos == 0 || nickBoundaryChars.find(text[pos-1]) != tstring::npos) &&
			    (pos + myNick.length() >= text.length() || nickBoundaryChars.find(text[pos + myNick.length()]) != tstring::npos))
			{
				SetSel(startPos + pos, startPos + pos + myNick.length());
				auto tmp = Colors::g_TextStyleMyNick;
				SetSelectionCharFormat(tmp);
				nickFound = true;
				pos += myNick.length() + 1;
			}
			else
				++pos;
		}
		if (nickFound)
			PLAY_SOUND(SOUND_CHATNAMEFILE);
	}

	SetRedraw(TRUE);
	RedrawWindow(NULL, NULL, RDW_ERASE | RDW_FRAME | RDW_INVALIDATE | RDW_ALLCHILDREN);
	links.clear();
}

bool ChatCtrl::processTag(ChatCtrl::TagItem& ti, tstring& tag, tstring::size_type start, tstring::size_type end, const CHARFORMAT2& prevFmt)
{
	if (tag.compare(0, 6, _T("color=")) == 0)
	{
		ti.type = BBCODE_COLOR;
		ti.fmt = prevFmt;
		ti.fmt.dwMask |= CFM_COLOR;
		tag.erase(0, 6);
		Colors::getColorFromString(tag, ti.fmt.crTextColor);
		ti.openTagStart = start;
		ti.openTagEnd = end;
		ti.closeTagStart = ti.closeTagEnd = tstring::npos;
		return true;
	}
	ti.type = findBBCode(tag);
	switch (ti.type)
	{
		case BBCODE_BOLD:
			ti.fmt = prevFmt;
			ti.fmt.dwMask = CFM_BOLD | CFM_ITALIC | CFM_UNDERLINE | CFM_STRIKEOUT;
			ti.fmt.dwEffects |= CFE_BOLD;
			break;
		case BBCODE_ITALIC:
			ti.fmt = prevFmt;
			ti.fmt.dwMask = CFM_BOLD | CFM_ITALIC | CFM_UNDERLINE | CFM_STRIKEOUT;
			ti.fmt.dwEffects |= CFE_ITALIC;
			break;
		case BBCODE_UNDERLINE:
			ti.fmt = prevFmt;
			ti.fmt.dwMask = CFM_BOLD | CFM_ITALIC | CFM_UNDERLINE | CFM_STRIKEOUT;
			ti.fmt.dwEffects |= CFE_UNDERLINE;
			break;
		case BBCODE_STRIKEOUT:
			ti.fmt = prevFmt;
			ti.fmt.dwMask = CFM_BOLD | CFM_ITALIC | CFM_UNDERLINE | CFM_STRIKEOUT;
			ti.fmt.dwEffects |= CFE_STRIKEOUT;
			break;
		default:
			return false;
	}
	ti.openTagStart = start;
	ti.openTagEnd = end;
	ti.closeTagStart = ti.closeTagEnd = tstring::npos;
	return true;
}

void ChatCtrl::processLink(const tstring& text, ChatCtrl::LinkItem& li)
{
	if (li.type == LINK_TYPE_MAGNET)
	{
		tstring link = text.substr(li.start, li.end - li.start);
		MagnetLink magnet;
		if (!magnet.parse(Text::fromT(link)))
		{
			li.start = li.end = tstring::npos;
			li.type = -1;
			return;
		}
		bool isTorrentLink = Util::isTorrentLink(link);
		li.updatedText = Text::toT(magnet.getFileName());
		tstring description;
		if (magnet.exactLength > 0)
			description = Util::formatBytesT(magnet.exactLength);
		if (magnet.dirSize > 0)
		{
			if (!description.empty()) description += _T(", ");
			description += TSTRING(SETTINGS_SHARE_SIZE) + _T(' ') + Util::formatBytesT(magnet.dirSize);
		}
		if (isTorrentLink)
		{
			if (!description.empty()) description += _T(", ");
			description += TSTRING(BT_LINK);
		}
		if (!isTorrentLink && !magnet.exactSource.empty())
		{
			if (!description.empty()) description += _T(", ");
			description += TSTRING(HUB) + _T(": ") + Text::toT(Util::formatDchubUrl(magnet.exactSource));
		}
		if (!magnet.acceptableSource.empty())
		{
			if (!description.empty()) description += _T(", ");
			description += TSTRING(WEB_URL) + _T(": ") + Text::toT(magnet.acceptableSource);
		}
		if (!description.empty())
		{
			li.updatedText += _T(" (");
			li.updatedText += description;
			li.updatedText += _T(')');
		}
		li.updatedText += HIDDEN_TEXT_SEP;
		li.updatedText += link;
		li.updatedText += HIDDEN_TEXT_SEP;
		li.hiddenTextLen = link.length() + 2;
	}
	else if (li.type == LINK_TYPE_WWW)
	{
		tstring link = text.substr(li.start, li.end - li.start);
		li.updatedText = link;
		link.insert(0, linkPrefixes[LINK_TYPE_HTTP]);
		li.updatedText += HIDDEN_TEXT_SEP;
		li.updatedText += link;
		li.updatedText += HIDDEN_TEXT_SEP;
		li.hiddenTextLen = link.length() + 2;
	}
}

void ChatCtrl::findSubstringAvoidingLinks(tstring::size_type& pos, tstring& text, const tstring& str, size_t& currentLink) const
{
	while (pos < text.length())
	{
		auto nextPos = text.find(str, pos);
		if (nextPos == tstring::npos) break;
		while (currentLink < links.size() && links[currentLink].end <= nextPos)
			++currentLink;
		if (currentLink >= links.size() || nextPos + str.length() - 1 < links[currentLink].start)
		{
			pos = nextPos;
			return;
		}
		pos = links[currentLink].end;
	}
	pos = tstring::npos;
}

bool ChatCtrl::hitNick(const POINT& p, tstring& nick, int& startPos, int& endPos)
{
	static const int MAX_NICK_LEN = 64;
	if (hubHint.empty()) return false;

	int charPos = CharFromPos(p);
	int lineIndex = LineFromChar(charPos);
	int lineStart = LineIndex(lineIndex);
	int lineEnd = lineStart + LineLength(charPos);

	if (!(charPos >= lineStart && charPos < lineEnd)) return false;

	if (charPos - MAX_NICK_LEN > lineStart) lineStart = charPos - MAX_NICK_LEN;
	if (charPos + MAX_NICK_LEN < lineEnd) lineEnd = charPos + MAX_NICK_LEN;

	tstring line;
	int len = lineEnd - lineStart;
	line.resize(len);
	GetTextRange(lineStart, lineEnd, &line[0]);
	tstring::size_type pos = charPos - lineStart;
	tstring::size_type nickStart = line.find_last_of(nickBoundaryChars, pos);
	if (nickStart == tstring::npos || nickStart == pos) return false;
	tstring::size_type nickEnd = line.find_first_of(nickBoundaryChars, pos + 1);
	if (nickEnd == tstring::npos) nickEnd = line.length();
	nick = line.substr(nickStart + 1, nickEnd - (nickStart + 1));
	if (!ClientManager::findLegacyUser(Text::fromT(nick), hubHint)) return false;

	startPos = lineStart + nickStart + 1;
	endPos = lineStart + nickEnd;
	return true;
}

bool ChatCtrl::hitIP(const POINT& p, tstring& result, int& startPos, int& endPos)
{
	// TODO: add IPv6 support.
	const int charPos = CharFromPos(p);
	int len = LineLength(charPos) + 1;
	if (len < 7) // 1.1.1.1
		return false;
		
	DWORD posBegin = FindWordBreak(WB_LEFT, charPos);
	DWORD posEnd = FindWordBreak(WB_RIGHTBREAK, charPos);
	len = (int) posEnd - (int) posBegin;
	
	if (len < 7) // 1.1.1.1
		return false;
		
	tstring text;
	text.resize(len);
	GetTextRange(posBegin, posEnd, &text[0]);
	
	for (size_t i = 0; i < text.length(); i++)
		if (!(text[i] == _T('.') || (text[i] >= _T('0') && text[i] <= _T('9'))))
			return false;
	
	if (!Util::isValidIp4(text))
		return false;
	
	result = std::move(text);
	startPos = posBegin;
	endPos = posEnd;
	return true;
}

bool ChatCtrl::hitText(tstring& text, int selBegin, int selEnd) const
{
	text.resize(selEnd - selBegin);
	GetTextRange(selBegin, selEnd, &text[0]);
	return !text.empty();
}

void ChatCtrl::goToEnd(POINT& scrollPos, bool force)
{
	SCROLLINFO si = { 0 };
	si.cbSize = sizeof(si);
	si.fMask = SIF_PAGE | SIF_RANGE | SIF_POS;
	GetScrollInfo(SB_VERT, &si);
	if (autoScroll || force)
	{
		// this must be called twice to work properly :(
		PostMessage(EM_SCROLL, SB_BOTTOM, 0);
		PostMessage(EM_SCROLL, SB_BOTTOM, 0);
	}
	SetScrollPos(&scrollPos);
}

void ChatCtrl::goToEnd(bool force)
{
	POINT pt = { 0 };
	GetScrollPos(&pt);
	goToEnd(pt, force);
	if (autoScroll || force)
	{
		// this must be called twice to work properly :(
		PostMessage(EM_SCROLL, SB_BOTTOM, 0);
		PostMessage(EM_SCROLL, SB_BOTTOM, 0);
	}
}

void ChatCtrl::invertAutoScroll()
{
	setAutoScroll(!autoScroll);
}

void ChatCtrl::setAutoScroll(bool flag)
{
	autoScroll = flag;
	if (autoScroll)
		goToEnd(false);
}

LRESULT ChatCtrl::onRButtonDown(POINT pt)
{
	selectedLine = LineFromChar(CharFromPos(pt));

	g_sSelectedText.clear();
	g_sSelectedUserName.clear();
	g_sSelectedIP.clear();
	g_sSelectedURL.clear();
	
	LONG selBegin, selEnd;
	GetSel(selBegin, selEnd);

	if (selEnd > selBegin)
	{
		CHARFORMAT2 cf;
		cf.cbSize = sizeof(cf);
		cf.dwMask = CFM_LINK;
		GetSelectionCharFormat(cf);
		if (cf.dwEffects & CFE_LINK)
		{
			g_sSelectedURL = getUrl(selBegin, selEnd, true);
			if (!g_sSelectedURL.empty()) return 1;
		}
	}
	
	const int charPos = CharFromPos(pt);
	int begin, end;
	if (selEnd > selBegin && charPos >= selBegin && charPos <= selEnd)
	{
		if (!hitIP(pt, g_sSelectedIP, begin, end))
			if (!hitNick(pt, g_sSelectedUserName, begin, end))
				hitText(g_sSelectedText, selBegin, selEnd);
				
		return 1;
	}
	
	// hightlight IP or nick when clicking on it
	if (hitIP(pt, g_sSelectedIP, begin, end) || hitNick(pt, g_sSelectedUserName, begin, end))
	{
		SetSel(begin, end);
		InvalidateRect(nullptr);
	}
	return 1;
}

//[+] sergiy.karasov
//���������� ����������� � ���� ���� ��� �������� ������ ���� �����
//������� ���������� ���� � ���� (�� ������� �����), ���� ��������������� (������� ����) �� ������ ����
LRESULT ChatCtrl::onMouseWheel(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
	RECT rc;
	POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) }; // location of mouse click
	SCROLLINFO si = { 0 };
	
	// Get the bounding rectangle of the client area.
	ChatCtrl::GetClientRect(&rc);
	ChatCtrl::ScreenToClient(&pt);
	if (PtInRect(&rc, pt))
	{
		si.cbSize = sizeof(si);
		si.fMask = SIF_PAGE | SIF_RANGE | SIF_POS;
		GetScrollInfo(SB_VERT, &si);
		
		if (GET_WHEEL_DELTA_WPARAM(wParam) > 0) //positive - wheel was rotated forward
		{
			if (si.nMin != si.nPos) //������� �� � ������ ��� ���� ���������
			{
				setAutoScroll(false);
			}
		}
		else //negative value indicates that the wheel was rotated backward, toward the user
		{
			//���� ���������
			//��������� - �� ������������ WM_MOUSEWHEEL ����� ��� �������� ������������,
			//������� ����������, ��������, ��������� ��� �������������� (�.�. ����� ��� ���������, �� ���� ��� ���)
			//�������� ������ ����� ����
			if (si.nPos == int(si.nMax - si.nPage))
			{
				setAutoScroll(true);
			}
		}
	}
	bHandled = FALSE;
	return 1;
}

tstring ChatCtrl::getUrlHiddenText(LONG end)
{
	TCHAR sep[2] = {};
	GetTextRange(end, end + 1, sep);
	if (sep[0] != HIDDEN_TEXT_SEP) return Util::emptyStringT;
	int line = LineFromChar(end);
	if (line < 0) return Util::emptyStringT;
	int nextStart = line + 1 >= GetLineCount() ? GetTextLength() : LineIndex(line + 1);
	if (end >= nextStart) return Util::emptyStringT;
	int hiddenLen = nextStart - end;
	tstring lineBuf;
	lineBuf.resize(hiddenLen + 1);
	GetTextRange(end, nextStart, &lineBuf[0]);
	auto pos = lineBuf.find(HIDDEN_TEXT_SEP, 1);
	if (pos == tstring::npos) return Util::emptyStringT;
	return lineBuf.substr(1, pos - 1);
}

tstring ChatCtrl::getUrl(LONG start, LONG end, bool keepSelected)
{
	tstring text;
	if (end > start)
	{
		text.resize(end - start + 1);
		SetSel(start, end);
		GetSelText(&text[0]);
		if (!text.empty())
			text.resize(text.length()-1);
		auto pos1 = text.find(HIDDEN_TEXT_SEP);
		if (pos1 != tstring::npos)
		{
			auto pos2 = text.find(HIDDEN_TEXT_SEP, pos1 + 1);
			if (pos2 != tstring::npos)
				text = text.substr(pos1 + 1, pos2 - (pos1 + 1));
		}
		else
		{
			tstring hiddenText = getUrlHiddenText(end);
			if (!hiddenText.empty())
				text = std::move(hiddenText);
		}
		if (!keepSelected)
			SetSel(end, end);
	}
	return text;
}

tstring ChatCtrl::getUrl(const ENLINK* el, bool keepSelected)
{
	return getUrl(el->chrg.cpMin, el->chrg.cpMax, keepSelected);
}

LRESULT ChatCtrl::onEnLink(int /*idCtrl*/, LPNMHDR pnmh, BOOL& /*bHandled*/)
{
	ENLINK* pEL = (ENLINK*)pnmh;
	if (pEL->msg == WM_LBUTTONUP || pEL->msg == WM_RBUTTONUP)
	{
		if (pEL->msg == WM_LBUTTONUP)
		{
			if (!(pEL->chrg.cpMin == ignoreLinkStart && pEL->chrg.cpMax == ignoreLinkEnd))
			{
				g_sSelectedURL = getUrl(pEL, false);
				dcassert(!g_sSelectedURL.empty());
				GetParent().PostMessage(WMU_CHAT_LINK_CLICKED, 0, 0);
			}
			ignoreLinkStart = ignoreLinkEnd = 0;
		}
		else if (pEL->msg == WM_RBUTTONUP)
		{
			g_sSelectedURL = getUrl(pEL, true);
			InvalidateRect(NULL);
			ignoreLinkStart = ignoreLinkEnd = 0;
		}
	}
	else if (pEL->msg == WM_MOUSEMOVE)
	{
		ignoreLinkStart = pEL->chrg.cpMin;
		ignoreLinkEnd = pEL->chrg.cpMax;
	}
	else if (pEL->msg == WM_LBUTTONDOWN)
	{
		ignoreLinkStart = ignoreLinkEnd = 0;
	}
	return 0;
}

IRichEditOle* ChatCtrl::getRichEditOle()
{
	if (!pRichEditOle) initRichEditOle();
	return pRichEditOle;
}

#ifdef IRAINMAN_INCLUDE_SMILE
void ChatCtrl::replaceObjects(tstring& s, int startIndex) const
{
	if (!pRichEditOle) return;
	REOBJECT ro;
	tstring::size_type pos = 0;
	int delta = 0;
	for (;;)
	{
		auto nextPos = s.find(WCH_EMBEDDING, pos);
		if (nextPos == tstring::npos) break;
		int shift = 1;
		memset(&ro, 0, sizeof(ro));
		ro.cbStruct = sizeof(ro);
		ro.cp = startIndex + delta + nextPos;
		if (SUCCEEDED(pRichEditOle->GetObject(REO_IOB_USE_CP, &ro, REO_GETOBJ_POLEOBJ)))
		{
			IGDIImage* pImage = nullptr;
			if (SUCCEEDED(ro.poleobj->QueryInterface(IID_IGDIImage, (void**) &pImage)))
			{
				BSTR text;
				if (SUCCEEDED(pImage->get_Text(&text)))
				{
					int textLen = wcslen(text);
					s.replace(nextPos, 1, text, textLen);
					shift = textLen;
					delta += 1 - textLen;
					SysFreeString(text);
				}
				pImage->Release();
			}
			ro.poleobj->Release();
		}
		pos = nextPos + shift;
	}
}
#endif

LRESULT ChatCtrl::onCopyActualLine(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	if (selectedLine >= 0)
	{
		int startIndex = LineIndex(selectedLine);
		int len = LineLength(startIndex);
		if (len > 0)
		{
			tstring line;
			line.resize(len);
			len = GetLine(selectedLine, &line[0], len);
			if (len > 0)
			{
				line.resize(len);
#ifdef IRAINMAN_INCLUDE_SMILE
				replaceObjects(line, startIndex);
#endif
				// remove hidden text
				tstring::size_type pos = 0;
				while (pos < line.length())
				{
					auto start = line.find(HIDDEN_TEXT_SEP, pos);
					if (start == tstring::npos) break;
					auto end = line.find(HIDDEN_TEXT_SEP, start + 1);
					if (end == tstring::npos) break;
					line.erase(start, end - start + 1);
					pos = start;
				}
				WinUtil::setClipboard(line);
			}
		}
	}
	return 0;
}

LRESULT ChatCtrl::onCopyURL(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	if (!g_sSelectedURL.empty())
	{
		WinUtil::setClipboard(g_sSelectedURL);
	}
	return 0;
}

#ifdef IRAINMAN_ENABLE_WHOIS
LRESULT ChatCtrl::onWhoisIP(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	if (!g_sSelectedIP.empty())
		WinUtil::processWhoisMenu(wID, g_sSelectedIP);
	return 0;
}

LRESULT ChatCtrl::onWhoisURL(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	if (!g_sSelectedURL.empty())
	{
		uint16_t port;
		string proto, host, file, query, fragment;
		Util::decodeUrl(Text::fromT(g_sSelectedURL), proto, host, port, file, query, fragment);
		if (!host.empty())
			WinUtil::openLink(_T("http://bgp.he.net/dns/") + Text::toT(host) + _T("#_website"));
	}
	return 0;
}

LRESULT ChatCtrl::onDumpUserInfo(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	IpAddress ip;
	if (!g_sSelectedIP.empty() && Util::parseIpAddress(ip, Text::fromT(g_sSelectedIP)))
	{
		string report = ip.type == AF_INET6 ? "IPv6 Info: " : "IPv4 Info: ";
		report += Identity::formatIpString(ip);
		ClientManager::LockInstanceClients l;
		const auto& clients = l.getData();
		auto i = clients.find(hubHint);
		if (i != clients.end())
			i->second->dumpUserInfo(report);
	}
	return 0;
}
#endif

LRESULT ChatCtrl::onEditCopy(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
#if 1
	if (!pRichEditOle)
	{
		initRichEditOle();
		if (pRichEditOle) return 0;
	}
	LONG start, end;
	GetSel(start, end);
	ITextDocument* pTextDoc = nullptr;
	if (SUCCEEDED(pRichEditOle->QueryInterface(IID_ITextDocument, (void**) &pTextDoc)))
	{
		ITextRange* pRange = nullptr;
		if (SUCCEEDED(pTextDoc->Range(start, end, &pRange)))
		{
			BSTR text;
			if (SUCCEEDED(pRange->GetText(&text)))
			{
				tstring s = text;
				SysFreeString(text);
#ifdef IRAINMAN_INCLUDE_SMILE
				replaceObjects(s, start);
#endif
				WinUtil::setClipboard(s);
			}
			pRange->Release();
		}
		pTextDoc->Release();
	}
#else
	Copy();
#endif
	return 0;
}

LRESULT ChatCtrl::onEditSelectAll(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	SetSelAll();
	return 0;
}

LRESULT ChatCtrl::onEditClearAll(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	Clear();
	return 0;
}

void ChatCtrl::setHubParam(const string& url, const string& nick)
{
	myNick = Text::toT(nick);
	hubHint = url;
}

void ChatCtrl::SetTextStyleMyNick(const CHARFORMAT2& ts)
{
	Colors::g_TextStyleMyNick = ts;
}

void ChatCtrl::Clear()
{
	SetWindowText(Util::emptyStringT.c_str());
}

void ChatCtrl::initRichEditOle()
{
	pRichEditOle = GetOleInterface();
	dcassert(pRichEditOle);
#ifdef IRAINMAN_INCLUDE_SMILE
	SetOleCallback(this);
#endif
}

#ifdef IRAINMAN_INCLUDE_SMILE
HRESULT STDMETHODCALLTYPE ChatCtrl::QueryInterface(THIS_ REFIID riid, LPVOID FAR * lplpObj)
{
	HRESULT res = E_NOINTERFACE;
	
	if (riid == IID_IRichEditOleCallback)
	{
		*lplpObj = this;
		res = S_OK;
	}
	
	return res;
}

COM_DECLSPEC_NOTHROW ULONG STDMETHODCALLTYPE ChatCtrl::AddRef(THIS)
{
	return InterlockedIncrement(&refs);
}

COM_DECLSPEC_NOTHROW ULONG STDMETHODCALLTYPE ChatCtrl::Release(THIS)
{
	return InterlockedDecrement(&refs);
}

// *** IRichEditOleCallback methods ***
COM_DECLSPEC_NOTHROW HRESULT STDMETHODCALLTYPE ChatCtrl::GetNewStorage(THIS_ LPSTORAGE FAR * lplpstg)
{
	return S_OK;
}

COM_DECLSPEC_NOTHROW HRESULT STDMETHODCALLTYPE ChatCtrl::GetInPlaceContext(THIS_ LPOLEINPLACEFRAME FAR * lplpFrame,
                                                                           LPOLEINPLACEUIWINDOW FAR * lplpDoc,
                                                                           LPOLEINPLACEFRAMEINFO lpFrameInfo)
{
	if (lplpFrame)
		*lplpFrame = nullptr;
	if (lplpDoc)
		*lplpDoc = nullptr;
	return S_OK;
}

COM_DECLSPEC_NOTHROW HRESULT STDMETHODCALLTYPE ChatCtrl::ShowContainerUI(THIS_ BOOL fShow)
{
	return S_OK;
}

COM_DECLSPEC_NOTHROW HRESULT STDMETHODCALLTYPE ChatCtrl::QueryInsertObject(THIS_ LPCLSID lpclsid, LPSTORAGE lpstg,
                                                                           LONG cp)
{
	return S_OK;
}

COM_DECLSPEC_NOTHROW HRESULT STDMETHODCALLTYPE ChatCtrl::DeleteObject(THIS_ LPOLEOBJECT lpoleobj)
{
	IGDIImageDeleteNotify *pDeleteNotify;
	if (lpoleobj->QueryInterface(IID_IGDIImageDeleteNotify, (void**)&pDeleteNotify) == S_OK && pDeleteNotify)
	{
		pDeleteNotify->SetDelete();
		pDeleteNotify->Release();
	}
	
	return S_OK;
}

COM_DECLSPEC_NOTHROW HRESULT STDMETHODCALLTYPE ChatCtrl::QueryAcceptData(THIS_ LPDATAOBJECT lpdataobj,
                                                                         CLIPFORMAT FAR * lpcfFormat, DWORD reco,
                                                                         BOOL fReally, HGLOBAL hMetaPict)
{
	return S_OK;
}

COM_DECLSPEC_NOTHROW HRESULT STDMETHODCALLTYPE ChatCtrl::ContextSensitiveHelp(THIS_ BOOL fEnterMode)
{
	return S_OK;
}

COM_DECLSPEC_NOTHROW HRESULT STDMETHODCALLTYPE ChatCtrl::GetClipboardData(THIS_ CHARRANGE FAR * lpchrg, DWORD reco,
                                                                          LPDATAOBJECT FAR * lplpdataobj)
{
	return E_NOTIMPL;
}

COM_DECLSPEC_NOTHROW HRESULT STDMETHODCALLTYPE ChatCtrl::GetDragDropEffect(THIS_ BOOL fDrag, DWORD grfKeyState,
                                                                           LPDWORD pdwEffect)
{
	return S_OK;
}

COM_DECLSPEC_NOTHROW HRESULT STDMETHODCALLTYPE ChatCtrl::GetContextMenu(THIS_ WORD seltype, LPOLEOBJECT lpoleobj,
                                                                        CHARRANGE FAR * lpchrg,
                                                                        HMENU FAR * lphmenu)
{
	// Forward message to host window
	::DefWindowProc(m_hWnd, WM_CONTEXTMENU, (WPARAM)m_hWnd, GetMessagePos());
	return S_OK;
}
#endif // IRAINMAN_INCLUDE_SMILE
