// Cena 3D do galpão. O plano do problema é 2D (x, y em metros); aqui x vira X e y vira -Z,
// para que a vista de planta mostre o mapa na mesma orientação do arquivo de cenário.

import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import { CSS2DRenderer, CSS2DObject } from 'three/addons/renderers/CSS2DRenderer.js';
import { RoundedBoxGeometry } from 'three/addons/geometries/RoundedBoxGeometry.js';

export const CORES_ESTADO = {
  INICIALIZANDO: '#8a96a0',
  DISPONIVEL: '#3fa36b',
  DESLOCANDO: '#4a8fd6',
  AGUARDANDO_PASSAGEM: '#2bb3c0',
  AGUARDANDO_EQUIPE: '#e58a2b',
  ACOPLANDO: '#e58a2b',
  TRANSPORTANDO: '#e58a2b',
  DESACOPLANDO: '#e58a2b',
  ABORTANDO: '#d9473f',
  DESLOCANDO_RECARGA: '#9b7be0',
  RECARREGANDO: '#9b7be0',
  DEFEITO: '#d9473f',
  SEM_COMUNICACAO: '#8a96a0',
};

const PX_POR_M = 48;
const ALTURA_CARGA = 0.42;

function dimensoesCaixa(tamanho) {
  return [
    [0.42, 0.34, 0.42],
    [0.82, 0.38, 0.46],
    [0.86, 0.4, 0.82],
    [1.02, 0.42, 1.02],
  ][Math.max(0, Math.min(3, tamanho - 1))];
}

function texturaCaixa() {
  const c = document.createElement('canvas');
  c.width = c.height = 128;
  const g = c.getContext('2d');
  g.fillStyle = '#c69b62';
  g.fillRect(0, 0, 128, 128);
  for (let i = 0; i < 400; i++) {
    g.fillStyle = `rgba(90,60,25,${Math.random() * 0.08})`;
    g.fillRect(Math.random() * 128, Math.random() * 128, 2, 2);
  }
  g.fillStyle = '#a8814e';
  g.fillRect(54, 0, 20, 128);
  g.strokeStyle = 'rgba(70,45,20,0.35)';
  g.lineWidth = 3;
  g.strokeRect(2, 2, 124, 124);
  const t = new THREE.CanvasTexture(c);
  t.colorSpace = THREE.SRGBColorSpace;
  return t;
}

export class Cena {
  constructor(container, { aoSelecionarRobo, aoClicarPiso }) {
    this.container = container;
    this.aoSelecionarRobo = aoSelecionarRobo;
    this.aoClicarPiso = aoClicarPiso;
    this.robos = new Map();
    this.caixas = new Map();
    this.rotas = new Map();
    this.passagens = [];
    this.estacoes = [];
    this.selecionado = null;
    this.pedidoPonto = null;
    this.modo = 'perspectiva';
    this.mostrarRotas = true;
    this.relogio = new THREE.Clock();
    this.materialCaixa = new THREE.MeshStandardMaterial({ map: texturaCaixa(), roughness: 0.85 });

    this.renderer = new THREE.WebGLRenderer({ antialias: true, powerPreference: 'high-performance' });
    this.renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
    this.renderer.shadowMap.enabled = true;
    this.renderer.shadowMap.type = THREE.PCFSoftShadowMap;
    this.renderer.toneMapping = THREE.ACESFilmicToneMapping;
    this.renderer.toneMappingExposure = 1.05;
    container.appendChild(this.renderer.domElement);

    this.rotulos = new CSS2DRenderer();
    this.rotulos.domElement.style.position = 'absolute';
    this.rotulos.domElement.style.inset = '0';
    this.rotulos.domElement.style.pointerEvents = 'none';
    container.appendChild(this.rotulos.domElement);

    this.cena = new THREE.Scene();
    this.cena.background = new THREE.Color('#2f363d');
    this.cena.fog = new THREE.Fog('#2f363d', 60, 140);

    this.camPerspectiva = new THREE.PerspectiveCamera(38, 1, 0.1, 400);
    this.camPlanta = new THREE.OrthographicCamera(-1, 1, 1, -1, 0.1, 200);
    this.controles = new OrbitControls(this.camPerspectiva, this.renderer.domElement);
    this.controles.enableDamping = true;
    this.controles.maxPolarAngle = Math.PI * 0.47;
    this.controles.minDistance = 4;
    this.controles.maxDistance = 90;
    this.controlesPlanta = new OrbitControls(this.camPlanta, this.renderer.domElement);
    this.controlesPlanta.enableRotate = false;
    this.controlesPlanta.screenSpacePanning = true;
    this.controlesPlanta.enabled = false;

    this.raycaster = new THREE.Raycaster();
    this.ponteiro = new THREE.Vector2();
    this.piso = null;
    this.renderer.domElement.addEventListener('pointerdown', (e) => (this.inicioClique = [e.clientX, e.clientY]));
    this.renderer.domElement.addEventListener('pointerup', (e) => this.clique(e));
    window.addEventListener('resize', () => this.redimensionar());
    this.redimensionar();
    this.animar();
  }

  // ---------------------------------------------------------------- coordenadas

  mundo(x, y, h = 0) {
    return new THREE.Vector3(x - this.L / 2, h, this.A / 2 - y);
  }

  mapa(v) {
    return { x: v.x + this.L / 2, y: this.A / 2 - v.z };
  }

  // ---------------------------------------------------------------- construção do galpão

  async construirMapa(mapa) {
    if (this.grupoMapa) {
      this.cena.remove(this.grupoMapa);
      for (const r of this.robos.values()) this.cena.remove(r.grupo);
      this.robos.clear();
      for (const c of this.caixas.values()) this.cena.remove(c);
      this.caixas.clear();
    }
    this.dados = mapa;
    this.L = mapa.galpao.largura;
    this.A = mapa.galpao.altura;
    this.grupoMapa = new THREE.Group();
    this.cena.add(this.grupoMapa);
    await document.fonts.load('700 40px "Barlow Condensed"').catch(() => {});

    this.criarLuzes();
    this.criarPiso();
    this.criarObstaculos();
    this.criarPassagens();
    this.criarEstacoes();
    this.criarPerimetro();
    this.enquadrar();
  }

  criarLuzes() {
    const g = this.grupoMapa;
    g.add(new THREE.HemisphereLight('#e4ecf2', '#5f5a50', 1.1));
    const sol = new THREE.DirectionalLight('#fff4e0', 2.2);
    sol.position.set(-this.L * 0.35, 30, this.A * 0.55);
    sol.castShadow = true;
    sol.shadow.mapSize.set(2048, 2048);
    const s = Math.max(this.L, this.A) * 0.62;
    Object.assign(sol.shadow.camera, { left: -s, right: s, top: s, bottom: -s, near: 1, far: 90 });
    sol.shadow.bias = -0.0005;
    sol.shadow.normalBias = 0.02;
    g.add(sol);
    // luminárias de galpão: luz fria e baixa vinda de cima
    for (let x = 6; x < this.L; x += 12) {
      for (let y = 6; y < this.A; y += 12) {
        const l = new THREE.PointLight('#dbe7ff', 18, 16, 1.6);
        l.position.copy(this.mundo(x, y, 7));
        g.add(l);
      }
    }
  }

  criarPiso() {
    const { L, A, dados } = this;
    const c = document.createElement('canvas');
    c.width = L * PX_POR_M;
    c.height = A * PX_POR_M;
    const g = c.getContext('2d');
    const px = (m) => m * PX_POR_M;
    const py = (m) => (A - m) * PX_POR_M;  // y do mapa cresce para cima

    g.fillStyle = '#b8bbb4';
    g.fillRect(0, 0, c.width, c.height);
    for (let i = 0; i < L * A * 30; i++) {
      const tom = Math.random() < 0.5 ? '0,0,0' : '255,255,255';
      g.fillStyle = `rgba(${tom},${Math.random() * 0.06})`;
      g.fillRect(Math.random() * c.width, Math.random() * c.height, 1 + Math.random() * 2, 1 + Math.random() * 2);
    }
    g.strokeStyle = 'rgba(40,40,40,0.06)';
    g.lineWidth = 1;
    for (let x = 0; x <= L; x++) { g.beginPath(); g.moveTo(px(x), 0); g.lineTo(px(x), c.height); g.stroke(); }
    for (let y = 0; y <= A; y++) { g.beginPath(); g.moveTo(0, py(y)); g.lineTo(c.width, py(y)); g.stroke(); }
    g.strokeStyle = 'rgba(30,30,30,0.18)';
    g.lineWidth = 2;
    for (let x = 0; x <= L; x += 6) { g.beginPath(); g.moveTo(px(x), 0); g.lineTo(px(x), c.height); g.stroke(); }
    for (let y = 0; y <= A; y += 6) { g.beginPath(); g.moveTo(0, py(y)); g.lineTo(c.width, py(y)); g.stroke(); }

    const estencil = (texto, x, y, cor, tam = 0.55, rot = 0) => {
      g.save();
      g.translate(px(x), py(y));
      g.rotate(rot);
      g.fillStyle = cor;
      g.font = `700 ${px(tam)}px "Barlow Condensed", sans-serif`;
      g.textAlign = 'center';
      g.textBaseline = 'middle';
      g.globalAlpha = 0.85;
      g.fillText(texto, 0, 0);
      g.restore();
    };
    const contorno = (x, y, w, h, cor, larg = 0.08, tracejado = null) => {
      g.save();
      g.strokeStyle = cor;
      g.lineWidth = px(larg);
      if (tracejado) g.setLineDash(tracejado.map(px));
      g.strokeRect(px(x), py(y + h), px(w), px(h));
      g.restore();
    };

    // faixa amarela de circulação ao longo do perímetro
    contorno(0.25, 0.25, L - 0.5, A - 0.5, '#e6b72a', 0.1);

    for (const b of dados.baias ?? []) {
      g.fillStyle = 'rgba(230,183,42,0.16)';
      g.fillRect(px(b.x - 3.2), py(b.y + 1.1), px(4.4), px(2.2));
      contorno(b.x - 3.2, b.y - 1.1, 4.4, 2.2, '#e6b72a', 0.09);
      estencil(`BAIA ${b.id}`, b.x - 2.4, b.y, '#a8811a', 0.55, -Math.PI / 2);
    }
    for (const z of dados.zonas ?? []) {
      g.save();
      g.beginPath();
      g.rect(px(z.x - 1.6), py(z.y + 1.6), px(3.2), px(3.2));
      g.clip();
      g.strokeStyle = 'rgba(47,143,134,0.35)';
      g.lineWidth = px(0.06);
      for (let k = -4; k < 8; k++) { g.beginPath(); g.moveTo(px(z.x - 1.6 + k * 0.5), py(z.y - 1.6)); g.lineTo(px(z.x - 1.6 + k * 0.5 + 3.2), py(z.y + 1.6)); g.stroke(); }
      g.restore();
      contorno(z.x - 1.6, z.y - 1.6, 3.2, 3.2, '#2f8f86', 0.1);
      estencil(`ZONA ${z.id}`, z.x + 2.3, z.y, '#22706a', 0.55, Math.PI / 2);
    }
    const e = dados.estacionamento;
    if (e) {
      for (let x = e.x; x < e.x + e.w; x++) for (let y = e.y; y < e.y + e.h; y++) contorno(x + 0.08, y + 0.08, 0.84, 0.84, 'rgba(250,250,250,0.55)', 0.035, [0.12, 0.08]);
      estencil('ESTACIONAMENTO', e.x + e.w / 2, e.y - 0.45, 'rgba(250,250,250,0.6)', 0.45);
    }
    for (const r of dados.estacoes_recarga ?? []) {
      g.fillStyle = 'rgba(155,123,224,0.28)';
      g.fillRect(px(r.x - 0.45), py(r.y + 0.45), px(0.9), px(0.9));
      contorno(r.x - 0.45, r.y - 0.45, 0.9, 0.9, '#8e6fd6', 0.05);
    }
    for (const p of dados.passagens ?? []) {
      g.save();
      g.beginPath();
      g.rect(px(p.x - 0.6), py(p.y + p.h), px(p.w + 1.2), px(p.h));
      g.clip();
      for (let k = -10; k < 30; k++) {
        g.fillStyle = k % 2 ? '#1d1d1d' : '#e6b72a';
        g.beginPath();
        const x0 = px(p.x - 0.6) + k * px(0.25);
        g.moveTo(x0, py(p.y));
        g.lineTo(x0 + px(0.25), py(p.y));
        g.lineTo(x0 + px(0.25) + px(p.h), py(p.y + p.h));
        g.lineTo(x0 + px(p.h), py(p.y + p.h));
        g.fill();
      }
      g.restore();
    }

    const tex = new THREE.CanvasTexture(c);
    tex.colorSpace = THREE.SRGBColorSpace;
    tex.anisotropy = this.renderer.capabilities.getMaxAnisotropy();
    const piso = new THREE.Mesh(new THREE.PlaneGeometry(L, A), new THREE.MeshStandardMaterial({ map: tex, roughness: 0.92, metalness: 0.02 }));
    piso.rotation.x = -Math.PI / 2;
    piso.receiveShadow = true;
    this.grupoMapa.add(piso);
    this.piso = piso;

    // piso externo, para o galpão não flutuar no vazio
    const externo = new THREE.Mesh(new THREE.PlaneGeometry(L * 4, A * 4), new THREE.MeshStandardMaterial({ color: '#3a4047', roughness: 1 }));
    externo.rotation.x = -Math.PI / 2;
    externo.position.y = -0.02;
    externo.receiveShadow = true;
    this.grupoMapa.add(externo);
  }

  ehParede(o) {
    return o.h >= this.A * 0.5 || o.w >= this.L * 0.5;
  }

  criarObstaculos() {
    const concreto = new THREE.MeshStandardMaterial({ color: '#8d9497', roughness: 0.9 });
    const faixa = new THREE.MeshStandardMaterial({ color: '#e6b72a', roughness: 0.6 });
    const montante = new THREE.MeshStandardMaterial({ color: '#2e5d9a', roughness: 0.5, metalness: 0.4 });
    const longarina = new THREE.MeshStandardMaterial({ color: '#e07a1f', roughness: 0.5, metalness: 0.3 });
    const passagens = this.dados.passagens ?? [];
    const livre = (x, y) => passagens.some((p) => x >= p.x && x < p.x + p.w && y >= p.y && y < p.y + p.h);

    for (const o of this.dados.obstaculos ?? []) {
      if (this.ehParede(o)) {
        // parede contínua, interrompida nas passagens: um bloco por trecho
        const vertical = o.h >= o.w;
        const n = vertical ? o.h : o.w;
        let inicio = 0;
        for (let i = 0; i <= n; i++) {
          const cx = vertical ? o.x : o.x + i;
          const cy = vertical ? o.y + i : o.y;
          const aberta = i === n || livre(cx, cy);
          if (aberta) {
            if (i > inicio) {
              const w = vertical ? o.w : i - inicio;
              const h = vertical ? i - inicio : o.h;
              const x0 = vertical ? o.x : o.x + inicio;
              const y0 = vertical ? o.y + inicio : o.y;
              const bloco = new THREE.Mesh(new THREE.BoxGeometry(w, 1.5, h), concreto);
              bloco.position.copy(this.mundo(x0 + w / 2, y0 + h / 2, 0.75));
              bloco.castShadow = bloco.receiveShadow = true;
              this.grupoMapa.add(bloco);
              const rodape = new THREE.Mesh(new THREE.BoxGeometry(w + 0.02, 0.18, h + 0.02), faixa);
              rodape.position.copy(this.mundo(x0 + w / 2, y0 + h / 2, 0.09));
              this.grupoMapa.add(rodape);
            }
            inicio = i + 1;
          }
        }
        continue;
      }
      // prateleira porta-paletes: montantes azuis, longarinas laranja e caixas em dois níveis
      const grupo = new THREE.Group();
      const centro = this.mundo(o.x + o.w / 2, o.y + o.h / 2);
      grupo.position.copy(centro);
      const w = o.w - 0.1;
      const d = o.h - 0.1;
      for (const sx of [-1, 1])
        for (const sz of [-1, 1]) {
          const m = new THREE.Mesh(new THREE.BoxGeometry(0.07, 2.1, 0.07), montante);
          m.position.set((sx * w) / 2, 1.05, (sz * d) / 2);
          m.castShadow = true;
          grupo.add(m);
        }
      for (const nivel of [0.15, 1.05, 1.95]) {
        for (const sz of [-1, 1]) {
          const lg = new THREE.Mesh(new THREE.BoxGeometry(o.w >= o.h ? w : 0.06, 0.08, o.w >= o.h ? 0.06 : d), longarina);
          if (o.w >= o.h) lg.position.set(0, nivel, (sz * d) / 2);
          else lg.position.set((sz * w) / 2, nivel, 0);
          grupo.add(lg);
        }
        if (nivel < 1.9) {
          const comprimento = Math.max(o.w, o.h);
          for (let k = 0; k < comprimento; k++) {
            if (Math.random() < 0.2) continue;
            const t = 0.45 + Math.random() * 0.3;
            const cx = new THREE.Mesh(new THREE.BoxGeometry(t, 0.3 + Math.random() * 0.4, t), this.materialCaixa);
            const ao = -comprimento / 2 + k + 0.5;
            cx.position.set(o.w >= o.h ? ao : 0, nivel + 0.05 + cx.geometry.parameters.height / 2, o.w >= o.h ? 0 : ao);
            cx.rotation.y = (Math.random() - 0.5) * 0.2;
            cx.castShadow = cx.receiveShadow = true;
            grupo.add(cx);
          }
        }
      }
      this.grupoMapa.add(grupo);
    }
  }

  criarPassagens() {
    this.passagens = [];
    const aco = new THREE.MeshStandardMaterial({ color: '#2b2f33', roughness: 0.4, metalness: 0.6 });
    for (const p of this.dados.passagens ?? []) {
      const g = new THREE.Group();
      const centro = this.mundo(p.x + p.w / 2, p.y + p.h / 2);
      g.position.copy(centro);
      const meia = p.h / 2 + 0.08;
      for (const s of [-1, 1]) {
        const poste = new THREE.Mesh(new THREE.BoxGeometry(0.12, 2.4, 0.12), aco);
        poste.position.set(0, 1.2, s * meia);
        poste.castShadow = true;
        g.add(poste);
      }
      const viga = new THREE.Mesh(new THREE.BoxGeometry(0.14, 0.14, p.h + 0.3), aco);
      viga.position.set(0, 2.4, 0);
      g.add(viga);
      const lampadas = [];
      for (let i = 0; i < p.capacidade; i++) {
        const l = new THREE.Mesh(new THREE.SphereGeometry(0.07, 12, 8), new THREE.MeshStandardMaterial({ color: '#1f2a1f', emissive: '#3fa36b', emissiveIntensity: 1.6 }));
        l.position.set(0.1, 2.25, (i - (p.capacidade - 1) / 2) * 0.2);
        g.add(l);
        lampadas.push(l);
      }
      const placa = document.createElement('div');
      placa.className = 'rotulo-placa';
      placa.textContent = `${p.nome} · 0/${p.capacidade}`;
      const rot = new CSS2DObject(placa);
      rot.position.set(0, 2.8, 0);
      g.add(rot);
      this.grupoMapa.add(g);
      this.passagens.push({ dados: p, lampadas, placa });
    }
  }

  criarEstacoes() {
    this.estacoes = [];
    const base = new THREE.MeshStandardMaterial({ color: '#3b3f46', roughness: 0.6, metalness: 0.3 });
    for (const r of this.dados.estacoes_recarga ?? []) {
      const g = new THREE.Group();
      g.position.copy(this.mundo(r.x, r.y));
      const lado = r.x < this.L / 2 ? -1 : 1;
      const poste = new THREE.Mesh(new RoundedBoxGeometry(0.22, 0.9, 0.34, 2, 0.04), base);
      poste.position.set(lado * 0.42, 0.45, 0);
      poste.castShadow = true;
      g.add(poste);
      const tela = new THREE.Mesh(new THREE.PlaneGeometry(0.16, 0.2), new THREE.MeshStandardMaterial({ color: '#111', emissive: '#9b7be0', emissiveIntensity: 1.2 }));
      tela.position.set(lado * 0.42 - lado * 0.115, 0.65, 0);
      tela.rotation.y = lado > 0 ? -Math.PI / 2 : Math.PI / 2;
      g.add(tela);
      this.grupoMapa.add(g);
      this.estacoes.push({ dados: r, tela });
    }
  }

  criarPerimetro() {
    const { L, A } = this;
    const parede = new THREE.MeshStandardMaterial({ color: '#7e868c', roughness: 0.95 });
    const porta = new THREE.MeshStandardMaterial({ color: '#4d565d', roughness: 0.7, metalness: 0.2 });
    const moldura = new THREE.MeshStandardMaterial({ color: '#e6b72a', roughness: 0.6 });
    // paredes do fundo (norte e oeste); os outros lados ficam abertos para a câmera
    const norte = new THREE.Mesh(new THREE.BoxGeometry(L + 0.6, 3.2, 0.3), parede);
    norte.position.copy(this.mundo(L / 2, A + 0.15, 1.6));
    norte.receiveShadow = true;
    this.grupoMapa.add(norte);
    const oeste = new THREE.Mesh(new THREE.BoxGeometry(0.3, 3.2, A + 0.6), parede);
    oeste.position.copy(this.mundo(-0.15, A / 2, 1.6));
    oeste.receiveShadow = true;
    this.grupoMapa.add(oeste);
    // portas de doca atrás de cada baia
    for (const b of this.dados.baias ?? []) {
      const d = new THREE.Mesh(new THREE.BoxGeometry(0.06, 2.6, 2.4), porta);
      d.position.copy(this.mundo(0.03, b.y, 1.3));
      this.grupoMapa.add(d);
      const m = new THREE.Mesh(new THREE.BoxGeometry(0.08, 2.8, 2.6), moldura);
      m.position.copy(this.mundo(0.01, b.y, 1.4));
      this.grupoMapa.add(m);
    }
  }

  enquadrar() {
    const { L, A } = this;
    this.camPerspectiva.position.set(-L * 0.04, Math.max(L, A) * 1.2, A * 1.12);
    this.controles.target.set(0, 0, -A * 0.02);
    this.controles.update();
    this.camPlanta.position.set(0, 60, 0);
    this.camPlanta.up.set(0, 0, -1);
    this.camPlanta.lookAt(0, 0, 0);
    this.controlesPlanta.target.set(0, 0, 0);
    this.redimensionar();
  }

  // ---------------------------------------------------------------- robôs

  criarRobo(id) {
    const grupo = new THREE.Group();
    const corpo = new THREE.Group();
    grupo.add(corpo);
    const chassi = new THREE.Mesh(new RoundedBoxGeometry(0.56, 0.16, 0.44, 3, 0.05), new THREE.MeshStandardMaterial({ color: '#2a3036', roughness: 0.55, metalness: 0.35 }));
    chassi.position.y = 0.11;
    const led = new THREE.Mesh(new RoundedBoxGeometry(0.575, 0.025, 0.455, 2, 0.01), new THREE.MeshStandardMaterial({ color: '#111', emissive: '#3fa36b', emissiveIntensity: 2.2 }));
    led.position.y = 0.075;
    const deck = new THREE.Mesh(new RoundedBoxGeometry(0.48, 0.035, 0.36, 2, 0.012), new THREE.MeshStandardMaterial({ color: '#59636b', roughness: 0.5, metalness: 0.3 }));
    deck.position.y = 0.205;
    const elevador = new THREE.Mesh(new THREE.BoxGeometry(0.38, 0.03, 0.28), new THREE.MeshStandardMaterial({ color: '#c9ced1', roughness: 0.35, metalness: 0.7 }));
    elevador.position.y = 0.235;
    const lidar = new THREE.Mesh(new THREE.CylinderGeometry(0.045, 0.05, 0.06, 16), new THREE.MeshStandardMaterial({ color: '#15181b', roughness: 0.3, metalness: 0.5 }));
    lidar.position.set(0.2, 0.25, 0);
    const frente = new THREE.Mesh(new THREE.BoxGeometry(0.02, 0.05, 0.3), new THREE.MeshStandardMaterial({ color: '#111', emissive: '#f2c230', emissiveIntensity: 1.2 }));
    frente.position.set(0.285, 0.12, 0);
    const rodas = new THREE.MeshStandardMaterial({ color: '#111', roughness: 0.8 });
    for (const sx of [-0.17, 0.17])
      for (const sz of [-0.2, 0.2]) {
        const r = new THREE.Mesh(new THREE.CylinderGeometry(0.05, 0.05, 0.04, 14), rodas);
        r.rotation.x = Math.PI / 2;
        r.position.set(sx, 0.05, sz);
        corpo.add(r);
      }
    for (const m of [chassi, deck, elevador, lidar]) {
      m.castShadow = true;
      m.receiveShadow = true;
    }
    corpo.add(chassi, led, deck, elevador, lidar, frente);
    for (const m of [chassi, led, deck, elevador, lidar, frente]) m.userData.robo = id;

    const anel = new THREE.Mesh(new THREE.RingGeometry(0.42, 0.5, 40), new THREE.MeshBasicMaterial({ color: '#f2c230', transparent: true, opacity: 0.9 }));
    anel.rotation.x = -Math.PI / 2;
    anel.position.y = 0.012;
    anel.visible = false;
    grupo.add(anel);

    const alvo = new THREE.Mesh(new THREE.TorusGeometry(0.16, 0.025, 8, 24), new THREE.MeshBasicMaterial({ color: '#f2c230' }));
    alvo.rotation.x = -Math.PI / 2;
    alvo.visible = false;
    this.cena.add(alvo);

    const div = document.createElement('div');
    div.className = 'rotulo-robo';
    div.innerHTML = `<span class="nome">R${id}</span><span class="bateria"><i><b></b></i></span><span class="alerta"></span>`;
    div.addEventListener('click', () => this.aoSelecionarRobo?.(id));
    const rotulo = new CSS2DObject(div);
    rotulo.position.set(0, 0.75, 0);
    grupo.add(rotulo);

    this.cena.add(grupo);
    const r = { id, grupo, corpo, led, elevador, lidar, anel, alvo, div, rotulo, pos: null, destino: null, theta: 0, thetaDestino: 0, elevacao: 0, acoplar: false, estado: 'INICIALIZANDO', ultimaVez: 0 };
    this.robos.set(id, r);
    return r;
  }

  atualizarFisico(sim) {
    const vistos = new Set();
    for (const s of sim.robos ?? []) {
      const r = this.robos.get(s.id) ?? this.criarRobo(s.id);
      vistos.add(s.id);
      r.destino = this.mundo(s.x, s.y);
      if (!r.pos) r.pos = r.destino.clone();
      r.thetaDestino = s.theta;
      r.acoplar = s.acoplar;
      r.falha = s.falha;
      r.bateria = s.bateria;
      r.sp = this.mundo(s.sp_x, s.sp_y, 0.03);
      r.comunicacao = s.comunicacao;
    }
    for (const [id, r] of this.robos)
      if (!vistos.has(id)) {
        this.cena.remove(r.grupo);
        this.cena.remove(r.alvo);
        this.robos.delete(id);
      }
  }

  atualizarLogico(sup, sim) {
    const porId = new Map((sup.robos ?? []).map((r) => [r.id, r]));
    for (const r of this.robos.values()) {
      const l = porId.get(r.id);
      r.estado = l ? l.estado : 'INICIALIZANDO';
      r.manual = l?.modo === 'MANUAL';
      const cor = CORES_ESTADO[r.estado] ?? '#8a96a0';
      r.led.material.emissive.set(cor);
      r.div.style.setProperty('--cor', cor);
      const b = r.bateria ?? l?.bateria ?? 0;
      const barra = r.div.querySelector('.bateria');
      barra.className = `bateria ${b <= 15 ? 'critica' : b <= 30 ? 'baixa' : ''}`;
      barra.querySelector('b').style.width = `${Math.max(0, Math.min(100, b))}%`;
      r.div.querySelector('.alerta').textContent = r.estado === 'DEFEITO' ? 'defeito' : r.estado === 'SEM_COMUNICACAO' ? 'sem sinal' : r.manual ? 'manual' : '';
    }

    for (const p of this.passagens) {
      const est = (sup.passagens ?? []).find((q) => q.nome === p.dados.nome);
      if (!est) continue;
      p.placa.textContent = `${p.dados.nome} · ${est.ocupacao}/${est.capacidade}`;
      p.placa.classList.toggle('cheia', est.ocupacao >= est.capacidade);
      p.lampadas.forEach((l, i) => l.material.emissive.set(i < est.ocupacao ? '#d9473f' : '#3fa36b'));
    }
    for (const e of this.estacoes) {
      const ocupada = [...this.robos.values()].some((r) => {
        if (r.estado !== 'RECARREGANDO' || !r.pos) return false;
        const p = this.mapa(r.pos);
        return Math.hypot(p.x - e.dados.x, p.y - e.dados.y) < 0.6;
      });
      e.tela.material.emissiveIntensity = ocupada ? 2.6 + Math.sin(performance.now() / 200) : 0.8;
    }
    this.atualizarCaixas(sup, sim);
  }

  atualizarCaixas(sup) {
    const equipes = new Map((sup.equipes ?? []).map((e) => [e.indice, e]));
    const vistos = new Set();
    const filaPorBaia = new Map();
    const entreguesPorZona = new Map();
    const baias = new Map((this.dados.baias ?? []).map((b) => [b.id, b]));

    for (const c of sup.caixas ?? []) {
      vistos.add(c.id);
      let malha = this.caixas.get(c.id);
      if (!malha) {
        const [w, h, d] = dimensoesCaixa(c.tamanho);
        malha = new THREE.Mesh(new THREE.BoxGeometry(w, h, d), this.materialCaixa);
        malha.castShadow = malha.receiveShadow = true;
        malha.userData = { altura: h, alvo: null };
        this.cena.add(malha);
        this.caixas.set(c.id, malha);
      }
      const h = malha.userData.altura;
      let alvo = null;
      if (c.estado === 'NA_FILA') {
        const b = baias.get(c.baia);
        const n = filaPorBaia.get(c.baia) ?? 0;
        filaPorBaia.set(c.baia, n + 1);
        if (b && n < 6) alvo = this.mundo(b.x - 2.3 + (n % 2) * 1.1, b.y - 0.55 + Math.floor(n / 2) * 0.55 - 0.2, h / 2);
        malha.visible = !!alvo;
      } else if (c.estado === 'EM_TRANSPORTE' || (c.estado === 'DESIGNADA' && c.equipe >= 0 && ['ACOPLANDO', 'TRANSPORTE', 'DESACOPLANDO'].includes(equipes.get(c.equipe)?.estado))) {
        const eq = equipes.get(c.equipe);
        malha.userData.membros = eq?.membros ?? [];
        malha.visible = true;
      } else if (c.estado === 'ENTREGUE') {
        // empilha no ponto onde a equipe descarregou
        const chave = `${c.x},${c.y}`;
        const n = entreguesPorZona.get(chave) ?? 0;
        entreguesPorZona.set(chave, n + 1);
        alvo = this.mundo(c.x, c.y, h / 2 + n * 0.44);
        malha.visible = n < 4;
      } else {
        alvo = this.mundo(c.x, c.y, h / 2);
        malha.visible = true;
      }
      if (!['EM_TRANSPORTE', 'DESIGNADA'].includes(c.estado)) malha.userData.membros = null;
      if (alvo) {
        if (!malha.userData.alvo) malha.position.copy(alvo);
        malha.userData.alvo = alvo;
      }
      malha.material = c.estado === 'INTERROMPIDA' ? this.materialInterrompida() : this.materialCaixa;
    }
    for (const [id, m] of this.caixas)
      if (!vistos.has(id)) {
        this.cena.remove(m);
        this.caixas.delete(id);
      }
  }

  materialInterrompida() {
    if (!this._interrompida) this._interrompida = new THREE.MeshStandardMaterial({ map: this.materialCaixa.map, color: '#ff9b8f', roughness: 0.85 });
    return this._interrompida;
  }

  definirRota(id, pontos) {
    const antiga = this.rotas.get(id);
    if (antiga) {
      this.cena.remove(antiga);
      antiga.geometry.dispose();
    }
    if (!pontos?.length) {
      this.rotas.delete(id);
      return;
    }
    const geo = new THREE.BufferGeometry().setFromPoints(pontos.map(([x, y]) => this.mundo(x / 100, y / 100, 0.04)));
    const cor = CORES_ESTADO[this.robos.get(id)?.estado] ?? '#4a8fd6';
    const linha = new THREE.Line(geo, new THREE.LineDashedMaterial({ color: cor, dashSize: 0.25, gapSize: 0.15, transparent: true, opacity: 0.85 }));
    linha.computeLineDistances();
    linha.visible = this.mostrarRotas;
    this.cena.add(linha);
    this.rotas.set(id, linha);
  }

  alternarRotas(v) {
    this.mostrarRotas = v;
    for (const l of this.rotas.values()) l.visible = v;
  }

  alternarRotulos(v) {
    this.container.classList.toggle('oculta-rotulos', !v);
  }

  selecionar(id) {
    this.selecionado = id;
    for (const r of this.robos.values()) {
      r.anel.visible = r.id === id;
      r.div.classList.toggle('selecionado', r.id === id);
    }
  }

  definirCamera(modo) {
    this.modo = modo;
    this.controles.enabled = modo === 'perspectiva';
    this.controlesPlanta.enabled = modo === 'planta';
    this.redimensionar();
  }

  pedirPonto(callback) {
    this.pedidoPonto = callback;
    this.renderer.domElement.style.cursor = callback ? 'crosshair' : '';
  }

  // ---------------------------------------------------------------- interação

  camera() {
    return this.modo === 'planta' ? this.camPlanta : this.camPerspectiva;
  }

  clique(e) {
    if (!this.inicioClique || Math.hypot(e.clientX - this.inicioClique[0], e.clientY - this.inicioClique[1]) > 5) return;
    const ret = this.renderer.domElement.getBoundingClientRect();
    this.ponteiro.set(((e.clientX - ret.left) / ret.width) * 2 - 1, -((e.clientY - ret.top) / ret.height) * 2 + 1);
    this.raycaster.setFromCamera(this.ponteiro, this.camera());
    if (this.pedidoPonto && this.piso) {
      const hit = this.raycaster.intersectObject(this.piso)[0];
      if (hit) {
        const p = this.mapa(hit.point);
        const cb = this.pedidoPonto;
        this.pedirPonto(null);
        cb(p);
      }
      return;
    }
    const corpos = [...this.robos.values()].map((r) => r.corpo);
    const hit = this.raycaster.intersectObjects(corpos, true)[0];
    if (hit) {
      let o = hit.object;
      while (o && o.userData.robo == null) o = o.parent;
      if (o) this.aoSelecionarRobo?.(o.userData.robo);
    }
  }

  // área da tela não coberta pelos painéis, em pixels: {esquerda, direita, topo, base}
  definirAreaLivre(area) {
    this.areaLivre = area;
    this.redimensionar();
  }

  redimensionar() {
    const w = this.container.clientWidth || window.innerWidth;
    const h = this.container.clientHeight || window.innerHeight;
    this.renderer.setSize(w, h);
    this.rotulos.setSize(w, h);
    this.camPerspectiva.aspect = w / h;
    const a = this.areaLivre ?? { esquerda: 0, direita: 0, topo: 0, base: 0 };
    const dx = (a.direita - a.esquerda) / 2;
    const dy = (a.base - a.topo) / 2;
    for (const cam of [this.camPerspectiva, this.camPlanta]) cam.setViewOffset(w, h, dx, dy, w, h);
    this.camPerspectiva.updateProjectionMatrix();
    if (this.L) {
      const margem = 1.08;
      const livreW = Math.max(200, w - a.esquerda - a.direita);
      const livreH = Math.max(200, h - a.topo - a.base);
      const escala = Math.max((this.L * margem) / livreW, (this.A * margem) / livreH) / 2;
      Object.assign(this.camPlanta, { left: -w * escala, right: w * escala, top: h * escala, bottom: -h * escala });
      this.camPlanta.updateProjectionMatrix();
    }
  }

  animar() {
    requestAnimationFrame(() => this.animar());
    const dt = Math.min(0.1, this.relogio.getDelta());
    const k = 1 - Math.exp(-dt * 10);
    for (const r of this.robos.values()) {
      if (!r.pos) continue;
      r.pos.lerp(r.destino, k);
      let d = r.thetaDestino - r.theta;
      d = Math.atan2(Math.sin(d), Math.cos(d));
      r.theta += d * k;
      r.grupo.position.copy(r.pos);
      r.corpo.rotation.y = r.theta;
      r.elevacao += ((r.acoplar ? 0.08 : 0) - r.elevacao) * k;
      r.elevador.position.y = 0.235 + r.elevacao;
      if (r.estado !== 'DEFEITO') r.lidar.rotation.y += dt * 9;
      r.anel.scale.setScalar(1 + 0.06 * Math.sin(performance.now() / 180));
      const mostrarAlvo = r.id === this.selecionado && r.sp && r.sp.distanceTo(r.pos) > 0.3;
      r.alvo.visible = mostrarAlvo;
      if (mostrarAlvo) r.alvo.position.copy(r.sp);
    }
    for (const c of this.caixas.values()) {
      const membros = (c.userData.membros ?? []).map((id) => this.robos.get(id)).filter((r) => r?.pos);
      if (membros.length) {
        // carga erguida: fica sobre o centro da equipe, na altura dos elevadores
        const centro = membros.reduce((acc, r) => acc.add(r.pos), new THREE.Vector3()).divideScalar(membros.length);
        const elev = membros.reduce((acc, r) => acc + r.elevacao, 0) / membros.length;
        if (elev > 0.01) {
          c.position.copy(centro.setY(0.25 + elev + c.userData.altura / 2));
          continue;
        }
      }
      if (c.userData.alvo) c.position.lerp(c.userData.alvo, k * 0.6);
    }
    if (this.modo === 'seguir' && this.selecionado != null) {
      const r = this.robos.get(this.selecionado);
      if (r?.pos) {
        const atras = new THREE.Vector3(-Math.cos(r.theta) * 5, 4, Math.sin(r.theta) * 5).add(r.pos);
        this.camPerspectiva.position.lerp(atras, k * 0.5);
        this.controles.target.lerp(r.pos, k);
        this.camPerspectiva.lookAt(this.controles.target);
      }
    } else if (this.modo === 'perspectiva') this.controles.update();
    else this.controlesPlanta.update();
    const cam = this.camera();
    this.renderer.render(this.cena, cam);
    this.rotulos.render(this.cena, cam);
  }
}
