// Tela de operação e simulação. É um cliente MQTT comum (via WebSocket): assina o estado publicado
// pelo organizador, pelo simulador e pelo coletor, e publica comandos de operação e de simulação.

import { Cena, CORES_ESTADO } from './cena.js';

const NOMES_ESTADO = {
  INICIALIZANDO: 'Inicializando',
  DISPONIVEL: 'Disponível',
  DESLOCANDO: 'Deslocando',
  AGUARDANDO_PASSAGEM: 'Aguardando passagem',
  AGUARDANDO_EQUIPE: 'Aguardando equipe',
  ACOPLANDO: 'Acoplando',
  TRANSPORTANDO: 'Transportando carga',
  DESACOPLANDO: 'Desacoplando',
  ABORTANDO: 'Depositando carga',
  DESLOCANDO_RECARGA: 'Indo recarregar',
  RECARREGANDO: 'Recarregando',
  DEFEITO: 'Defeito',
  SEM_COMUNICACAO: 'Sem comunicação',
};
const NOMES_CAIXA = {
  NA_FILA: 'Na fila da baia',
  NO_PR: 'Na baia, aguardando equipe',
  DESIGNADA: 'Equipe a caminho',
  EM_TRANSPORTE: 'Em transporte',
  INTERROMPIDA: 'Interrompida',
  ENTREGUE: 'Entregue',
};

const $ = (s) => document.querySelector(s);
const el = (tag, props = {}, ...filhos) => {
  const e = document.createElement(tag);
  Object.assign(e, props);
  for (const f of filhos) e.append(f);
  return e;
};

const estado = {
  mapa: null,
  sup: { robos: [], equipes: [], passagens: [], caixas: [], kpi: {}, sistema: {} },
  sim: { robos: [], colisoes: 0 },
  selecionado: null,
  eventos: [],
  alarmes: [],
  online: new Set(),
};

const cena = new Cena($('#cena'), {
  aoSelecionarRobo: (id) => selecionar(id),
  aoClicarPiso: () => {},
});

function atualizarAreaLivre() {
  const frota = document.querySelector('.painel-frota').getBoundingClientRect();
  const detalhe = $('#painel-detalhe');
  const gaveta = $('#gaveta').getBoundingClientRect();
  cena.definirAreaLivre({
    esquerda: frota.right,
    direita: detalhe.hidden ? 0 : window.innerWidth - detalhe.getBoundingClientRect().left,
    topo: 64,
    base: window.innerHeight - gaveta.top,
  });
}
window.addEventListener('resize', atualizarAreaLivre);
atualizarAreaLivre();

// ---------------------------------------------------------------- MQTT

const config = await fetch('/config.json').then((r) => r.json()).catch(() => ({ porta_ws: 9001 }));
const cliente = mqtt.connect(`ws://${location.hostname}:${config.porta_ws}`, {
  clientId: `amr_tela_${Math.random().toString(16).slice(2, 8)}`,
  reconnectPeriod: 1000,
});

cliente.on('connect', () => {
  estado.online.add('broker');
  cliente.subscribe(['amr/sim/mapa', 'amr/sim/estado', 'amr/sup/estado', 'amr/sup/alarme', 'amr/log/evento', 'amr/log/historico', 'amr/robo/+/rota', 'amr/status/+'], { qos: 1 });
  cliente.publish('amr/log/pedido', '{}', { qos: 1 });
  desenharConexoes();
});
cliente.on('close', () => {
  estado.online.delete('broker');
  desenharConexoes();
});

cliente.on('message', (topico, dados) => {
  const texto = dados.toString();
  if (topico.startsWith('amr/status/')) {
    const proc = topico.slice('amr/status/'.length);
    if (texto === 'online') estado.online.add(proc);
    else estado.online.delete(proc);
    desenharConexoes();
    return;
  }
  let msg;
  try {
    msg = JSON.parse(texto);
  } catch {
    return;
  }
  switch (topico) {
    case 'amr/sim/mapa':
      estado.mapa = msg;
      cena.construirMapa(msg).then(() => preencherLocais());
      break;
    case 'amr/sim/estado':
      estado.sim = msg;
      if (cena.L) cena.atualizarFisico(msg);
      break;
    case 'amr/sup/estado':
      estado.sup = msg;
      if (cena.L) cena.atualizarLogico(msg, estado.sim);
      desenharTudo();
      break;
    case 'amr/sup/alarme':
      estado.alarmes.unshift(msg);
      estado.alarmes.length = Math.min(estado.alarmes.length, 200);
      desenharAlarmes();
      avisar(msg.texto);
      break;
    case 'amr/log/evento':
      estado.eventos.unshift(msg);
      estado.eventos.length = Math.min(estado.eventos.length, 400);
      desenharEventos();
      break;
    case 'amr/log/historico':
      estado.eventos = msg.reverse();
      desenharEventos();
      break;
    default: {
      const m = topico.match(/^amr\/robo\/(\d+)\/rota$/);
      if (m) cena.definirRota(Number(m[1]), msg);
    }
  }
});

const operador = (acao, extra = {}) => cliente.publish('amr/cmd/operador', JSON.stringify({ acao, ...extra }), { qos: 1 });
const simulacao = (acao, extra = {}) => cliente.publish('amr/sim/cmd', JSON.stringify({ acao, ...extra }), { qos: 1 });

// ---------------------------------------------------------------- desenho

function desenharConexoes() {
  for (const li of document.querySelectorAll('#conexoes li')) li.classList.toggle('online', estado.online.has(li.dataset.proc));
}

function robosCombinados() {
  const logicos = new Map(estado.sup.robos.map((r) => [r.id, r]));
  return (estado.sim.robos ?? []).map((f) => ({ ...f, ...(logicos.get(f.id) ?? { estado: 'INICIALIZANDO', modo: 'AUTO' }), fisico: f })).sort((a, b) => a.id - b.id);
}

function bateria(b) {
  const classe = b <= 15 ? 'critica' : b <= 30 ? 'baixa' : '';
  return `<span class="bateria ${classe}"><i><b style="width:${Math.max(0, Math.min(100, b))}%"></b></i>${Math.round(b)}%</span>`;
}

function rotuloEstado(e) {
  return `<span class="estado-rotulo" style="--cor:${CORES_ESTADO[e] ?? '#8a96a0'}">${NOMES_ESTADO[e] ?? e}</span>`;
}

function desenharTudo() {
  const s = estado.sup.sistema ?? {};
  const sis = $('#sistema');
  sis.dataset.estado = s.estado ?? 'INICIANDO';
  $('#sistema-estado').textContent = s.parada_geral ? 'Parada geral' : s.estado === 'OPERANDO' ? 'Operando' : 'Iniciando';
  $('#relogio').textContent = `t = ${Math.floor(estado.sup.t_s ?? 0)} s`;
  const parada = $('#parada-geral');
  parada.textContent = s.parada_geral ? 'Liberar parada' : 'Parada geral';
  parada.classList.toggle('ativa', !!s.parada_geral);
  if (document.activeElement?.id !== 'chave-anticolisao') $('#chave-anticolisao').checked = s.anticolisao !== false;
  if (document.activeElement?.id !== 'chave-rotas-adaptativas') $('#chave-rotas-adaptativas').checked = s.rotas_adaptativas !== false;
  desenharFrota();
  desenharDetalhe();
  desenharCargas();
  desenharDesempenho();
}

function desenharFrota() {
  const robos = robosCombinados();
  $('#contagem-frota').textContent = `${robos.length} ${robos.length === 1 ? 'robô' : 'robôs'}`;
  const lista = $('#lista-frota');
  const ids = new Set(robos.map((r) => String(r.id)));
  for (const li of [...lista.children]) if (!ids.has(li.dataset.id)) li.remove();
  robos.forEach((r, i) => {
    // atualiza no lugar: recriar a lista a cada quadro faria cliques se perderem
    let li = lista.querySelector(`li[data-id="${r.id}"]`);
    if (!li) {
      li = el('li');
      li.dataset.id = r.id;
      li.addEventListener('click', () => selecionar(r.id));
    }
    if (lista.children[i] !== li) lista.insertBefore(li, lista.children[i] ?? null);
    li.className = r.id === estado.selecionado ? 'selecionado' : '';
    const tarefa = r.caixa >= 0 ? `caixa ${r.caixa}` : r.estado === 'RECARREGANDO' || r.estado === 'DESLOCANDO_RECARGA' ? 'recarga' : 'livre';
    const html = `<span class="tag">R${r.id}</span>${rotuloEstado(r.estado)}<span class="modo ${r.modo === 'MANUAL' ? 'manual' : ''}">${r.modo === 'MANUAL' ? 'Manual' : 'Auto'}</span>
        <span class="info">${bateria(r.fisico.bateria)}<span>${tarefa}</span><span>${(r.distancia ?? 0).toFixed(0)} m</span>${r.evitando ? '<span class="pill aviso">desviando</span>' : ''}</span>`;
    if (li.dataset.html !== html) {
      li.innerHTML = html;
      li.dataset.html = html;
    }
  });
}

function selecionar(id) {
  estado.selecionado = id;
  cena.selecionar(id);
  const mudou = $('#painel-detalhe').hidden !== (id == null);
  $('#painel-detalhe').hidden = id == null;
  if (mudou) atualizarAreaLivre();
  desenharFrota();
  desenharDetalhe();
}

function desenharDetalhe() {
  const r = robosCombinados().find((x) => x.id === estado.selecionado);
  if (!r) return;
  $('#detalhe-titulo').textContent = `Robô R${r.id}`;
  $('#detalhe-estado').outerHTML = `<span class="estado-rotulo" id="detalhe-estado" style="--cor:${CORES_ESTADO[r.estado]}">${NOMES_ESTADO[r.estado] ?? r.estado}</span>`;
  const f = r.fisico;
  const dados = [
    ['Posição', `(${f.x.toFixed(2)}, ${f.y.toFixed(2)}) m`],
    ['Setpoint', `(${f.sp_x.toFixed(2)}, ${f.sp_y.toFixed(2)}) m`],
    ['Velocidade', `${f.v.toFixed(2)} m/s`],
    ['Bateria', bateria(f.bateria)],
    ['Distância total', `${(r.distancia ?? 0).toFixed(1)} m`],
    ['Missão', r.caixa >= 0 ? `caixa ${r.caixa}, equipe ${r.equipe}` : 'nenhuma'],
    ['Carga', f.atracada ? 'acoplada e erguida' : f.acoplar ? 'acoplando' : 'sem carga'],
    ['Processo', r.pid ? `pid ${r.pid}, slot ${r.slot}` : 'iniciando'],
    ['Supervisão', r.situacao ?? '—'],
    ['Anticolisão', `${r.desvios ?? 0} intervenções`],
    ['T_NAV', r.nav ? `${r.nav.ciclos} ciclos, ${r.nav.perdas} perdas, C máx ${(r.nav.c_max_us / 1000).toFixed(2)} ms` : '—'],
  ];
  $('#detalhe-dados').innerHTML = dados.map(([k, v]) => `<dt>${k}</dt><dd>${v}</dd>`).join('');
  for (const b of document.querySelectorAll('[data-modo]')) b.setAttribute('aria-pressed', String((r.modo === 'MANUAL') === (b.dataset.modo === 'manual')));
  $('#bloco-manual').hidden = r.modo !== 'MANUAL';
  $('#sim-defeito').textContent = f.falha ? 'Remover defeito' : 'Gerar defeito';
  $('#sim-comunicacao').textContent = f.comunicacao === false ? 'Restaurar comunicação' : 'Cortar comunicação';
  const faixa = $('#sim-bateria');
  if (document.activeElement !== faixa && !faixa.dataset.mexido) {
    faixa.value = Math.round(f.bateria);
    $('#sim-bateria-valor').textContent = `${Math.round(f.bateria)}%`;
  }
}

function desenharCargas() {
  const caixas = estado.sup.caixas ?? [];
  const pend = caixas.filter((c) => c.estado !== 'ENTREGUE');
  const entr = caixas.filter((c) => c.estado === 'ENTREGUE').reverse();
  $('#n-cargas').textContent = pend.length ? `(${pend.length})` : '';
  const linha = (c, ...cols) => `<tr><td>Caixa ${c.id}</td><td>${c.tamanho}</td><td>Baia ${c.baia} → Zona ${c.zona}</td>${cols.map((x) => `<td>${x}</td>`).join('')}</tr>`;
  $('#tabela-pendentes').innerHTML = pend
    .map((c) => linha(c, `${NOMES_CAIXA[c.estado] ?? c.estado}${c.aguardando_frota ? ' <span class="pill aviso">aguarda frota</span>' : ''}`, c.estado === 'NA_FILA' ? '—' : `${Math.round(c.espera_s)} s`))
    .join('') || '<tr><td colspan="5">Nenhuma caixa pendente.</td></tr>';
  $('#tabela-entregues').innerHTML = entr.slice(0, 60).map((c) => linha(c, c.robos?.length ? c.robos.map((r) => `R${r}`).join(', ') : '—', c.lead_s ? `${Math.round(c.lead_s)} s` : '—')).join('') || '<tr><td colspan="5">Nenhuma entrega ainda.</td></tr>';
}

function desenharDesempenho() {
  const k = estado.sup.kpi ?? {};
  const robos = estado.sup.robos ?? [];
  const perdas = robos.reduce((a, r) => a + (r.nav?.perdas ?? 0) + (r.status?.perdas ?? 0), 0);
  const cmax = robos.reduce((a, r) => Math.max(a, r.nav?.c_max_us ?? 0), 0);
  const indicadores = [
    [k.entregues ?? 0, 'caixas entregues'],
    [(k.taxa_min ?? 0).toFixed(1), 'caixas por minuto (últimos 5 min)'],
    [k.lead_medio_s ? `${Math.round(k.lead_medio_s)} s` : '—', 'tempo médio da baia ao destino'],
    [k.desvios ?? 0, 'intervenções da anticolisão'],
    [estado.sim.colisoes ?? 0, 'colisões no simulador'],
    [perdas, 'prazos perdidos (T_NAV + T_STATUS)'],
    [`${(cmax / 1000).toFixed(2)} ms`, 'maior tempo de execução da T_NAV'],
    [k.perdas_registro ?? 0, 'registros perdidos pelo coletor'],
  ];
  $('#indicadores').innerHTML = indicadores.map(([v, t]) => `<div class="indicador"><strong>${v}</strong><span>${t}</span></div>`).join('');
  $('#tabela-rt').innerHTML = robos
    .map((r) => `<tr><td>R${r.id}</td><td>${r.nav.ciclos}</td><td>${r.nav.perdas + r.status.perdas}</td><td>${(r.nav.c_max_us / 1000).toFixed(2)} ms</td><td>${(r.nav.lat_max_us / 1000).toFixed(2)} ms</td><td>${(r.status.c_max_us / 1000).toFixed(2)} ms</td><td>${r.desvios}</td></tr>`)
    .join('');
  $('#tabela-passagens').innerHTML = (estado.sup.passagens ?? [])
    .map((p) => `<tr><td>${p.nome}</td><td>${p.ocupacao} de ${p.capacidade}${p.ocupantes.length ? ` (R${p.ocupantes.join(', R')})` : ''}</td><td>${p.entradas}</td><td>${p.espera_media_s.toFixed(1)} s</td><td>${p.esperador >= 0 ? `R${p.esperador}` : '—'}</td></tr>`)
    .join('');
}

function desenharEventos() {
  $('#lista-eventos').innerHTML = estado.eventos
    .slice(0, 300)
    .map((e) => `<li><time>${(e.t ?? '').slice(11, 19)}</time><span class="cat cat-${e.categoria}">${e.origem}</span><span>${e.texto}${e.robos?.length ? ` — robôs ${e.robos.join(', ')}` : ''}</span></li>`)
    .join('');
}

function desenharAlarmes() {
  $('#n-alarmes').textContent = estado.alarmes.length ? `(${estado.alarmes.length})` : '';
  $('#lista-alarmes').classList.add('alarmes');
  $('#lista-alarmes').innerHTML = estado.alarmes.map((a) => `<li><time>${(a.t ?? '').slice(11, 19)}</time><span>${a.texto}</span></li>`).join('');
}

let temporizadorAviso;
function avisar(texto) {
  const t = $('#toast');
  t.textContent = texto;
  t.classList.add('visivel');
  clearTimeout(temporizadorAviso);
  temporizadorAviso = setTimeout(() => t.classList.remove('visivel'), 3500);
}

function aguardarPiso(texto, callback) {
  const aviso = $('#aviso-piso');
  aviso.textContent = `${texto} (Esc cancela)`;
  aviso.hidden = false;
  cena.pedirPonto((p) => {
    aviso.hidden = true;
    callback(p);
  });
}
document.addEventListener('keydown', (e) => {
  if (e.key === 'Escape') {
    cena.pedirPonto(null);
    $('#aviso-piso').hidden = true;
  }
});

function preencherLocais() {
  const m = estado.mapa;
  const opcoes = [
    ...m.baias.map((b) => [`baia:${b.id}`, `Baia ${b.id}`]),
    ...m.zonas.map((z) => [`zona:${z.id}`, `Zona ${z.id}`]),
    ...m.estacoes_recarga.map((_, i) => [`recarga:${i}`, `Recarga ${i + 1}`]),
  ];
  $('#destino-local').innerHTML = opcoes.map(([v, t]) => `<option value="${v}">${t}</option>`).join('');
  $('#caixa-baia').innerHTML = m.baias.map((b) => `<option value="${b.id}">Baia ${b.id}</option>`).join('');
  $('#caixa-zona').innerHTML = m.zonas.map((z) => `<option value="${z.id}">Zona ${z.id}</option>`).join('');
  $('#subtitulo').textContent = m.semente != null ? `Mapa aleatório, semente ${m.semente}` : 'Operação e simulação da frota';
}

// ---------------------------------------------------------------- ações

$('#parada-geral').addEventListener('click', () => operador(estado.sup.sistema?.parada_geral ? 'liberar_parada' : 'parada_geral'));
$('#chave-anticolisao').addEventListener('change', (e) => operador('anticolisao', { ativo: e.target.checked }));
$('#chave-rotas-adaptativas').addEventListener('change', (e) => operador('rotas_adaptativas', { ativo: e.target.checked }));
$('#novo-robo').addEventListener('click', () => {
  simulacao('novo_robo');
  avisar('Robô novo disponibilizado no estacionamento');
});
$('#novo-robo-local').addEventListener('click', () => aguardarPiso('Clique no piso onde o robô novo deve aparecer', (p) => simulacao('novo_robo', p)));
$('#fechar-detalhe').addEventListener('click', () => selecionar(null));

for (const b of document.querySelectorAll('[data-modo]'))
  b.addEventListener('click', () => estado.selecionado != null && operador('modo', { robo: estado.selecionado, modo: b.dataset.modo }));
for (const b of document.querySelectorAll('[data-cmd]'))
  b.addEventListener('click', () => {
    if (estado.selecionado == null) return;
    operador(b.dataset.cmd, { robo: estado.selecionado });
    avisar(`${b.textContent} enviado ao robô R${estado.selecionado}`);
  });
$('#enviar-destino').addEventListener('click', () => {
  const [tipo, id] = $('#destino-local').value.split(':');
  operador('destino', { robo: estado.selecionado, local: { tipo, id: Number(id) } });
  avisar(`Destino enviado ao robô R${estado.selecionado}`);
});
$('#destino-no-piso').addEventListener('click', () =>
  aguardarPiso(`Clique no piso para o destino do robô R${estado.selecionado}`, (p) => {
    operador('destino', { robo: estado.selecionado, x: p.x, y: p.y });
    avisar(`Destino (${p.x.toFixed(1)}, ${p.y.toFixed(1)}) enviado ao robô R${estado.selecionado}`);
  })
);
$('#sim-defeito').addEventListener('click', () => {
  const f = robosCombinados().find((x) => x.id === estado.selecionado)?.fisico;
  simulacao('defeito', { robo: estado.selecionado, ativo: !f?.falha });
});
$('#sim-comunicacao').addEventListener('click', () => {
  const f = robosCombinados().find((x) => x.id === estado.selecionado)?.fisico;
  simulacao('comunicacao', { robo: estado.selecionado, ativo: f?.comunicacao === false });
});
$('#sim-bateria').addEventListener('input', (e) => {
  e.target.dataset.mexido = '1';
  $('#sim-bateria-valor').textContent = `${e.target.value}%`;
});
$('#sim-aplicar-bateria').addEventListener('click', () => {
  const faixa = $('#sim-bateria');
  simulacao('bateria', { robo: estado.selecionado, nivel: Number(faixa.value) });
  delete faixa.dataset.mexido;
  avisar(`Bateria do robô R${estado.selecionado} ajustada para ${faixa.value}%`);
});
$('#form-caixa').addEventListener('submit', (e) => {
  e.preventDefault();
  const dados = { baia: Number($('#caixa-baia').value), tamanho: Number($('#caixa-tamanho').value), zona: Number($('#caixa-zona').value) };
  simulacao('nova_caixa', dados);
  avisar(`Caixa de tamanho ${dados.tamanho} incluída na Baia ${dados.baia}`);
});

for (const b of document.querySelectorAll('[data-camera]'))
  b.addEventListener('click', () => {
    if (b.dataset.camera === 'seguir' && estado.selecionado == null) {
      avisar('Selecione um robô para a câmera seguir');
      return;
    }
    for (const o of document.querySelectorAll('[data-camera]')) o.setAttribute('aria-pressed', String(o === b));
    cena.definirCamera(b.dataset.camera);
  });
$('#chave-rotas').addEventListener('change', (e) => cena.alternarRotas(e.target.checked));
$('#chave-rotulos').addEventListener('change', (e) => cena.alternarRotulos(e.target.checked));

for (const aba of document.querySelectorAll('[role="tab"]'))
  aba.addEventListener('click', () => {
    for (const a of document.querySelectorAll('[role="tab"]')) a.setAttribute('aria-selected', String(a === aba));
    for (const c of document.querySelectorAll('[data-conteudo]')) c.hidden = c.dataset.conteudo !== aba.dataset.aba;
    if (document.body.classList.contains('gaveta-recolhida')) $('#recolher').click();
  });
$('#recolher').addEventListener('click', () => {
  document.body.classList.toggle('gaveta-recolhida');
  $('#gaveta').classList.toggle('recolhida');
  setTimeout(atualizarAreaLivre, 50);
});
