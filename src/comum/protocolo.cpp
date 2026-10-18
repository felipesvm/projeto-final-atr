#include "protocolo.hpp"

namespace amr {

const char* nome_tipo(TipoMsg t) {
    switch (t) {
        case TipoMsg::PARAR: return "PARAR";
        case TipoMsg::ABORTAR: return "ABORTAR";
        case TipoMsg::ENCERRAR: return "ENCERRAR";
        case TipoMsg::RETOMAR: return "RETOMAR";
        case TipoMsg::MODO: return "MODO";
        case TipoMsg::DESTINO_MANUAL: return "DESTINO_MANUAL";
        case TipoMsg::MISSAO_EQUIPE: return "MISSAO_EQUIPE";
        case TipoMsg::RECONHECER_FALHA: return "RECONHECER_FALHA";
        case TipoMsg::FALHA: return "FALHA";
        case TipoMsg::BATERIA_CRITICA: return "BATERIA_CRITICA";
        case TipoMsg::SEM_COMUNICACAO: return "SEM_COMUNICACAO";
        case TipoMsg::NOVO_ROBO: return "NOVO_ROBO";
        case TipoMsg::ROBO_PRONTO: return "ROBO_PRONTO";
        case TipoMsg::ROBO_TERMINOU: return "ROBO_TERMINOU";
        case TipoMsg::EM_POSICAO: return "EM_POSICAO";
        case TipoMsg::ATRACADO: return "ATRACADO";
        case TipoMsg::ENTREGUE: return "ENTREGUE";
        case TipoMsg::ABORTO_CONCLUIDO: return "ABORTO_CONCLUIDO";
        case TipoMsg::BATERIA_BAIXA: return "BATERIA_BAIXA";
        case TipoMsg::RECARGA_CONCLUIDA: return "RECARGA_CONCLUIDA";
        case TipoMsg::PASSAGEM_ESPERA: return "PASSAGEM_ESPERA";
        case TipoMsg::ROTA_TIMEOUT: return "ROTA_TIMEOUT";
        case TipoMsg::SEM_ROTA: return "SEM_ROTA";
        case TipoMsg::DEADLINE_NAV: return "DEADLINE_NAV";
        case TipoMsg::OPERADOR: return "OPERADOR";
        case TipoMsg::ANUNCIO: return "ANUNCIO";
        case TipoMsg::INJETAR: return "INJETAR";
    }
    return "?";
}

unsigned prioridade(TipoMsg t) {
    switch (t) {
        case TipoMsg::PARAR:
        case TipoMsg::ABORTAR:
        case TipoMsg::ENCERRAR: return 31;
        case TipoMsg::FALHA:
        case TipoMsg::BATERIA_CRITICA:
        case TipoMsg::SEM_COMUNICACAO:
        case TipoMsg::OPERADOR: return 30;
        case TipoMsg::RETOMAR:
        case TipoMsg::MODO:
        case TipoMsg::NOVO_ROBO:
        case TipoMsg::ROBO_PRONTO:
        case TipoMsg::ROBO_TERMINOU:
        case TipoMsg::EM_POSICAO:
        case TipoMsg::ATRACADO:
        case TipoMsg::ENTREGUE:
        case TipoMsg::ABORTO_CONCLUIDO: return 20;
        case TipoMsg::DESTINO_MANUAL: return 15;
        case TipoMsg::MISSAO_EQUIPE:
        case TipoMsg::RECONHECER_FALHA:
        case TipoMsg::BATERIA_BAIXA:
        case TipoMsg::RECARGA_CONCLUIDA: return 10;
        case TipoMsg::PASSAGEM_ESPERA:
        case TipoMsg::ROTA_TIMEOUT:
        case TipoMsg::SEM_ROTA:
        case TipoMsg::DEADLINE_NAV: return 5;
        default: return 10;
    }
}

}  // namespace amr
