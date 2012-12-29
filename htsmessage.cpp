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

#define __STDC_CONSTANT_MACROS 1

#include <vlc_common.h>
#include <vlc_plugin.h>
#include <vlc_network.h>

#include "htsmessage.h"

enum
{
	O32_LITTLE_ENDIAN = 0x03020100ul,
	O32_BIG_ENDIAN = 0x00010203ul,
	O32_PDP_ENDIAN = 0x01000302ul
};
static const union { unsigned char bytes[4]; uint32_t value; } o32_host_order = { { 0, 1, 2, 3 } };

int64_t endian64(int64_t v)
{
	if(o32_host_order.value == O32_BIG_ENDIAN)
		return v;

	int64_t res = 0;
	
	char *vp = (char*)&v;
	char *rp = (char*)&res;
	
	rp[0] = vp[7];
	rp[1] = vp[6];
	rp[2] = vp[5];
	rp[3] = vp[4];
	rp[4] = vp[3];
	rp[5] = vp[2];
	rp[6] = vp[1];
	rp[7] = vp[0];
	
	return res;
}

HtsMap::HtsMap(uint32_t /*length*/, void *buf)
{
	char *tmpbuf = (char*)buf;
	
	if(tmpbuf[0] != getType())
		return;
	tmpbuf += 1;
	
	unsigned char nlen = (unsigned char)tmpbuf[0];
	tmpbuf += 1;
	
	int64_t mlen = ntohl(*((uint32_t*)tmpbuf));
	tmpbuf += sizeof(uint32_t);
	
	if(nlen > 0)
	{
		setName(std::string(tmpbuf, (size_t)nlen));
		tmpbuf += nlen;
	}
	
	while(mlen > 0)
	{
		unsigned char mtype = (unsigned char)tmpbuf[0];
		unsigned char subNameLen = (unsigned char)tmpbuf[1];
		uint32_t subLen = ntohl(*((uint32_t*)(tmpbuf+2)));
		
		uint32_t psize = 1+1+4;
		psize += subNameLen;
		psize += subLen;
		
		std::shared_ptr<HtsData> newData;
		switch(mtype)
		{
			case 1:
				newData = std::make_shared<HtsMap>(psize, tmpbuf);
				break;
			case 2:
				newData = std::make_shared<HtsInt>(psize, tmpbuf);
				break;
			case 3:
				newData = std::make_shared<HtsStr>(psize, tmpbuf);
				break;
			case 4:
				newData = std::make_shared<HtsBin>(psize, tmpbuf);
				break;
			case 5:
				newData = std::make_shared<HtsList>(psize, tmpbuf);
				break;
		}
		
		setData(newData->getName(), newData);
		
		tmpbuf += psize;
		mlen -= psize;
	}
}

HtsMessage HtsMap::makeMsg()
{
	HtsMessage res;
	res.setRoot(*this);
	return res;
}

bool HtsMap::contains(const std::string &name)
{
	return data.count(name) > 0;
}

uint32_t HtsMap::getU32(const std::string &name)
{
	return getData(name)->getU32();
}

int64_t HtsMap::getS64(const std::string &name)
{
	return getData(name)->getS64();
}

std::string HtsMap::getStr(const std::string &name)
{
	return getData(name)->getStr();
}

void HtsMap::getBin(const std::string &name, uint32_t *len, void **buf)
{
	getData(name)->getBin(len, buf);
}

HtsList HtsMap::getList(const std::string &name)
{
	std::shared_ptr<HtsData> dat = getData(name);
	if(!dat->isList())
		return HtsList();
	return *std::static_pointer_cast<HtsList>(dat);
}

std::shared_ptr<HtsData> HtsMap::getData(const std::string &name)
{
	if(!contains(name))
		return std::make_shared<HtsData>();
	return data.at(name);
}

void HtsMap::setData(const std::string &name, std::shared_ptr<HtsData> newData)
{
	newData->setName(name);
	data[name] = newData;
}


HtsList::HtsList(uint32_t /*length*/, void *buf)
{
	char *tmpbuf = (char*)buf;
	
	if(tmpbuf[0] != getType())
		return;
	tmpbuf += 1;
	
	unsigned char nlen = (unsigned char)tmpbuf[0];
	tmpbuf += 1;
	
	int64_t mlen = ntohl(*((uint32_t*)tmpbuf));
	tmpbuf += sizeof(uint32_t);

	if(nlen > 0)
	{
		setName(std::string(tmpbuf, (size_t)nlen));
		tmpbuf += nlen;
	}
	
	while(mlen > 0)
	{
		unsigned char mtype = (unsigned char)tmpbuf[0];
		unsigned char subNameLen = (unsigned char)tmpbuf[1];
		uint32_t subLen = ntohl(*((uint32_t*)(tmpbuf+2)));
		
		uint32_t psize = 1+1+4;
		psize += subNameLen;
		psize += subLen;
		
		std::shared_ptr<HtsData> newData;
		switch(mtype)
		{
			case 1:
				newData = std::make_shared<HtsMap>(psize, tmpbuf);
				break;
			case 2:
				newData = std::make_shared<HtsInt>(psize, tmpbuf);
				break;
			case 3:
				newData = std::make_shared<HtsStr>(psize, tmpbuf);
				break;
			case 4:
				newData = std::make_shared<HtsBin>(psize, tmpbuf);
				break;
			case 5:
				newData = std::make_shared<HtsList>(psize, tmpbuf);
				break;
		}

		appendData(newData);
		
		tmpbuf += psize;
		mlen -= psize;
	}
}

uint32_t HtsList::count()
{
	return data.size();
}

std::shared_ptr<HtsData> HtsList::getData(uint32_t n)
{
	if(n >= data.size())
		return std::make_shared<HtsData>();
	return data.at(n);
}

void HtsList::appendData(std::shared_ptr<HtsData> newData)
{
	newData->setName("");
	data.push_back(newData);
}


HtsInt::HtsInt(uint32_t /*length*/, void *buf)
{
	data = 0;
	char *tmpbuf = (char*)buf;
	
	if(tmpbuf[0] != getType())
		return;
	
	unsigned char nlen = (unsigned char)tmpbuf[1];
	uint32_t len = ntohl(*((uint32_t*)(tmpbuf+2)));
	if(len > 8)
		len = 8;
	
	tmpbuf += 6;
	
	if(nlen > 0)
	{
		setName(std::string(tmpbuf, (size_t)nlen));
		tmpbuf += nlen;
	}
	
	char *datap = (char*)&data;
	
	for(uint32_t i = len; i > 0; i--)
		datap[len-i] = tmpbuf[len-i]; //TODO: Find out correct order
}


HtsStr::HtsStr(uint32_t /*length*/, void *buf)
{
	char *tmpbuf = (char*)buf;

	if(tmpbuf[0] != getType())
		return;
	
	unsigned char nlen = (unsigned char)tmpbuf[1];
	uint32_t len = ntohl(*((uint32_t*)(tmpbuf+2)));
	
	tmpbuf += 6;
	
	if(nlen > 0)
	{
		setName(std::string(tmpbuf, (size_t)nlen));
		tmpbuf += nlen;
	}
	
	data = std::string(tmpbuf, (size_t)len);
}


HtsBin::HtsBin(const HtsBin &other)
{
	other.getBin(&data_length, &data_buf);
}

HtsBin::HtsBin(uint32_t /*length*/, void *buf)
{
	char *tmpbuf = (char*)buf;

	if(tmpbuf[0] != getType())
		return;
	
	unsigned char nlen = (unsigned char)tmpbuf[1];
	data_length = ntohl(*((uint32_t*)(tmpbuf+2)));

	tmpbuf += 6;
	
	if(nlen > 0)
	{
		setName(std::string(tmpbuf, (size_t)nlen));
		tmpbuf += nlen;
	}
	
	data_buf = malloc(data_length);
	memcpy(data_buf, tmpbuf, data_length);
}

void HtsBin::getBin(uint32_t *len, void **buf) const
{
	*len = data_length;
	void *mem = malloc(data_length);
	memcpy(mem, data_buf, data_length);
	*buf = mem;
}

void HtsBin::setBin(uint32_t len, void *buf)
{
	if(data_buf)
		free(data_buf);

	data_length = len;
	data_buf = malloc(len);
	memcpy(data_buf, buf, len);
}

HtsBin::~HtsBin()
{
	if(data_buf)
		free(data_buf);
}


HtsMessage HtsMessage::Deserialize(uint32_t length, void *buf)
{
	char *tmpbuf = (char*)buf;

	HtsMap res;
	
	while(length > 5)
	{
		unsigned char mtype = (unsigned char)tmpbuf[0];
		unsigned char subNameLen = (unsigned char)tmpbuf[1];
		uint32_t subLen = ntohl(*((uint32_t*)(tmpbuf+2)));
		
		uint32_t psize = 6;
		psize += subNameLen;
		psize += subLen;
		
		std::shared_ptr<HtsData> newData;
		switch(mtype)
		{
			case 1:
				newData = std::make_shared<HtsMap>(psize, tmpbuf);
				break;
			case 2:
				newData = std::make_shared<HtsInt>(psize, tmpbuf);
				break;
			case 3:
				newData = std::make_shared<HtsStr>(psize, tmpbuf);
				break;
			case 4:
				newData = std::make_shared<HtsBin>(psize, tmpbuf);
				break;
			case 5:
				newData = std::make_shared<HtsList>(psize, tmpbuf);
				break;
		}
		
		res.setData(newData->getName(), newData);
		
		length -= psize;
		tmpbuf += psize;
	}
	
	return res.makeMsg();
}

bool HtsMessage::Serialize(uint32_t *length, void **buf)
{
	unsigned char *resBuf = 0;
	uint32_t resLength = 4;
	*length = 0;
	*buf = 0;

	HtsMap map = getRoot();
	auto umap = map.getRawData();
	for(auto it = umap.begin(); it != umap.end(); ++it)
		resLength += it->second->calcSize();
	
	resBuf = (unsigned char*)malloc(resLength);
	memset(resBuf, 0xFF, resLength);
	
	*((uint32_t*)resBuf) = htonl(resLength);

	char *tmpbuf = (char*)resBuf;
	tmpbuf += 4;

	for(auto it = umap.begin(); it != umap.end(); ++it)
	{
		it->second->Serialize(tmpbuf);
		tmpbuf += it->second->calcSize();
	}
	
	/*printf("Calculated total size %d\n", resLength + 4);
	for(uint32_t i = 0; i < resLength; i++)
	{
		char an = resBuf[i];
		if((an >= 'a' && an <= 'z') || (an >= 'A' && an <= 'Z'))
			an = resBuf[i];
		else
			an = '-';
		
		printf("0x%X(%c) ", resBuf[i], an);
	}
	printf("\n");*/

	*length = resLength+4;
	*buf = resBuf;
	return true;
}

uint32_t HtsMap::calcSize()
{
	return pCalcSize() + 6 + getName().length();
}

uint32_t HtsMap::pCalcSize()
{
	uint32_t totalSize = 0;
	for(auto it = data.begin(); it != data.end(); ++it)
	{
		totalSize += it->second->calcSize();
	}
	return totalSize;
}

void HtsMap::Serialize(void *buf)
{
	unsigned char *tmpbuf = (unsigned char*)buf;

	tmpbuf[0] = getType();
	tmpbuf[1] = getName().length();
	
	*((uint32_t*)(tmpbuf + 2)) = htonl(pCalcSize());
	tmpbuf += 6;
	
	if(getName().length() > 0)
	{
		memcpy(tmpbuf, getName().c_str(), getName().length());
		tmpbuf += getName().length();
	}
	
	for(auto it = data.begin(); it != data.end(); ++it)
	{
		std::shared_ptr<HtsData> dat = it->second;
		dat->Serialize(tmpbuf);
		tmpbuf += dat->calcSize();
	}
}

uint32_t HtsList::pCalcSize()
{
	uint32_t totalSize = 0;
	for(uint32_t i = 0; i < data.size(); i++)
	{
		totalSize += data.at(i)->calcSize();
	}
	return totalSize;
}

uint32_t HtsList::calcSize()
{
	return pCalcSize() + 6 + getName().length();
}

void HtsList::Serialize(void *buf)
{
	unsigned char *tmpbuf = (unsigned char*)buf;

	tmpbuf[0] = getType();
	tmpbuf[1] = getName().length();
	
	*((uint32_t*)(tmpbuf + 2)) = htonl(pCalcSize());
	tmpbuf += 6;
	
	if(getName().length() > 0)
	{
		memcpy(tmpbuf, getName().c_str(), getName().length());
		tmpbuf += getName().length();
	}
	
	for(uint32_t i = 0; i < data.size(); i++)
	{
		std::shared_ptr<HtsData> d = data.at(i);
		d->Serialize(tmpbuf);
		tmpbuf += d->calcSize();
	}
}

uint32_t HtsInt::calcSize()
{
	return pCalcSize() + getName().length() + 6;
}

uint32_t HtsInt::pCalcSize()
{
	if(data < 0)
		return 8;
	if(data <= 0xFF)
		return 1;
	if(data <= 0xFFFF)
		return 2;
	if(data <= 0xFFFFFF)
		return 3;
	if(data <= 0xFFFFFFFF)
		return 4;
	if(data <= 0xFFFFFFFFFF)
		return 5;
	if(data <= 0xFFFFFFFFFFFF)
		return 6;
	if(data <= 0xFFFFFFFFFFFFFF)
		return 7;
	return 8;
}

void HtsInt::Serialize(void *buf)
{
	uint32_t len = pCalcSize();
	char *tmpbuf = (char*)buf;
	
	tmpbuf[0] = getType();
	tmpbuf[1] = getName().length();
	
	*((uint32_t*)(tmpbuf + 2)) = htonl(len);
	tmpbuf += 6;
	
	if(getName().length() > 0)
	{
		memcpy(tmpbuf, getName().c_str(), getName().length());
		tmpbuf += getName().length();
	}
	
	int64_t tmp = data;
	char *datap = (char*)&tmp;
	
	for(uint32_t i = len; i > 0; i--)
	{
		tmpbuf[len-i] = datap[len-i]; //TODO: Find out correct order!
	}
}

uint32_t HtsStr::calcSize()
{
	return data.length() + getName().length() + 6;
}

void HtsStr::Serialize(void *buf)
{
	unsigned char *tmpbuf = (unsigned char*)buf;

	tmpbuf[0] = getType();
	tmpbuf[1] = getName().length();
	
	*((uint32_t*)(tmpbuf + 2)) = htonl(data.length());
	tmpbuf += 6;
	
	if(getName().length() > 0)
	{
		memcpy(tmpbuf, getName().c_str(), getName().length());
		tmpbuf += getName().length();
	}
	
	memcpy(tmpbuf, data.c_str(), data.length());
}

uint32_t HtsBin::calcSize()
{
	return data_length + getName().length() + 6;
}

void HtsBin::Serialize(void *buf)
{
	unsigned char *tmpbuf = (unsigned char*)buf;

	tmpbuf[0] = getType();
	tmpbuf[1] = getName().length();
	
	*((uint32_t*)(tmpbuf + 2)) = htonl(data_length);
	tmpbuf += 6;
	
	if(getName().length() > 0)
	{
		memcpy(tmpbuf, getName().c_str(), getName().length());
		tmpbuf += getName().length();
	}
	
	memcpy(tmpbuf, data_buf, data_length);
}
