#ifndef TM_MANAGER_H
#define TM_MANAGER_H
#include "rc.h"
#include <boost/filesystem.hpp>
#include <fstream>
#include <map>
#include "sql/statements.h"
#include "rm_filehandle.h"
#include "rm_manager.h"
#include "rm_record.h"
#include "ix_manager.h"

namespace bf = boost::filesystem;

class TM_Manager
{
private:
    RM_Manager *rmm;
    RM_FileHandle *rmfh;
    bf::path path;
    std::map<std::string, IX_Manager *> indexst;
    std::vector<IX_Manager *> indexv;
public:
    TM_Manager(FileManager *fm, BufPageManager *bpm, bf::path path)
        : path(path)
    {
        rmm = new RM_Manager(fm, bpm);
        rmfh = new RM_FileHandle(path);
        rmm->CreateFile((path / "data.db").string().c_str());
        rmm->OpenFile((path / "data.db").string().c_str(), rmfh);
        createIndex();
    }

    ~TM_Manager()
    {
        rmm->CloseFile(rmfh);
        delete rmfh;
        delete rmm;

        for (auto it : indexst)
            delete it.second;
    }

    RC createIndex(bool createNew = false)
    {
        indexv.clear();
        std::vector<std::pair<RID, RM_Record> > alldata;

        if (createNew)alldata = rmfh->ListRec();

        bf::path filename = path / configFile;
        std::ifstream fi(filename.string());
        int n;
        fi >> n;

        for (int i = 0; i < n; i++)
        {
            std::string name, type;
            bool notnull, index, primary;
            int len;
            getline(fi, name);

            if (name.empty())getline(fi, name);

            fi >> type >> len >> notnull >> index >> primary;
            Type *data;

            if (type == "INTEGER")
            {
                data = new Type_int(!notnull);
            }
            else if (type == "INT")
            {
                data = new Type_int(!notnull, 0, len);
            }
            else if (type == "CHAR" || type == "VARCHAR")
            {
                if (len <= 32)data = new Type_varchar<32>(!notnull);
                else if (len <= 64)data = new Type_varchar<64>(!notnull);
                else if (len <= 128)data = new Type_varchar<128>(!notnull);
                else if (len <= 256)data = new Type_varchar<256>(!notnull);
            }

            if (index && indexst.find(name) == indexst.end())
            {
                bf::path f1 = path / ("_" + name + ".db");
                bf::path f2 = path / ("_deque_" + name + ".db");
                IX_Manager *it = new IX_Manager(f1.c_str(), primary ? NULL : f2.c_str(), data);
                indexst.insert(make_pair(name, it));

                for (auto data : alldata)
                {
                    it->InsertEntry(data.second.get(i), data.first);
                }
            }

            auto it = indexst.find(name);
            indexv.push_back(it == indexst.end() ? NULL : it->second);
        }

        for (auto it : alldata)
        {
            it.second.clear();
        }

        return Success;
    }

    RC dropIndex()
    {
        indexv.clear();
        bf::path filename = path / configFile;
        std::ifstream fi(filename.string());
        int n;
        fi >> n;

        for (int i = 0; i < n; i++)
        {
            std::string name, type;
            bool notnull, index, primary;
            int len;
            getline(fi, name);

            if (name.empty())getline(fi, name);

            fi >> type >> len >> notnull >> index >> primary;

            auto it = indexst.find(name);

            if (!index && it != indexst.end())
            {
                delete it->second;
                bf::path f1 = path / ("_" + name + ".db");
                bf::path f2 = path / ("deque_" + name + ".db");
                bf::remove(f1);

                if (!primary)bf::remove(f2);

                indexst.erase(it);
            }

            it = indexst.find(name);
            indexv.push_back(it == indexst.end() ? NULL : it->second);
        }

        return Success;
    }

    std::map<string, int> makeHeadMap()
    {
        bf::path filename = path / configFile;
        std::ifstream fi(filename.string());
        int n;
        fi >> n;
        std::map<string, int>st;

        for (int i = 0; i < n; i++)
        {
            std::string name, type;
            bool notnull, index, primary;
            int len;
            getline(fi, name);

            if (name.empty())getline(fi, name);

            fi >> type >> len >> notnull >> index >> primary;
            st[name] = i;
        }

        return st;
    }

    RC insertRecord(std::vector<hsql::Expr *> values)
    {

        bf::path filename = path / configFile;
        std::ifstream fi(filename.string());
        int n;
        fi >> n;

        if (n != int(values.size()))
        {
            fprintf(stderr, "There are %d columns in this table but insert %d columns\n", n, int(values.size()));
            return Error;
        }

        RM_Record head;


        for (int i = 0; i < n; i++)
        {
            std::string name, type;
            bool notnull, index, primary;
            int len;
            getline(fi, name);

            if (name.empty())getline(fi, name);

            fi >> type >> len >> notnull >> index >> primary;
            Type *data;

            switch (values[i]->type)
            {
                case hsql::kExprLiteralInt:
                    if (type == "INTEGER" || type == "INT")
                    {
                        data = new Type_int(!notnull, values[i]->ival);
                    }
                    else
                    {
                        fprintf(stderr, "Values[%d] type is error.\n", i);
                        return Error;
                    }

                    break;

                case hsql::kExprLiteralString:
                    if (type == "CHAR" || type == "VARCHAR")
                    {
                        if (strlen(values[i]->name) > len)
                        {
                            fprintf(stderr, "Values[%d] '%s' is longer than %d.\n", i, values[i]->name, len);
                            return Error;
                        }

                        if (len <= 32)data = new Type_varchar<32>(!notnull, values[i]->name, strlen(values[i]->name));
                        else if (len <= 64)data = new Type_varchar<64>(!notnull, values[i]->name, strlen(values[i]->name));
                        else if (len <= 128)data = new Type_varchar<128>(!notnull, values[i]->name, strlen(values[i]->name));
                        else if (len <= 256)data = new Type_varchar<256>(!notnull, values[i]->name, strlen(values[i]->name));
                    }
                    else
                    {
                        fprintf(stderr, "Values[%d] type is error.\n", i);
                        return Error;
                    }

                    break;

                default:
                    fprintf(stderr, "Values[%d] type is error.\n", i);
                    return Error;
            }

            if (primary)
            {
                std::vector<RID> check = indexv[i]->SearchEntry(data);

                if (!check.empty())
                {
                    fprintf(stderr, "Values[%d] is a Primary Key and unique.\n", i);
                    return Error;
                }
            }

            head.push_back(data);
        }

        RID rid;
        RC result = rmfh->InsertRec(head, rid);

        for (int i = 0; i < n; i++)if (indexv[i])indexv[i]->InsertEntry(head.get(i), rid);

        head.clear();
        return result;

    }

    RC check(const hsql::Expr &expr, const std::map<string, int> &st, RM_Record &rec, bool &flag)
    {
        if (expr.type != hsql::kExprOperator)
        {
            fprintf(stderr, "Where Expr should be a bool expression.\n");
            return Error;
        }

        int tleft = 0;
        int ileft;
        const char *cleft;
        bool bleft;

        switch (expr.expr->type)
        {
            case hsql::kExprColumnRef:
            {
                auto it = st.find(std::string(expr.expr->name));

                if (it == st.end())
                {
                    fprintf(stderr, "Column %s is not found.\n", expr.expr->name);
                    return Error;
                }

                Type *t = rec.get(it->second);

                if (dynamic_cast<Type_int *>(t) != NULL)
                {
                    tleft = 0;
                    ileft = ((Type_int *)(t))->getValue();
                }
                else if (dynamic_cast<Type_varchar<32>*>(t) != NULL)
                {
                    tleft = 1;
                    cleft = ((Type_varchar<32> *)(t))->getStr();
                }
                else if (dynamic_cast<Type_varchar<64>*>(t) != NULL)
                {
                    tleft = 1;
                    cleft = ((Type_varchar<64> *)(t))->getStr();
                }
                else if (dynamic_cast<Type_varchar<128>*>(t) != NULL)
                {
                    tleft = 1;
                    cleft = ((Type_varchar<128> *)(t))->getStr();
                }
                else if (dynamic_cast<Type_varchar<256>*>(t) != NULL)
                {
                    tleft = 1;
                    cleft = ((Type_varchar<256> *)(t))->getStr();
                }
                else
                {
                    fprintf(stderr, "The Expr Column Type is error.\n");
                    return Error;
                }
            }
            break;

            case hsql::kExprLiteralInt:
                tleft = 0;
                ileft = expr.expr->ival;
                break;

            case hsql::kExprLiteralString:
                tleft = 1;
                cleft = expr.expr->name;
                break;

            case hsql::kExprOperator:
                tleft = 2;
                check(*expr.expr, st, rec, bleft);
                break;

            default:
                fprintf(stderr, "The Expr Literal Type is error.\n");
                return Error;
        }

        if (expr.op_type == hsql::Expr::NOT)
        {
            if (tleft == 2)
            {
                flag = !bleft;
                return Success;
            }
            else
            {
                fprintf(stderr, "The Expr Type is error.\n");
                return Error;
            }
        }

        int tright = 0;
        int iright;
        const char *cright;
        bool bright;

        switch (expr.expr2->type)
        {
            case hsql::kExprColumnRef:
            {
                auto it = st.find(std::string(expr.expr2->name));

                if (it == st.end())
                {
                    fprintf(stderr, "Column %s is not found.\n", expr.expr2->name);
                    return Error;
                }

                Type *t = rec.get(it->second);

                if (dynamic_cast<Type_int *>(t) != NULL)
                {
                    tright = 0;
                    iright = ((Type_int *)(t))->getValue();
                }
                else if (dynamic_cast<Type_varchar<32>*>(t) != NULL)
                {
                    tright = 1;
                    cright = ((Type_varchar<32> *)(t))->getStr();
                }
                else if (dynamic_cast<Type_varchar<64>*>(t) != NULL)
                {
                    tright = 1;
                    cright = ((Type_varchar<64> *)(t))->getStr();
                }
                else if (dynamic_cast<Type_varchar<128>*>(t) != NULL)
                {
                    tright = 1;
                    cright = ((Type_varchar<128> *)(t))->getStr();
                }
                else if (dynamic_cast<Type_varchar<256>*>(t) != NULL)
                {
                    tright = 1;
                    cright = ((Type_varchar<256> *)(t))->getStr();
                }
                else
                {
                    fprintf(stderr, "The Expr Column Type is error.\n");
                    return Error;
                }
            }
            break;

            case hsql::kExprLiteralInt:
                tright = 0;
                iright = expr.expr2->ival;
                break;

            case hsql::kExprLiteralString:
                tright = 1;
                cright = expr.expr2->name;
                break;

            case hsql::kExprOperator:
                tright = 2;
                check(*expr.expr2, st, rec, bright);
                break;

            default:
                fprintf(stderr, "The Expr Literal Type is error.\n");
                return Error;
        }

        if (tleft != tright)
        {
            fprintf(stderr, "The Expr Type is error.\n");
            return Error;
        }

        switch (expr.op_type)
        {
            case hsql::Expr::SIMPLE_OP:

                if (expr.op_char == '=')
                {
                    if (tleft == 0)
                        flag = (ileft == iright);
                    else if (tleft == 1)
                        flag = (strcmp(cleft, cright) == 0);
                    else
                    {
                        fprintf(stderr, "The Expr Type is error.\n");
                        return Error;
                    }
                }
                else if (expr.op_char == '<')
                {
                    if (tleft == 0)
                        flag = (ileft < iright);
                    else if (tleft == 1)
                        flag = (strcmp(cleft, cright) < 0);
                    else
                    {
                        fprintf(stderr, "The Expr Type is error.\n");
                        return Error;
                    }
                }
                else if (expr.op_char == '>')
                {
                    if (tleft == 0)
                        flag = (ileft > iright);
                    else if (tleft == 1)
                        flag = (strcmp(cleft, cright) > 0);
                    else
                    {
                        fprintf(stderr, "The Expr Type is error.\n");
                        return Error;
                    }
                }

                break;

            case hsql::Expr::NOT_EQUALS:
                if (tleft == 0)
                    flag = (ileft != iright);
                else if (tleft == 1)
                    flag = (strcmp(cleft, cright) != 0);
                else
                {
                    fprintf(stderr, "The Expr Type is error.\n");
                    return Error;
                }

                break;

            case hsql::Expr::LESS_EQ:
                if (tleft == 0)
                    flag = (ileft <= iright);
                else if (tleft == 1)
                    flag = (strcmp(cleft, cright) <= 0);
                else
                {
                    fprintf(stderr, "The Expr Type is error.\n");
                    return Error;
                }

                break;

            case hsql::Expr::GREATER_EQ:
                if (tleft == 0)
                    flag = (ileft >= iright);
                else if (tleft == 1)
                    flag = (strcmp(cleft, cright) >= 0);
                else
                {
                    fprintf(stderr, "The Expr Type is error.\n");
                    return Error;
                }

                break;

            case hsql::Expr::AND:
                if (tleft == 2)
                    flag = (bleft && bright);
                else
                {
                    fprintf(stderr, "The Expr Type is error.\n");
                    return Error;
                }

                break;

            case hsql::Expr::OR:
                if (tleft == 2)
                    flag = (bleft || bright);
                else
                {
                    fprintf(stderr, "The Expr Type is error.\n");
                    return Error;
                }

                break;

//        case hsql::Expr::NOT:
//            break;
            default:
                fprintf(stderr, "The Expr Operation is error.\n");
                return Error;
                break;
        }

        return Success;

    }

    RC getSet(const hsql::Expr &expr, const std::map<string, int> &st, std::map<RID, RM_Record> &ans, bool &flag)
    {
        if (expr.type != hsql::kExprOperator)
        {
            fprintf(stderr, "Where Expr should be a bool expression.\n");
            return Error;
        }

        int tleft = 0;
        std::map<RID, RM_Record> sleft;
        RM_Record head = rmfh->makeHead();
        Type *data;
        IX_Manager *index;

        switch (expr.expr->type)
        {
            case hsql::kExprColumnRef:
            {
                auto it = st.find(std::string(expr.expr->name));

                if (it == st.end())
                {
                    fprintf(stderr, "Column %s is not found.\n", expr.expr->name);
                    return Error;
                }

                data = head.get(it->second);

                if ((index = indexv[it->second]) == NULL)
                {
                    fprintf(stderr, "Column %s doesn't have index and try to use brute-force...\n", expr.expr->name);
                    flag = false;
                    return Success;
                }
            }
            break;

            case hsql::kExprOperator:
            {
                tleft = 1;
                bool f;
                RC result = getSet(*expr.expr, st, sleft, f);

                if (result == Error)
                {
                    return Error;
                }
                else if (f == false)
                {
                    flag = false;
                    return Success;
                }
            }
            break;

            default:
                fprintf(stderr, "Try to use brute-force...\n");
                flag = false;
                return Success;
        }

        if (expr.op_type == hsql::Expr::NOT)
        {
            fprintf(stderr, "Try to use brute-force...\n");
            flag = false;
            return Success;
        }

        int tright;
        std::map<RID, RM_Record> sright;
        int iright;
        const char *cright;

        switch (expr.expr2->type)
        {
            case hsql::kExprColumnRef:
                fprintf(stderr, "Try to use brute-force...\n");
                flag = false;
                return Success;

            case hsql::kExprLiteralInt:
                tright = 2;
                iright = expr.expr2->ival;
                break;

            case hsql::kExprLiteralString:
                tright = 3;
                cright = expr.expr2->name;
                break;

            case hsql::kExprOperator:
            {
                tright = 1;
                bool f;
                RC result = getSet(*expr.expr2, st, sright, f);

                if (result == Error)
                {
                    return Error;
                }
                else if (f == false)
                {
                    flag = false;
                    return Success;
                }
            }
            break;

            default:
                fprintf(stderr, "Try to use brute-force...\n");
                flag = false;
                return Success;
        }



        if (tleft == tright && tleft == 1)
        {
            switch (expr.op_type)
            {
                case hsql::Expr::AND:
                    for (auto it : sleft)
                    {
                        if (sright.find(it.first) != sright.end())
                            ans.insert(it);
                    }

                    flag = true;
                    break;

                case hsql::Expr::OR:
                    for (auto it : sleft)
                    {
                        if (ans.find(it.first) != ans.end())
                            ans.insert(it);
                    }

                    for (auto it : sright)
                    {
                        if (ans.find(it.first) != ans.end())
                            ans.insert(it);
                    }

                    break;

                default:
                    fprintf(stderr, "The Expr Operation is error.\n");
                    return Error;
            }
        }
        else if (tleft == 0 && tright == 2)
        {
            if (dynamic_cast<Type_int *>(data) != NULL)
            {
                ((Type_int *)data)->setValue(iright);
            }
            else
            {
                fprintf(stderr, "The Expr Column Type is error.\n");
                return Error;
            }

            switch (expr.op_type)
            {
                case hsql::Expr::SIMPLE_OP:

                    if (expr.op_char == '=')
                    {
                        std::vector<RID> vec = index->SearchEntry(data);

                        for (auto rid : vec)
                        {
                            RM_Record rec;
                            rmfh->GetRec(rid, rec);
                            ans.insert(make_pair(rid, rec));
                        }
                    }

                    else if (expr.op_char == '<')
                    {
                        Type *t = new Type_int(false, 0x80000000);
                        std::vector<RID> vec = index->SearchRangeEntry(t, data);
                        auto it = st.find(std::string(expr.expr->name));

                        for (auto rid : vec)
                        {
                            RM_Record rec;
                            rmfh->GetRec(rid, rec);

                            if (*((Type_int *)rec.get(it->second)) == *(Type_int *)data)
                            {
                                rec.clear();
                                continue;
                            }

                            ans.insert(make_pair(rid, rec));
                        }
                    }
                    else if (expr.op_char == '>')
                    {
                        Type *t = new Type_int(false, 0x7fffffff);
                        std::vector<RID> vec = index->SearchRangeEntry(data, t);
                        auto it = st.find(std::string(expr.expr->name));

                        for (auto rid : vec)
                        {
                            RM_Record rec;
                            rmfh->GetRec(rid, rec);

                            if (*((Type_int *)rec.get(it->second)) == *(Type_int *)data)
                            {
                                rec.clear();
                                continue;
                            }

                            ans.insert(make_pair(rid, rec));
                        }
                    }

                    break;

                case hsql::Expr::LESS_EQ:
                {
                    Type *t = new Type_int(false, 0x80000000);
                    std::vector<RID> vec = index->SearchRangeEntry(t, data);
                    auto it = st.find(std::string(expr.expr->name));

                    for (auto rid : vec)
                    {
                        RM_Record rec;
                        rmfh->GetRec(rid, rec);
                        ans.insert(make_pair(rid, rec));
                    }
                }
                break;

                case hsql::Expr::GREATER_EQ:
                {
                    Type *t = new Type_int(false, 0x7fffffff);
                    std::vector<RID> vec = index->SearchRangeEntry(data, t);
                    auto it = st.find(std::string(expr.expr->name));

                    for (auto rid : vec)
                    {
                        RM_Record rec;
                        rmfh->GetRec(rid, rec);
                        ans.insert(make_pair(rid, rec));
                    }
                }
                break;

                default:
                    fprintf(stderr, "Try to use brute-force...\n");
                    flag = false;
                    return Success;
            }
        }
        else if (tleft == 0 && tright == 3)
        {
            if (dynamic_cast<Type_varchar<32>*>(data) != NULL)
            {
                ((Type_varchar<32> *)data)->setStr(cright, strlen(cright));
            }
            else if (dynamic_cast<Type_varchar<64>*>(data) != NULL)
            {
                ((Type_varchar<64> *)data)->setStr(cright, strlen(cright));
            }
            else if (dynamic_cast<Type_varchar<128>*>(data) != NULL)
            {
                ((Type_varchar<128> *)data)->setStr(cright, strlen(cright));
            }
            else if (dynamic_cast<Type_varchar<256>*>(data) != NULL)
            {
                ((Type_varchar<256> *)data)->setStr(cright, strlen(cright));
            }
            else
            {
                fprintf(stderr, "The Expr Column Type is error.\n");
                return Error;
            }

            switch (expr.op_type)
            {
                case hsql::Expr::SIMPLE_OP:
                    if (expr.op_char == '=')
                    {
                        std::vector<RID> vec = index->SearchEntry(data);

                        for (auto rid : vec)
                        {
                            RM_Record rec;
                            rmfh->GetRec(rid, rec);
                            ans.insert(make_pair(rid, rec));
                        }
                    }

                    break;

                default:
                    fprintf(stderr, "Try to use brute-force...\n");
                    flag = false;
                    return Success;
            }
        }
        else
        {
            fprintf(stderr, "The Expr Type is error.\n");
            return Error;
        }

        flag = true;
        return Success;

    }

    RC selectRecord(std::vector<hsql::Expr *> &fields, hsql::Expr *wheres)
    {
        std::map<string, int> st = makeHeadMap();
        std::vector<RM_Record> ans;
        bool flag = false;
        std::map<RID, RM_Record> set;
        std::vector<std::pair<RID, RM_Record> > data;

        if (wheres && getSet(*wheres, st, set, flag) == Error)return Error;

        if (flag)
        {
            for (auto it : set)
            {
                data.push_back(it);
                ans.push_back(it.second);
            }
        }
        else
        {
            data = rmfh->ListRec();

            for (auto it : data)
            {
                bool flag;

                if (wheres && check(*wheres, st, it.second, flag) == Error)
                {
                    return Error;
                }

                if (!wheres || flag)ans.push_back(it.second);
            }
        }

        for (RM_Record rec : ans)
        {
            for (hsql::Expr * expr : fields)
            {
                switch (expr->type)
                {
                    case hsql::kExprStar:
                        rec.print();
                        break;

                    case hsql::kExprColumnRef:
                    {
                        auto it = st.find(std::string(expr->name));

                        if (it == st.end())
                        {
                            fprintf(stderr, "Column %s is not found.\n", expr->name);
                            return Error;
                        }

                        Type *t = rec.get(it->second);
                        t->print();
                    }
                    break;

                    default:
                        fprintf(stderr, "Expr type is error.\n");
                        return Error;
                }
            }

            printf("\n");
        }

        printf("\n");

        for (auto it : data)
        {
            it.second.clear();
        }
    }

    RC deleteRecord(hsql::Expr *wheres)
    {
        std::map<string, int> st = makeHeadMap();
        std::vector<RID> ans;
        bool flag = false;
        std::map<RID, RM_Record> set;
        std::vector<std::pair<RID, RM_Record> > data;

        if (wheres && getSet(*wheres, st, set, flag) == Error)return Error;

        if (flag)
        {
            for (auto it : set)
            {
                data.push_back(it);
                ans.push_back(it.first);
            }
        }
        else
        {
            data = rmfh->ListRec();

            for (auto it : data)
            {
                bool flag;

                if (wheres && check(*wheres, st, it.second, flag) == Error)
                {
                    return Error;
                }

                if (!wheres || flag)ans.push_back(it.first);
            }
        }

        for (RID rid : ans)
        {
            RM_Record record;
            rmfh->GetRec(rid, record);

            for (int i = 0; i < indexv.size(); i++)if (indexv[i])
                {
                    indexv[i]->DeleteEntry(record.get(i), rid);
                }

            record.clear();
            rmfh->DeleteRec(rid);
        }

        for (auto it : data)
        {
            it.second.clear();
        }
    }

    RC updateRecord(std::vector<hsql::UpdateClause *> &update, hsql::Expr *wheres)
    {
        std::map<string, int> st = makeHeadMap();

        for (auto it : update)
        {
            if (st.find(string(it->column)) == st.end())
            {
                fprintf(stderr, "Column %s is not found.\n", it->column);
                return Error;
            }
        }

        std::vector<RM_Record> rec;
        std::vector<RID> rid;
        bool flag = false;
        std::map<RID, RM_Record> set;
        std::vector<std::pair<RID, RM_Record> > data;

        if (wheres && getSet(*wheres, st, set, flag) == Error)return Error;

        if (flag)
        {
            for (auto it : set)
            {
                data.push_back(it);
                rid.push_back(it.first);
                rec.push_back(it.second);
            }
        }
        else
        {
            data = rmfh->ListRec();

            for (auto it : data)
            {
                bool flag;

                if (wheres && check(*wheres, st, it.second, flag) == Error)
                {
                    return Error;
                }

                if (!wheres || flag)rid.push_back(it.first), rec.push_back(it.second);
            }
        }

        for (RID rec : rid)
        {
            RM_Record record;
            rmfh->GetRec(rec, record);

            for (int i = 0; i < indexv.size(); i++)if (indexv[i])
                {
                    indexv[i]->DeleteEntry(record.get(i), rec);
                }

            record.clear();
            rmfh->DeleteRec(rec);
        }

        std::vector<RM_Record> ans(rec.size());

        bf::path filename = path / configFile;
        std::ifstream fi(filename.string());
        int n;
        fi >> n;

        for (int i = 0; i < n; i++)
        {
            std::string name, type;
            bool notnull, index, primary;
            int len;
            getline(fi, name);

            if (name.empty())getline(fi, name);

            fi >> type >> len >> notnull >> index >> primary;
            bool f = false;
            hsql::Expr *expr;

            for (auto it : update)
            {
                if (name == std::string(it->column))
                {
                    f = true;
                    expr = it->value;
                }
            }

            if (!f)
            {
                for (int j = 0; j < rec.size(); j++)ans[j].push_back(rec[j].get(i));

                continue;
            }

            Type *data;

            switch (expr->type)
            {
                case hsql::kExprLiteralInt:
                    if (type == "INTEGER" || type == "INT")
                    {
                        data = new Type_int(!notnull, expr->ival);
                    }
                    else
                    {
                        fprintf(stderr, "Update Column %s type is error.\n", name.c_str());
                        return Error;
                    }

                    break;

                case hsql::kExprLiteralString:
                    if (type == "CHAR" || type == "VARCHAR")
                    {
                        if (strlen(expr->name) > len)
                        {
                            fprintf(stderr, "Update Value '%s' is longer than %d.\n", expr->name, len);
                            return Error;
                        }

                        if (len <= 32)data = new Type_varchar<32>(!notnull, expr->name, strlen(expr->name));
                        else if (len <= 64)data = new Type_varchar<64>(!notnull, expr->name, strlen(expr->name));
                        else if (len <= 128)data = new Type_varchar<128>(!notnull, expr->name, strlen(expr->name));
                        else if (len <= 256)data = new Type_varchar<256>(!notnull, expr->name, strlen(expr->name));
                    }
                    else
                    {
                        fprintf(stderr, "Update type is error.\n");
                        return Error;
                    }

                    break;

                default:
                    fprintf(stderr, "Update type is error.\n");
                    return Error;
            }

            for (int j = 0; j < rec.size(); j++)ans[j].push_back(data);
        }

        for (RM_Record rec : ans)
        {
            RID rid;
            rmfh->InsertRec(rec, rid);

            for (int i = 0; i < n; i++)if (indexv[i])indexv[i]->InsertEntry(rec.get(i), rid);
        }


        for (auto it : data)
        {
            it.second.clear();
        }
    }

};


#endif