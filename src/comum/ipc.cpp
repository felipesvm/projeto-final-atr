#include "ipc.hpp"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/sem.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

#include "tempo.hpp"

namespace amr {

ErroSistema::ErroSistema(const std::string& onde)
    : std::runtime_error(onde + ": " + std::strerror(errno)), codigo_(errno) {}

// ---------------------------------------------------------------- memória

MemoriaCompartilhada MemoriaCompartilhada::criar(const std::string& nome, size_t tamanho) {
    shm_unlink(nome.c_str());
    int fd = shm_open(nome.c_str(), O_CREAT | O_EXCL | O_RDWR, 0660);
    if (fd < 0) throw ErroSistema("shm_open " + nome);
    if (ftruncate(fd, off_t(tamanho)) != 0) {
        close(fd);
        throw ErroSistema("ftruncate " + nome);
    }
    void* p = mmap(nullptr, tamanho, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (p == MAP_FAILED) throw ErroSistema("mmap " + nome);
    std::memset(p, 0, tamanho);
    return MemoriaCompartilhada(p, tamanho);
}

MemoriaCompartilhada MemoriaCompartilhada::abrir(const std::string& nome) {
    int fd = shm_open(nome.c_str(), O_RDWR, 0);
    if (fd < 0) throw ErroSistema("shm_open " + nome);
    struct stat st {};
    if (fstat(fd, &st) != 0) {
        close(fd);
        throw ErroSistema("fstat " + nome);
    }
    void* p = mmap(nullptr, size_t(st.st_size), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (p == MAP_FAILED) throw ErroSistema("mmap " + nome);
    return MemoriaCompartilhada(p, size_t(st.st_size));
}

bool MemoriaCompartilhada::existe(const std::string& nome) {
    int fd = shm_open(nome.c_str(), O_RDONLY, 0);
    if (fd < 0) return false;
    close(fd);
    return true;
}

void MemoriaCompartilhada::remover(const std::string& nome) { shm_unlink(nome.c_str()); }

MemoriaCompartilhada::MemoriaCompartilhada(MemoriaCompartilhada&& outra) noexcept
    : dados_(std::exchange(outra.dados_, nullptr)), tamanho_(std::exchange(outra.tamanho_, 0)) {}

MemoriaCompartilhada& MemoriaCompartilhada::operator=(MemoriaCompartilhada&& outra) noexcept {
    if (this != &outra) {
        if (dados_) munmap(dados_, tamanho_);
        dados_ = std::exchange(outra.dados_, nullptr);
        tamanho_ = std::exchange(outra.tamanho_, 0);
    }
    return *this;
}

MemoriaCompartilhada::~MemoriaCompartilhada() {
    if (dados_) munmap(dados_, tamanho_);
}

// ---------------------------------------------------------------- filas

namespace {
int flags_acesso(FilaMensagens::Acesso a) {
    switch (a) {
        case FilaMensagens::Acesso::Leitura: return O_RDONLY;
        case FilaMensagens::Acesso::Escrita: return O_WRONLY;
        default: return O_RDWR;
    }
}
}  // namespace

FilaMensagens FilaMensagens::criar(const std::string& nome, long max_mensagens, size_t tamanho_msg, Acesso acesso) {
    mq_attr attr{};
    attr.mq_maxmsg = max_mensagens;
    attr.mq_msgsize = long(tamanho_msg);
    mqd_t fd = mq_open(nome.c_str(), O_CREAT | flags_acesso(acesso), 0660, &attr);
    if (fd == mqd_t(-1) && errno == EINVAL && max_mensagens > 10) {
        // Sem permissão para passar de fs.mqueue.msg_max: cai para o padrão do sistema.
        attr.mq_maxmsg = 10;
        fd = mq_open(nome.c_str(), O_CREAT | flags_acesso(acesso), 0660, &attr);
    }
    if (fd == mqd_t(-1)) throw ErroSistema("mq_open " + nome);
    return FilaMensagens(fd, nome);
}

FilaMensagens FilaMensagens::abrir(const std::string& nome, Acesso acesso) {
    mqd_t fd = mq_open(nome.c_str(), flags_acesso(acesso));
    if (fd == mqd_t(-1)) throw ErroSistema("mq_open " + nome);
    return FilaMensagens(fd, nome);
}

void FilaMensagens::remover(const std::string& nome) { mq_unlink(nome.c_str()); }

FilaMensagens::FilaMensagens(FilaMensagens&& outra) noexcept
    : fd_(std::exchange(outra.fd_, mqd_t(-1))), nome_(std::move(outra.nome_)) {}

FilaMensagens& FilaMensagens::operator=(FilaMensagens&& outra) noexcept {
    if (this != &outra) {
        if (valida()) mq_close(fd_);
        fd_ = std::exchange(outra.fd_, mqd_t(-1));
        nome_ = std::move(outra.nome_);
    }
    return *this;
}

FilaMensagens::~FilaMensagens() {
    if (valida()) mq_close(fd_);
}

bool FilaMensagens::enviar(const void* msg, size_t tamanho, unsigned prioridade, int64_t espera_ns) {
    for (;;) {
        int r;
        if (espera_ns < 0) {
            r = mq_send(fd_, static_cast<const char*>(msg), tamanho, prioridade);
        } else {
            timespec prazo = prazo_parede(espera_ns);
            r = mq_timedsend(fd_, static_cast<const char*>(msg), tamanho, prioridade, &prazo);
        }
        if (r == 0) return true;
        if (errno == EINTR) continue;
        if (errno == ETIMEDOUT || errno == EAGAIN) return false;
        throw ErroSistema("mq_send " + nome_);
    }
}

bool FilaMensagens::receber(void* msg, size_t tamanho, int64_t espera_ns, unsigned* prioridade) {
    for (;;) {
        ssize_t r;
        if (espera_ns < 0) {
            r = mq_receive(fd_, static_cast<char*>(msg), tamanho, prioridade);
        } else {
            timespec prazo = prazo_parede(espera_ns);
            r = mq_timedreceive(fd_, static_cast<char*>(msg), tamanho, prioridade, &prazo);
        }
        if (r >= 0) return true;
        if (errno == EINTR && espera_ns < 0) continue;
        if (errno == ETIMEDOUT || errno == EAGAIN || errno == EINTR) return false;
        throw ErroSistema("mq_receive " + nome_);
    }
}

long FilaMensagens::capacidade() const {
    mq_attr a{};
    mq_getattr(fd_, &a);
    return a.mq_maxmsg;
}

long FilaMensagens::pendentes() const {
    mq_attr a{};
    mq_getattr(fd_, &a);
    return a.mq_curmsgs;
}

// ---------------------------------------------------------------- semáforos POSIX

SemaforoNomeado SemaforoNomeado::criar(const std::string& nome, unsigned valor) {
    sem_unlink(nome.c_str());
    sem_t* s = sem_open(nome.c_str(), O_CREAT | O_EXCL, 0660, valor);
    if (s == SEM_FAILED) throw ErroSistema("sem_open " + nome);
    return SemaforoNomeado(s);
}

SemaforoNomeado SemaforoNomeado::abrir(const std::string& nome) {
    sem_t* s = sem_open(nome.c_str(), 0);
    if (s == SEM_FAILED) throw ErroSistema("sem_open " + nome);
    return SemaforoNomeado(s);
}

void SemaforoNomeado::remover(const std::string& nome) { sem_unlink(nome.c_str()); }

SemaforoNomeado::SemaforoNomeado(SemaforoNomeado&& outro) noexcept : sem_(std::exchange(outro.sem_, nullptr)) {}

SemaforoNomeado& SemaforoNomeado::operator=(SemaforoNomeado&& outro) noexcept {
    if (this != &outro) {
        if (sem_) sem_close(sem_);
        sem_ = std::exchange(outro.sem_, nullptr);
    }
    return *this;
}

SemaforoNomeado::~SemaforoNomeado() {
    if (sem_) sem_close(sem_);
}

bool SemaforoNomeado::tentar() {
    while (sem_trywait(sem_) != 0) {
        if (errno == EAGAIN) return false;
        if (errno != EINTR) throw ErroSistema("sem_trywait");
    }
    return true;
}

bool SemaforoNomeado::esperar_por(int64_t espera_ns) {
    timespec prazo = prazo_parede(espera_ns);
    while (sem_timedwait(sem_, &prazo) != 0) {
        if (errno == ETIMEDOUT) return false;
        if (errno != EINTR) throw ErroSistema("sem_timedwait");
    }
    return true;
}

void SemaforoNomeado::liberar() {
    if (sem_post(sem_) != 0) throw ErroSistema("sem_post");
}

int SemaforoNomeado::valor() const {
    int v = 0;
    sem_getvalue(sem_, &v);
    return v;
}

// ---------------------------------------------------------------- semáforos System V

namespace {
union ArgSemctl {
    int val;
    semid_ds* buf;
    unsigned short* array;
};
}  // namespace

SemaforosPassagem SemaforosPassagem::criar(const std::vector<int>& capacidades) {
    int n = int(capacidades.size());
    int semid = semget(IPC_PRIVATE, n > 0 ? n : 1, IPC_CREAT | 0660);
    if (semid < 0) throw ErroSistema("semget");
    for (int i = 0; i < n; ++i) {
        ArgSemctl arg{};
        arg.val = capacidades[size_t(i)];
        if (semctl(semid, i, SETVAL, arg) != 0) throw ErroSistema("semctl SETVAL");
    }
    return SemaforosPassagem(semid, n);
}

SemaforosPassagem SemaforosPassagem::abrir(int semid, int quantidade) { return SemaforosPassagem(semid, quantidade); }

bool SemaforosPassagem::tentar_adquirir(const std::vector<Operacao>& ops) {
    std::vector<sembuf> buf;
    buf.reserve(ops.size());
    for (const auto& op : ops) {
        sembuf s{};
        s.sem_num = static_cast<unsigned short>(op.passagem);
        s.sem_op = static_cast<short>(-op.unidades);
        s.sem_flg = IPC_NOWAIT;  // sem SEM_UNDO: robô morto continua fisicamente na passagem
        buf.push_back(s);
    }
    while (semop(semid_, buf.data(), buf.size()) != 0) {
        if (errno == EAGAIN) return false;
        if (errno != EINTR) throw ErroSistema("semop");
    }
    return true;
}

void SemaforosPassagem::liberar(int passagem, int unidades) {
    sembuf s{};
    s.sem_num = static_cast<unsigned short>(passagem);
    s.sem_op = static_cast<short>(unidades);
    s.sem_flg = 0;
    while (semop(semid_, &s, 1) != 0) {
        if (errno != EINTR) throw ErroSistema("semop liberar");
    }
}

int SemaforosPassagem::valor(int passagem) const { return semctl(semid_, passagem, GETVAL); }

void SemaforosPassagem::remover() {
    if (semid_ >= 0) semctl(semid_, 0, IPC_RMID);
    semid_ = -1;
}

// ---------------------------------------------------------------- mutex compartilhado

void iniciar_mutex_compartilhado(pthread_mutex_t* mutex) {
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    pthread_mutexattr_setrobust(&attr, PTHREAD_MUTEX_ROBUST);
    pthread_mutexattr_setprotocol(&attr, PTHREAD_PRIO_INHERIT);
    int r = pthread_mutex_init(mutex, &attr);
    pthread_mutexattr_destroy(&attr);
    if (r != 0) {
        errno = r;
        throw ErroSistema("pthread_mutex_init");
    }
}

TravaCompartilhada::TravaCompartilhada(pthread_mutex_t* mutex) : mutex_(mutex) {
    int r = pthread_mutex_lock(mutex_);
    if (r == EOWNERDEAD) {
        pthread_mutex_consistent(mutex_);
        dono_morreu_ = true;
    } else if (r != 0) {
        errno = r;
        throw ErroSistema("pthread_mutex_lock");
    }
}

TravaCompartilhada::~TravaCompartilhada() { pthread_mutex_unlock(mutex_); }

}  // namespace amr
