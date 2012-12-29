/*****************************************************************************
 * Copyright (C) 2012
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#include "htsmessage.h"

HtsMap::HtsMap(uint32_t length, void *buf)
{
}

HtsMessage HtsMap::makeMsg()
{
	HtsMessage res;
	res.setRoot(*this);
	return res;
}

bool HtsMap::contains(const std::string &name)
{
}

uint32_t HtsMap::getU32(const std::string &name)
{
}

int64_t HtsMap::getS64(const std::string &name)
{
}

std::string HtsMap::getStr(const std::string &name)
{
}

void HtsMap::getBin(const std::string &name, uint32_t *len, void **buf)
{
}

HtsList HtsMap::getList(const std::string &name)
{
}

HtsData HtsMap::getData(const std::string &name)
{
}

void HtsMap::setData(const std::string &name, HtsData newData)
{
}


HtsList::HtsList(uint32_t length, void *buf)
{
}

uint32_t HtsList::count()
{
}

HtsData HtsList::getData(uint32_t n)
{
}

void HtsList::appendData(HtsData newData)
{
}


HtsInt::HtsInt(uint32_t length, void *buf)
{
}


HtsStr::HtsStr(uint32_t length, void *buf)
{
}


HtsBin::HtsBin(uint32_t length, void *buf)
{
}

void HtsBin::getBin(uint32_t *len, void **buf)
{
}

HtsBin::~HtsBin()
{
}


HtsMessage HtsMessage::Deserialize(uint32_t length, void *buf)
{
}

bool HtsMessage::Serialize(uint32_t *length, void **buf) const
{
}
