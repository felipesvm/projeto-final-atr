#pragma once

#include <cstdint>
#include <ctime>
#include <string>

namespace amr {

constexpr int64_t NS_POR_MS = 1000000;
constexpr int64_t NS_POR_S = 1000000000;

inline int64_t ms_para_ns(int64_t ms) { return ms * NS_POR_MS; }

inline int64_t ler_relogio(clockid_t relogio) {
    timespec ts{};
    clock_gettime(relogio, &ts);
    return int64_t(ts.tv_sec) * NS_POR_S + ts.tv_nsec;
}

inline int64_t agora_ns() { return ler_relogio(CLOCK_MONOTONIC); }
inline int64_t parede_ns() { return ler_relogio(CLOCK_REALTIME); }

inline timespec para_timespec(int64_t ns) {
    timespec ts{};
    ts.tv_sec = ns / NS_POR_S;
    ts.tv_nsec = ns % NS_POR_S;
    return ts;
}

// Prazo absoluto no relógio de parede, usado por mq_timed* e sem_timedwait.
inline timespec prazo_parede(int64_t daqui_ns) { return para_timespec(parede_ns() + daqui_ns); }

std::string data_hora_iso(int64_t parede);

}  // namespace amr
