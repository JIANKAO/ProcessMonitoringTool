#pragma once
#include <QString>

class Tr {
public:
    static void setLang(const QString& code);
    static QString lang();
    static QString s(const char* key);
};