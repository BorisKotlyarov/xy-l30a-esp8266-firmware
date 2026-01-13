#ifndef XYPARSER_H
#define XYPARSER_H

#include <Arduino.h>

struct XYPacket
{
    float voltage; // ex: 00.0V
    int percent;   // ex: 000
    int hours;     // ex: 00
    int minutes;   // ex: 00
    char state[3]; // ex: "CL"
};

class XYParser
{
public:
    static bool parse(const char *line, XYPacket &packet);
};

#endif // XYPARSER_H
