/**
 * vim: set ts=4 :
 * =============================================================================
 * SendVar Proxy Manager
 * Copyright (C) 2011 Afronanny.  All rights reserved.
 * =============================================================================
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 3.0, as published by the
 * Free Software Foundation.
 * 
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * As a special exception, AlliedModders LLC gives you permission to link the
 * code of this program (as well as its derivative works) to "Half-Life 2," the
 * "Source Engine," the "SourcePawn JIT," and any Game MODs that run on software
 * by the Valve Corporation.  You must obey the GNU General Public License in
 * all respects for all other code used.  Additionally, AlliedModders LLC grants
 * this exception to all derivative works.  AlliedModders LLC defines further
 * exceptions, found in LICENSE.txt (as of this writing, version JULY-31-2007),
 * or <http://www.sourcemod.net/license.php>.
 *
 * Version: $Id$
 */

// taken straight from the SourceMod Updater
#include "gamedataupdater.h"

Downloader::Downloader() : buffer(NULL), size(0), pos(0)
{
}

Downloader::~Downloader()
{
	free(buffer);
}

DownloadWriteStatus Downloader::OnDownloadWrite(IWebTransfer *session, void* userdata, void* ptr, size_t size, size_t nmemb)
{
	size_t total = size * nmemb;
	if (pos + total > size)
	{
		size_t tmp = (pos + total) - size;
		size += tmp + (tmp / 2);
		buffer = (char *)realloc(buffer, size);
	}

	memcpy(&buffer[pos], ptr, total);
	pos += total;

	return DownloadWrite_Okay;
}
