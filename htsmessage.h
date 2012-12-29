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

#ifndef H__HTSMESSAGEPP__H__
#define H__HTSMESSAGEPP__H__

#include <unordered_map>
#include <cstdint>
#include <string>
#include <vector>
#include <list>

class HtsMap;
class HtsList;
class HtsInt;
class HtsStr;
class HtsBin;
class HtsMessage;

class HtsData
{
	public:
	HtsData() {}
	virtual ~HtsData() {}

	virtual uint32_t getU32() { return 0; }
	virtual int64_t getS64() { return 0; }
	virtual std::string getStr() { return std::string(); }
	virtual void getBin(uint32_t *len, void **buf) { *len = 0; *buf = 0; }

	virtual uint32_t calcSize() { return 0; }
	virtual void Serialize(void *) {}
	
	virtual bool isMap() { return false; }
	virtual bool isList() { return false; }
	virtual bool isInt() { return false; }
	virtual bool isStr() { return false; }
	virtual bool isBin() { return false; }
	virtual unsigned char getType() { return 0; }

	virtual bool isValid() { return false; }
	
	std::string getName() const { return name; }
	void setName(const std::string &newName) { name = newName; }
	
	private:
	std::string name;
};

class HtsMap : public HtsData
{
	public:
	HtsMap() {}
	HtsMap(uint32_t length, void *buf);

	HtsMessage makeMsg();

	bool contains(const std::string &name);
	uint32_t getU32(const std::string &name);
	int64_t getS64(const std::string &name);
	std::string getStr(const std::string &name);
	void getBin(const std::string &name, uint32_t *len, void **buf);
	HtsList getList(const std::string &name);

	std::unordered_map<std::string, HtsData> getRawData() { return data; }
	HtsData getData(const std::string &name);
	void setData(const std::string &name, HtsData newData);

	uint32_t calcSize();
	void Serialize(void *buf);
	
	bool isMap() { return true; }
	bool isValid() { return true; }
	unsigned char getType() { return 1; }

	private:
	uint32_t pCalcSize();
	std::unordered_map<std::string, HtsData> data;
};

 class HtsList : public HtsData
{
	public:
	HtsList() {}
	HtsList(uint32_t length, void *buf);

	uint32_t count();
	HtsData getData(uint32_t n);
	void appendData(HtsData newData);

	uint32_t calcSize();
	void Serialize(void *buf);

	bool isList() { return true; }
	bool isValid() { return true; }
	unsigned char getType() { return 5; }

	private:
	uint32_t pCalcSize();
	std::vector<HtsData> data;
};

class HtsInt : public HtsData
{
	public:
	HtsInt():data(0) {}
	HtsInt(uint32_t length, void *buf);
	HtsInt(uint32_t data):data(data) {}
	HtsInt(int32_t data):data(data) {}
	HtsInt(uint64_t data) { data = (int64_t)data; }
	HtsInt(int64_t data):data(data) {}

	uint32_t getU32() { return (uint32_t)data; }
	int64_t getS64() { return data; }

	uint32_t calcSize();
	void Serialize(void *buf);

	bool isInt() { return true; }
	bool isValid() { return true; }
	unsigned char getType() { return 2; }

	private:
	uint32_t pCalcSize();
	int64_t data;
};

class HtsStr : public HtsData
{
	public:
	HtsStr() {}
	HtsStr(uint32_t length, void *buf);
	HtsStr(const std::string &str):data(str) {}

	std::string getStr() { return data; }

	uint32_t calcSize();
	void Serialize(void *buf);

	virtual bool isStr() { return true; }
	bool isValid() { return true; }
	unsigned char getType() { return 3; }

	private:
	std::string data;
};

class HtsBin : public HtsData
{
	public:
	HtsBin(const HtsBin &other);
	HtsBin():data_length(0),data_buf(0) {}
	HtsBin(uint32_t length, void *buf);
	~HtsBin();

	void getBin(uint32_t *len, void **buf) const;
	void setBin(uint32_t len, void *buf);

	uint32_t calcSize();
	void Serialize(void *buf);

	bool isBin() { return true; }
	bool isValid() { return true; }
	unsigned char getType() { return 4; }

	private:
	uint32_t data_length;
	void *data_buf;
};

class HtsMessage
{
	public:
	HtsMessage():valid(false) {}

	static HtsMessage Deserialize(uint32_t length, void *buf);
	bool Serialize(uint32_t *length, void **buf) const;

	HtsMap getRoot() const { return root; }
	void setRoot(HtsMap newRoot) { root = newRoot; valid = true; }
	bool isValid() { return valid; }

	private:
	bool valid;
	HtsMap root;
};

#endif
