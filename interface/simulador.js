// Simulador do galpão (Etapa 2): emula o hardware de cada robô, guarda o mapa e atende a interface
// gráfica. Conversa com o núcleo C++ só por MQTT, como o hardware real conversaria.
//
//   node interface/simulador.js --cenario config/cenario.json --robos 6 [--broker mqtt://127.0.0.1:1883]

import fs from 'node:fs';
import http from 'node:http';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import mqtt from 'mqtt';
import { RoboSimulado } from './sim/fisica.js';

const AQUI = path.dirname(fileURLToPath(import.meta.url));
const args = Object.fromEntries(
  process.argv.slice(2).reduce((acc, a, i, arr) => (a.startsWith('--') ? [...acc, [a.slice(2), arr[i + 1]]] : acc), [])
);
const CENARIO = args.cenario ?? 'config/cenario.json';
const ROBOS_INICIAIS = Number(args.robos ?? 4);
const BROKER = args.broker ?? 'mqtt://127.0.0.1:1883';
const PORTA_HTTP = Number(args['porta-http'] ?? 8080);
const PORTA_WS = Number(args['porta-ws'] ?? 9001);
const PERIODO_MS = 50;

const hora = () => new Date().toTimeString().slice(0, 8);
const log = (...m) => console.log(`${hora()} [simulador]`, ...m);

const cenario = JSON.parse(fs.readFileSync(CENARIO, 'utf8'));
const p = cenario.parametros ?? {};
const params = {
  velocidade_max: p.velocidade_max ?? 1,
  aceleracao: p.aceleracao ?? 1,
  descarga_movendo: p.descarga_movendo ?? 0.05,
  descarga_parado: p.descarga_parado ?? 0.01,
  recarga: p.recarga ?? 2,
  bateria_inicial: p.bateria_inicial ?? 100,
  tempo_atracar_s: p.tempo_atracar_s ?? 1,
};
const { largura, altura } = cenario.galpao;

// grade de ocupação: obstáculos menos passagens
const bloqueado = new Uint8Array(largura * altura);
for (const o of cenario.obstaculos ?? [])
  for (let y = o.y; y < o.y + o.h; y++) for (let x = o.x; x < o.x + o.w; x++) if (x >= 0 && y >= 0 && x < largura && y < altura) bloqueado[y * largura + x] = 1;
for (const q of cenario.passagens ?? [])
  for (let y = q.y; y < q.y + q.h; y++) for (let x = q.x; x < q.x + q.w; x++) bloqueado[y * largura + x] = 0;
const livre = (x, y) => {
  const cx = Math.floor(x);
  const cy = Math.floor(y);
  return cx >= 0 && cy >= 0 && cx < largura && cy < altura && !bloqueado[cy * largura + cx];
};

const robos = new Map();
const estatisticas = { colisoes: 0, tick_medio_ms: 0, tick_max_ms: 0 };
const emContato = new Set();
const acoplados = new Map();
let gestorOnline = false;
const anunciosPendentes = [];

function vagaLivre() {
  const e = cenario.estacionamento ?? { x: 1, y: 1, w: 5, h: 2 };
  for (let lin = 0; lin < e.h; lin++)
    for (let col = 0; col < e.w; col++) {
      const x = e.x + col + 0.5;
      const y = e.y + lin + 0.5;
      if (livre(x, y) && ![...robos.values()].some((r) => Math.hypot(r.x - x, r.y - y) < 0.7)) return { x, y };
    }
  return { x: e.x + 0.5 + Math.random() * e.w, y: e.y + 0.5 + Math.random() * e.h };
}

// ------------------------------------------------------------------ MQTT

const cliente = mqtt.connect(BROKER, {
  clientId: 'amr_simulador',
  reconnectPeriod: 1000,
  will: { topic: 'amr/status/simulador', payload: 'offline', qos: 1, retain: true },
});

cliente.on('connect', () => {
  log(`conectado ao broker ${BROKER}`);
  cliente.publish('amr/status/simulador', 'online', { qos: 1, retain: true });
  cliente.publish('amr/sim/mapa', JSON.stringify(cenario), { qos: 1, retain: true });
  cliente.subscribe(['amr/hw/+/atuadores', 'amr/sim/cmd', 'amr/status/gestor'], { qos: 1 });
  cliente.subscribe('amr/sup/estado', { qos: 0 });
});

cliente.on('message', (topico, dados) => {
  if (topico === 'amr/sup/estado') {
    // robôs acoplados à mesma carga formam um corpo só: não contam como colisão entre si
    acoplados.clear();
    try {
      for (const e of JSON.parse(dados.toString()).equipes ?? [])
        if (['ACOPLANDO', 'TRANSPORTE', 'DESACOPLANDO', 'ABORTANDO'].includes(e.estado)) for (const id of e.membros) acoplados.set(id, e.indice);
    } catch {
      /* estado incompleto: mantém o anterior */
    }
    return;
  }
  if (topico === 'amr/status/gestor') {
    gestorOnline = dados.toString() === 'online';
    if (gestorOnline) descarregarAnuncios();
    return;
  }
  let msg;
  try {
    msg = JSON.parse(dados.toString());
  } catch {
    return;
  }
  const m = topico.match(/^amr\/hw\/(\d+)\/atuadores$/);
  if (m) {
    robos.get(Number(m[1]))?.aplicarAtuadores(msg);
    return;
  }
  if (topico === 'amr/sim/cmd') comando(msg);
});

function anunciar(id, x, y) {
  anunciosPendentes.push({ id, x, y });
  if (gestorOnline) descarregarAnuncios();
}

function descarregarAnuncios() {
  // um anúncio a cada 150 ms, como robôs chegando ao galpão
  const proximo = anunciosPendentes.shift();
  if (!proximo) return;
  cliente.publish('amr/sim/robo_novo', JSON.stringify({ hw_id: proximo.id, i_posicao_x: Math.round(proximo.x * 100), i_posicao_y: Math.round(proximo.y * 100) }), { qos: 1 });
  log(`robô ${proximo.id} disponibilizado em (${proximo.x.toFixed(1)}, ${proximo.y.toFixed(1)})`);
  if (anunciosPendentes.length) setTimeout(descarregarAnuncios, 150);
}

function novoRobo(x, y) {
  const id = robos.size ? Math.max(...robos.keys()) + 1 : 1;
  const pos = x != null && y != null && livre(x, y) ? { x, y } : vagaLivre();
  robos.set(id, new RoboSimulado(id, pos.x, pos.y, params));
  anunciar(id, pos.x, pos.y);
  return id;
}

function comando(msg) {
  const r = robos.get(Number(msg.robo));
  switch (msg.acao) {
    case 'defeito':
      if (!r) return;
      r.falha = msg.ativo ?? !r.falha;
      log(`robô ${r.id}: defeito ${r.falha ? 'gerado' : 'removido'}`);
      break;
    case 'bateria':
      if (!r) return;
      r.bateria = Math.max(0, Math.min(100, Number(msg.nivel ?? r.bateria - 20)));
      log(`robô ${r.id}: bateria em ${r.bateria.toFixed(0)}%`);
      break;
    case 'comunicacao':
      if (!r) return;
      r.comunicacao = msg.ativo ?? !r.comunicacao;
      break;
    case 'novo_robo':
      novoRobo(msg.x, msg.y);
      break;
    case 'nova_caixa':
      // a fila de caixas é do organizador; o simulador só repassa a caixa que chegou na baia
      cliente.publish('amr/sim/caixa_nova', JSON.stringify({ tamanho: msg.tamanho, baia: msg.baia, zona: msg.zona }), { qos: 1 });
      log(`caixa de tamanho ${msg.tamanho} incluída na baia ${msg.baia} (destino zona ${msg.zona})`);
      break;
    default:
      break;
  }
}

// ------------------------------------------------------------------ física (50 ms)

let proximoTick = Date.now() + PERIODO_MS;
let ultimoEstado = 0;
function tick() {
  const inicio = performance.now();
  const dt = PERIODO_MS / 1000;
  const estacoes = cenario.estacoes_recarga ?? [];
  for (const r of robos.values()) {
    r.passo(dt, estacoes, livre);
    if (r.comunicacao && cliente.connected) cliente.publish(`amr/hw/${r.id}/sensores`, JSON.stringify(r.sensores()), { qos: 0 });
  }

  // colisão física: dois robôs a menos de 35 cm um do outro
  const lista = [...robos.values()];
  for (let i = 0; i < lista.length; i++)
    for (let j = i + 1; j < lista.length; j++) {
      const a = lista[i];
      const b = lista[j];
      const chave = `${a.id}-${b.id}`;
      const mesmaCarga = acoplados.has(a.id) && acoplados.get(a.id) === acoplados.get(b.id) && a.atracada && b.atracada;
      if (!mesmaCarga && Math.hypot(a.x - b.x, a.y - b.y) < 0.35) {
        if (!emContato.has(chave)) {
          emContato.add(chave);
          estatisticas.colisoes++;
          log(`colisão entre os robôs ${a.id} e ${b.id} em (${a.x.toFixed(1)}, ${a.y.toFixed(1)})`);
          cliente.publish("amr/sim/colisao", JSON.stringify({ robos: [a.id, b.id], x: +a.x.toFixed(2), y: +a.y.toFixed(2) }), { qos: 0 });
        }
      } else emContato.delete(chave);
    }

  const agora = Date.now();
  if (agora - ultimoEstado >= 100 && cliente.connected) {
    ultimoEstado = agora;
    cliente.publish('amr/sim/estado', JSON.stringify({ t: agora, robos: lista.map((r) => r.resumo()), ...estatisticas }), { qos: 0 });
  }

  const ms = performance.now() - inicio;
  estatisticas.tick_medio_ms = +(0.95 * estatisticas.tick_medio_ms + 0.05 * ms).toFixed(3);
  estatisticas.tick_max_ms = +Math.max(estatisticas.tick_max_ms, ms).toFixed(3);

  proximoTick += PERIODO_MS;
  const espera = proximoTick - Date.now();
  if (espera < -PERIODO_MS * 4) proximoTick = Date.now() + PERIODO_MS;  // atraso grande: não tenta recuperar em rajada
  setTimeout(tick, Math.max(0, espera));
}

// ------------------------------------------------------------------ interface web

const TIPOS = { '.html': 'text/html; charset=utf-8', '.js': 'text/javascript; charset=utf-8', '.css': 'text/css; charset=utf-8', '.json': 'application/json', '.svg': 'image/svg+xml', '.png': 'image/png', '.woff2': 'font/woff2' };
const RAIZES = [
  ['/vendor/three/', path.join(AQUI, 'node_modules/three/')],
  ['/vendor/mqtt/', path.join(AQUI, 'node_modules/mqtt/dist/')],
  ['/vendor/fontes/', path.join(AQUI, 'node_modules/@fontsource/')],
  ['/', path.join(AQUI, 'web/')],
];

http
  .createServer((req, res) => {
    const url = decodeURIComponent(new URL(req.url, 'http://x').pathname);
    if (url === '/config.json') {
      res.writeHead(200, { 'Content-Type': 'application/json' });
      res.end(JSON.stringify({ porta_ws: PORTA_WS }));
      return;
    }
    for (const [prefixo, raiz] of RAIZES) {
      if (!url.startsWith(prefixo)) continue;
      const arquivo = path.normalize(path.join(raiz, url.slice(prefixo.length) || 'index.html'));
      if (!arquivo.startsWith(raiz)) break;
      fs.readFile(arquivo, (erro, conteudo) => {
        if (erro) {
          res.writeHead(404);
          res.end('não encontrado');
          return;
        }
        res.writeHead(200, { 'Content-Type': TIPOS[path.extname(arquivo)] ?? 'application/octet-stream', 'Cache-Control': 'no-cache' });
        res.end(conteudo);
      });
      return;
    }
    res.writeHead(404);
    res.end();
  })
  .listen(PORTA_HTTP, () => log(`interface em http://localhost:${PORTA_HTTP}`));

for (let i = 0; i < ROBOS_INICIAIS; i++) novoRobo();
setTimeout(tick, PERIODO_MS);

const encerrar = () => {
  cliente.publish('amr/status/simulador', 'offline', { qos: 1, retain: true }, () => {
    cliente.end(false, () => process.exit(0));
  });
  setTimeout(() => process.exit(0), 1000);
};
process.on('SIGTERM', encerrar);
process.on('SIGINT', encerrar);
