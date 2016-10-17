#ifndef RM_TYPE_H
#define RM_TYPE_H
#include "byte.h"
#include <cstring>
#include <cstdio>

class RM_Type
{
public:
    enum SizeType
    {
        sta = 0,
        var
    } sizeType;
    bool null;
    virtual Byte toByte() = 0;
    virtual void fromByte(Byte byte) = 0;
    virtual int getSize() = 0;
    virtual void print() = 0;
    RM_Type(SizeType _sizeType, bool _null)
        : sizeType(_sizeType), null(_null)
    {
    }
};

class RM_Type_int : public RM_Type
{
private:
    int value;
public:
    RM_Type_int(bool _null = true, int t = 0)
        : RM_Type(RM_Type::sta, _null), value(t)
    {
    }
    int getSize()
    {
        return sizeof(int) / sizeof(uch);
    }

    Byte toByte()
    {
        if (null) value = 0;

        return Byte(sizeof(int) / sizeof(uch), (uch *)&value);
    }
    void fromByte(Byte byte)
    {
        value = *(int *)byte.a;
    }
    void print()
    {
        if (null)printf("%d ", 0);
        else printf("%d ", value);
    }

    int getValue()
    {
        return value;
    }
    void setValue(int t)
    {
        value = t;
    }
};

template<int size = 64>
class RM_Type_varchar : public RM_Type
{
private:
    char *str;
    int length;
public:
    RM_Type_varchar(bool _null = true, const char *_str = "", int _length = 0)
        : RM_Type(RM_Type::var, _null)
    {
        str = new char[size + 1];
        memset(str, 0, sizeof(char)*size);
        length = _length;
        memcpy(str, _str, length * sizeof(char));
        str[length] = '\000';
    }
    ~RM_Type_varchar()
    {
        if (str != NULL)delete [] str;
    }

    int getSize()
    {
        return null ? 0 : length;
    }

    Byte toByte()
    {
        return Byte(null ? 0 : length, (uch *)str);
    }
    void fromByte(Byte byte)
    {
        memset(str, 0, sizeof(char)*size);
        const char *_str = (char *)byte.a;
        length = byte.length;
        memcpy(str, _str, length * sizeof(char));
        str[length] = '\000';
    }
    void print()
    {
        if (!null)printf("%s ", str);
        else printf(" ");
    }
    const char *getStr()
    {
        return str;
    }
    void setStr(const char *_str, int _length)
    {
        memset(str, 0, sizeof(char)*size);
        length = _length;
        memcpy(str, _str, length * sizeof(char));
        str[length] = '\000';
    }
};

#endif
