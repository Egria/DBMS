#ifndef RM_FILEHANDLE_H
#define RM_FILEHANDLE_H
#include "rc.h"
#include <bufmanager/BufPageManager.h>
#include <fileio/FileManager.h>
#include "rm_record.h"
class RM_FileHandle
{
private:
    BufPageManager *bpm;
    int fileId;
    int findPage(int length)
    {
        int index;
        BufType b = bpm->getPage(fileId, 0, index);

        for (int i = 1; PAGE_INT_NUM; i++)
        {
            int t = b[i];
            int num = t & 0x0000ffff, uses = t >> 16;

            if (PAGE_SIZE - uses >= length + 4)return i;
        }

        return -1;
    }
public:
    RM_FileHandle(BufPageManager *_bpm = NULL)
    {
        bpm = _bpm;
        fileId = -1;
    }
    ~RM_FileHandle()
    {

    }
    void init(BufPageManager *_bpm, int _fileId)
    {
        bpm = _bpm;
        fileId = _fileId;
    }
    int getFileId()
    {
        return fileId;
    }


    RC InsertRec      (const RM_Record &rec, RID &rid)
    {
        Byte byte = rec.toByte();
        int index, index_zero, pageId = findPage(byte.length);

        if (pageId == -1)return Error;

        BufType b = bpm->getPage(fileId, pageId, index);
        uch *bc = (uch *)b;
        ush fp = *(ush *)(bc + PAGE_SIZE - 2);
        ush num = *(ush *)(bc + PAGE_SIZE - 4);

        for (int i = 1; i <= num; i++)
        {
            if (*(ush *)(bc + PAGE_SIZE - 4 * (i + 1)) == 0xffff)
            {
                if (byte.length + fp >= PAGE_SIZE - 4 * (num + 1))return Error;

                rid = RID(pageId, i);
                memcpy(bc + fp, byte.a, byte.length * sizeof(uch));
                *(ush *)(bc + PAGE_SIZE - 4 * (i + 1)) = fp;
                *(ush *)(bc + PAGE_SIZE - 4 * (i + 1) + 2) = byte.length;
                fp += byte.length;
                *(ush *)(bc + PAGE_SIZE - 2) = fp;
                bpm->markDirty(index);
                BufType zero = bpm->getPage(fileId, 0, index_zero);
                zero[pageId] = (zero[pageId] & 0xffff0000) | num;
                zero[pageId] = (zero[pageId] & 0x0000ffff) | ((4 * (num + 2) + fp) << 16);
                bpm->markDirty(index_zero);
                return Success;
            }
        }

        rid = RID(pageId, ++num);

        if (byte.length + fp >= PAGE_SIZE - 4 * (num + 1))return Error;

        memcpy(bc + fp, byte.a, byte.length * sizeof(uch));
        *(ush *)(bc + PAGE_SIZE - 4 * (num + 1)) = fp;
        *(ush *)(bc + PAGE_SIZE - 4 * (num + 1) + 2) = byte.length;
        fp += byte.length;
        *(ush *)(bc + PAGE_SIZE - 2) = fp;
        *(ush *)(bc + PAGE_SIZE - 4) = num;
        bpm->markDirty(index);
        BufType zero = bpm->getPage(fileId, 0, index_zero);
        zero[pageId] = (zero[pageId] & 0xffff0000) | num;
        zero[pageId] = (zero[pageId] & 0x0000ffff) | ((4 * (num + 2) + fp) << 16);
        bpm->markDirty(index_zero);
        return Success;
    }

    RC GetRec         (const RID &rid, RM_Record &rec) const
    {
        int index;
        BufType b = bpm->getPage(fileId, rid.pageId, index);
        uch *bc = (uch *)b;
        ush fp = *(ush *)(bc + PAGE_SIZE - 2);
        ush num = *(ush *)(bc + PAGE_SIZE - 4) + 1;

        if (rid.rowId > num || rid.rowId <= 0)return Error;

        ush offset = *(ush *)(bc + PAGE_SIZE - 4 * (rid.rowId + 1));
        ush length = *(ush *)(bc + PAGE_SIZE - 4 * (rid.rowId + 1) + 2);

        if (offset == 0xffff)return Error;

        Byte byte = Byte(length, bc + offset);
        rec.fromByte(byte);
        return Success;
    }
    RC DeleteRec      (const RID &rid)
    {
        int index, index_zero;
        BufType b = bpm->getPage(fileId, rid.pageId, index);
        uch *bc = (uch *)b;
        ush num = *(ush *)(bc + PAGE_SIZE - 4) + 1;
        ush fp = *(ush *)(bc + PAGE_SIZE - 2);

        if (rid.rowId > num || rid.rowId <= 0)return Error;

        ush offset = *(ush *)(bc + PAGE_SIZE - 4 * (rid.rowId + 1));
        ush length = *(ush *)(bc + PAGE_SIZE - 4 * (rid.rowId + 1) + 2);

        for (uch *i = bc + offset; i < bc + fp - length; i++)*i = *(i + length);

        *(ush *)(bc + PAGE_SIZE - 4 * (rid.rowId + 1)) = 0xffff;
        *(ush *)(bc + PAGE_SIZE - 4 * (rid.rowId + 1) + 2) = 0;

        for (int i = 1; i <= num; i++)
        {
            if (*(ush *)(bc + PAGE_SIZE - 4 * (i + 1)) >= offset + length && *(ush *)(bc + PAGE_SIZE - 4 * (i + 1)) != 0xffff)*(ush *)(bc + PAGE_SIZE - 4 * (i + 1)) -= length;
        }

        *(ush *)(bc + PAGE_SIZE - 2) = (fp -= length);
        bpm->markDirty(index);
        BufType zero = bpm->getPage(fileId, 0, index_zero);
        zero[rid.pageId] = (zero[rid.pageId] & 0x0000ffff) | ((4 * (num + 2) + fp) << 16);
        bpm->markDirty(index_zero);
        return Success;
    }
    std::vector<std::pair<RID, RM_Record> > ListRec()
    {
        int zero_index;
        RM_Record b;
        b.push_back(new RM_Type_int());
        b.push_back(new RM_Type_varchar<>());
        b.push_back(new RM_Type_int());
        b.push_back(new RM_Type_varchar<>());
        b.push_back(new RM_Type_int());
        b.push_back(new RM_Type_int());
        b.push_back(new RM_Type_varchar<>());
        b.push_back(new RM_Type_varchar<>());
        std::vector<std::pair<RID, RM_Record> > list;

        for (int i = 1; i < PAGE_INT_NUM; i++)
        {
            BufType bb = bpm->getPage(fileId, 0, zero_index);
            int num = bb[i] & 0x0000ffff;

            if (num == 0)continue;

            for (int j = 0; j < num; j++)
            {
                if (this->GetRec(RID(i, j), b) == Success)
                {
                    RM_Record b;
                    b.push_back(new RM_Type_int());
                    b.push_back(new RM_Type_varchar<>());
                    b.push_back(new RM_Type_int());
                    b.push_back(new RM_Type_varchar<>());
                    b.push_back(new RM_Type_int());
                    b.push_back(new RM_Type_int());
                    b.push_back(new RM_Type_varchar<>());
                    b.push_back(new RM_Type_varchar<>());
                    this->GetRec(RID(i, j), b);
                    list.push_back(make_pair(RID(i, j), b));
                }
            }
        }

        return list;
    }

    //RC UpdateRec      (const RM_Record &rec);              // Update a record
    //RC ForcePages     (PageNum pageNum = ALL_PAGES) const; // Write dirty page(s)
};

#endif
