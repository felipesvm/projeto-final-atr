// Hardware simulado de um AMR: ponto material com velocidade e aceleração limitadas,
// bateria, mecanismo de acoplamento e watchdog de comunicação.

export class RoboSimulado {
  constructor(id, x, y, p) {
    this.id = id;
    this.x = x;
    this.y = y;
    this.vx = 0;
    this.vy = 0;
    this.theta = 0;
    this.p = p;
    this.bateria = p.bateria_inicial ?? 100;
    this.falha = false;
    this.comunicacao = true;
    this.atracada = false;
    this.acoplar = false;
    this.tempoComando = 0;
    this.sp = { x, y };
    this.ultimoAtuador = Date.now();
    this.seq = 0;
    this.distancia = 0;
  }

  aplicarAtuadores(msg) {
    // Tabela 1: setpoints inteiros em centímetros
    this.sp = { x: msg.o_sp_posicao_x / 100, y: msg.o_sp_posicao_y / 100 };
    const acoplar = !!msg.o_acoplar_e_erguer;
    if (acoplar !== this.acoplar) {
      this.acoplar = acoplar;
      this.tempoComando = 0;
    }
    this.ultimoAtuador = Date.now();
  }

  passo(dt, estacoes, livre) {
    const { velocidade_max: vmax, aceleracao: a } = this.p;
    // watchdog de hardware: sem atuadores por 1 s, o robô para sozinho
    const semComando = Date.now() - this.ultimoAtuador > 1000;
    let vdx = 0;
    let vdy = 0;
    if (!this.falha && !semComando) {
      const ex = this.sp.x - this.x;
      const ey = this.sp.y - this.y;
      const d = Math.hypot(ex, ey);
      if (d > 1e-3) {
        const v = Math.min(vmax, Math.sqrt(2 * a * d));
        vdx = (v * ex) / d;
        vdy = (v * ey) / d;
      }
    }
    let ax = vdx - this.vx;
    let ay = vdy - this.vy;
    const dv = Math.hypot(ax, ay);
    const dvMax = a * dt;
    if (dv > dvMax) {
      ax *= dvMax / dv;
      ay *= dvMax / dv;
    }
    this.vx += ax;
    this.vy += ay;
    // parede: o robô desliza ao longo dela em vez de atravessá-la
    let nx = this.x + this.vx * dt;
    let ny = this.y + this.vy * dt;
    if (!livre(nx, ny)) {
      if (livre(nx, this.y)) {
        ny = this.y;
        this.vy = 0;
      } else if (livre(this.x, ny)) {
        nx = this.x;
        this.vx = 0;
      } else {
        nx = this.x;
        ny = this.y;
        this.vx = 0;
        this.vy = 0;
      }
    }
    this.distancia += Math.hypot(nx - this.x, ny - this.y);
    this.x = nx;
    this.y = ny;
    const rapidez = Math.hypot(this.vx, this.vy);
    if (rapidez > 0.05) this.theta = Math.atan2(this.vy, this.vx);

    const naEstacao = estacoes.some((e) => Math.hypot(e.x - this.x, e.y - this.y) < 0.5);
    if (rapidez < 0.02 && naEstacao) {
      this.bateria = Math.min(100, this.bateria + this.p.recarga * dt);
    } else {
      const carga = this.atracada ? 1.5 : 1;
      const taxa = rapidez > 0.02 ? this.p.descarga_movendo * carga : this.p.descarga_parado;
      this.bateria = Math.max(0, this.bateria - taxa * dt);
    }

    this.tempoComando += dt;
    if (this.tempoComando >= this.p.tempo_atracar_s) this.atracada = this.acoplar;
  }

  sensores() {
    return {
      i_posicao_x: Math.round(this.x * 100),
      i_posicao_y: Math.round(this.y * 100),
      i_carga_atracada: this.atracada,
      i_percentual_bateria: Math.floor(this.bateria),
      i_falha: this.falha,
      seq: ++this.seq,
    };
  }

  resumo() {
    return {
      id: this.id,
      x: +this.x.toFixed(3),
      y: +this.y.toFixed(3),
      theta: +this.theta.toFixed(3),
      v: +Math.hypot(this.vx, this.vy).toFixed(2),
      bateria: +this.bateria.toFixed(1),
      falha: this.falha,
      comunicacao: this.comunicacao,
      atracada: this.atracada,
      acoplar: this.acoplar,
      sp_x: +this.sp.x.toFixed(2),
      sp_y: +this.sp.y.toFixed(2),
    };
  }
}
