// Acompanha o sistema pelo broker e imprime um resumo periódico (usado nos testes de escalabilidade).
//   node interface/ferramentas/acompanhar.mjs [--intervalo-ms 10000] [--duracao-ms 60000] [--json]
import mqtt from 'mqtt';

const args = Object.fromEntries(
  process.argv.slice(2).reduce((acc, a, i, arr) => (a.startsWith('--') ? [...acc, [a.slice(2), arr[i + 1]?.startsWith('--') || arr[i + 1] == null ? true : arr[i + 1]]] : acc), [])
);
const intervalo = Number(args['intervalo-ms'] ?? 10000);
const duracao = Number(args['duracao-ms'] ?? 60000);
const c = mqtt.connect(args.broker ?? 'mqtt://127.0.0.1:1883');
let sup;
let sim;
let colisoesInicio = null;
const t0 = Date.now();
c.on('connect', () => c.subscribe(['amr/sup/estado', 'amr/sim/estado']));
c.on('message', (t, d) => {
  const m = JSON.parse(d);
  if (t === 'amr/sup/estado') sup = m;
  else {
    sim = m;
    if (colisoesInicio == null) colisoesInicio = m.colisoes;
  }
});

function resumo() {
  const robos = sup?.robos ?? [];
  const estados = {};
  for (const r of robos) estados[r.estado] = (estados[r.estado] ?? 0) + 1;
  const max = (f) => robos.reduce((a, r) => Math.max(a, f(r)), 0);
  const soma = (f) => robos.reduce((a, r) => a + f(r), 0);
  return {
    t_s: Math.round((Date.now() - t0) / 1000),
    robos: robos.length,
    entregues: sup?.kpi?.entregues ?? 0,
    colisoes: (sim?.colisoes ?? 0) - (colisoesInicio ?? 0),
    desvios: sup?.kpi?.desvios ?? 0,
    nav_perdas: soma((r) => r.nav.perdas),
    status_perdas: soma((r) => r.status.perdas),
    nav_c_max_ms: +(max((r) => r.nav.c_max_us) / 1000).toFixed(3),
    nav_atraso_max_ms: +(max((r) => r.nav.lat_max_us) / 1000).toFixed(3),
    status_c_max_ms: +(max((r) => r.status.c_max_us) / 1000).toFixed(3),
    status_atraso_max_ms: +(max((r) => r.status.lat_max_us) / 1000).toFixed(3),
    sim_tick_max_ms: sim?.tick_max_ms ?? 0,
    estados,
  };
}

const iv = setInterval(() => {
  if (sup && !args.json) console.log(JSON.stringify(resumo()));
}, intervalo);
setTimeout(() => {
  clearInterval(iv);
  if (args.json) console.log(JSON.stringify(resumo()));
  c.end(true);
  process.exit(0);
}, duracao);
