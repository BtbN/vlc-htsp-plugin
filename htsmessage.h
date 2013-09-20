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
#include <memory>
#include <list>

class HtsMap;
class HtsList;
class HtsInt;
class HtsStr;
class HtsBin;
class HtsMessage;

extern const std::string emptyString;

class HtsData
{
    public:
    HtsData() {}
    virtual ~HtsData() {}

    virtual uint32_t getU32() { return 0; }
    virtual int64_t getS64() { return 0; }
    virtual const std::string &getStr() { return emptyString; }
    virtual void getBin(uint32_t *len, void **buf) const { *len = 0; *buf = 0; }

    virtual uint32_t calcSize() { printf("WARNING!\n"); return 0; }
    virtual void Serialize(void *) { printf("WARNING!\n"); }

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
	using HtsData::getU32;
    uint32_t getU32(const std::string &name);
	using HtsData::getS64;
    int64_t getS64(const std::string &name);
	using HtsData::getStr;
    const std::string &getStr(const std::string &name);
	using HtsData::getBin;
    void getBin(const std::string &name, uint32_t *len, void **buf);
    std::shared_ptr<HtsList> getList(const std::string &name);
    std::shared_ptr<HtsMap> getMap(const std::string &name);

    std::unordered_map<std::string, std::shared_ptr<HtsData>> getRawData() { return data; }
    std::shared_ptr<HtsData> getData(const std::string &name);
    void setData(const std::string &name, std::shared_ptr<HtsData> newData);
    void setData(const std::string &name, uint32_t newData);
    void setData(const std::string &name, int32_t newData);
    void setData(const std::string &name, uint64_t newData);
    void setData(const std::string &name, int64_t newData);
    void setData(const std::string &name, const std::string &newData);

    virtual uint32_t calcSize();
    virtual void Serialize(void *buf);

    virtual bool isMap() { return true; }
    virtual bool isValid() { return true; }
    virtual unsigned char getType() { return 1; }

    private:
    uint32_t pCalcSize();
    std::unordered_map<std::string, std::shared_ptr<HtsData>> data;
};

 class HtsList : public HtsData
{
    public:
    HtsList() {}
    HtsList(uint32_t length, void *buf);

    uint32_t count();
    std::shared_ptr<HtsData> getData(uint32_t n);
    void appendData(std::shared_ptr<HtsData> newData);

    virtual uint32_t calcSize();
    virtual void Serialize(void *buf);

    virtual bool isList() { return true; }
    virtual bool isValid() { return true; }
    virtual unsigned char getType() { return 5; }

    private:
    uint32_t pCalcSize();
    std::vector<std::shared_ptr<HtsData>> data;
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

    virtual uint32_t getU32() { return (uint32_t)data; }
    virtual int64_t getS64() { return data; }

    virtual uint32_t calcSize();
    virtual void Serialize(void *buf);

    virtual bool isInt() { return true; }
    virtual bool isValid() { return true; }
    virtual unsigned char getType() { return 2; }

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

    virtual const std::string &getStr() { return data; }

    virtual uint32_t calcSize();
    virtual void Serialize(void *buf);

    virtual bool isStr() { return true; }
    virtual bool isValid() { return true; }
    virtual unsigned char getType() { return 3; }

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

    virtual void getBin(uint32_t *len, void **buf) const;
    virtual void setBin(uint32_t len, void *buf);

    virtual uint32_t calcSize();
    virtual void Serialize(void *buf);

    virtual bool isBin() { return true; }
    virtual bool isValid() { return true; }
    virtual unsigned char getType() { return 4; }

    private:
    uint32_t data_length;
    void *data_buf;
};

class HtsMessage
{
    public:
    HtsMessage():valid(false) {}

    static HtsMessage Deserialize(uint32_t length, void *buf);
    bool Serialize(uint32_t *length, void **buf);

    std::shared_ptr<HtsMap> getRoot() { return root; }
    void setRoot(std::shared_ptr<HtsMap> newRoot) { root = newRoot; valid = true; }
    bool isValid() { return valid; }

    private:
    bool valid;
    std::shared_ptr<HtsMap> root;
};

#endif
