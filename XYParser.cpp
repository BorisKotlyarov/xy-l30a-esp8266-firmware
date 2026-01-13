#include "XYParser.h"

bool XYParser::parse(const char *line, XYPacket &packet)
{
    char buffer[64];
    strncpy(buffer, line, sizeof(buffer));
    buffer[sizeof(buffer) - 1] = '\0';

    const char *delimiters = " ,:%";
    char *token = strtok(buffer, delimiters);
    int field = 0;

    while (token != nullptr)
    {
        switch (field)
        {
        case 0:
            packet.voltage = atof(token);
            break;
        case 1:
            packet.percent = atoi(token);
            break;
        case 2:
            packet.hours = atoi(token);
            break;
        case 3:
            packet.minutes = atoi(token);
            break;
        case 4:
            strncpy(packet.state, token, 2);
            packet.state[2] = '\0';
            break;
        default:
            return false;
        }
        token = strtok(nullptr, delimiters);
        field++;
    }

    return (field == 5);
}
