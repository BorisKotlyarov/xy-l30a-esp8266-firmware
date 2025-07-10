#ifndef XYPARSER_H
#define XYPARSER_H

#include <Arduino.h>

struct XYPacket
{
    float voltage; // Пример: 00.0V
    int percent;   // Пример: 000
    int hours;     // Время: часы
    int minutes;   // Время: минуты
    char state[3]; // Например: "CL"
};

class XYParser
{
public:
    // Функция разбора строки, возвращает true при успешном парсинге
    static bool parse(const char *line, XYPacket &packet);
};

#endif // XYPARSER_H