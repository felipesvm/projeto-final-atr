#include "tempo.hpp"

#include <cstdio>

namespace amr {

std::string data_hora_iso(int64_t parede) {
    time_t segundos = parede / NS_POR_S;
    int milis = int((parede % NS_POR_S) / NS_POR_MS);
    tm local{};
    localtime_r(&segundos, &local);
    char buf[64];
    std::snprintf(buf, sizeof buf, "%04d-%02d-%02dT%02d:%02d:%02d.%03d", local.tm_year + 1900,
                  local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min, local.tm_sec, milis);
    return buf;
}

}  // namespace amr
