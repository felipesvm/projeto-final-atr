#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace amr {

struct ConfigMqtt {
    std::string host = "127.0.0.1";
    int porta = 1883;
    std::string id_cliente;
    std::string topico_testamento;  // LWT: publicado pelo broker se a conexão cair
    std::string mensagem_testamento = "offline";
    int keepalive_s = 10;

    // "host:porta", como em AMR_MQTT
    static ConfigMqtt de_endereco(const std::string& endereco, std::string id);
};

// Cliente MQTT 3.1.1 mínimo (CONNECT com LWT, PUBLISH QoS 0/1, SUBSCRIBE, PINGREQ), com reconexão.
// Os callbacks rodam na thread de recepção do cliente e só devem copiar dados.
class ClienteMqtt {
public:
    using Callback = std::function<void(const std::string& topico, const std::string& payload)>;

    explicit ClienteMqtt(ConfigMqtt cfg);
    ~ClienteMqtt();
    ClienteMqtt(const ClienteMqtt&) = delete;
    ClienteMqtt& operator=(const ClienteMqtt&) = delete;

    void assinar(const std::string& filtro, int qos, Callback cb);
    void iniciar();
    void parar();

    // Não espera confirmação; devolve falso se não houver conexão no momento.
    bool publicar(const std::string& topico, const std::string& payload, int qos = 0, bool retido = false);
    bool conectado() const { return conectado_.load(); }

    static bool casa(const std::string& filtro, const std::string& topico);

private:
    struct Assinatura {
        std::string filtro;
        int qos;
        Callback cb;
    };

    void laco();
    bool conectar();
    void desconectar();
    bool enviar(const std::vector<uint8_t>& pacote);
    bool ler_pacote(uint8_t& tipo, std::vector<uint8_t>& corpo, int espera_ms);
    void tratar_publish(uint8_t flags, const std::vector<uint8_t>& corpo);

    ConfigMqtt cfg_;
    std::vector<Assinatura> assinaturas_;
    std::mutex mtx_assinaturas_;
    std::mutex mtx_envio_;
    int sock_ = -1;
    std::atomic<bool> ativo_{false};
    std::atomic<bool> conectado_{false};
    std::atomic<int64_t> ultimo_envio_ns_{0};
    uint16_t proximo_id_ = 1;
    std::thread thread_;
};

}  // namespace amr
