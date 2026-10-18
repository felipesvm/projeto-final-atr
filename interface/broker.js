// Broker MQTT do sistema: TCP para os processos C++ e WebSocket para a tela no navegador.
// Pode ser trocado pelo Mosquitto (./run.sh --mosquitto) sem mudar mais nada.

import net from 'node:net';
import http from 'node:http';
import { WebSocketServer, createWebSocketStream } from 'ws';
import Aedes from 'aedes';

const args = Object.fromEntries(
  process.argv.slice(2).reduce((acc, a, i, arr) => (a.startsWith('--') ? [...acc, [a.slice(2), arr[i + 1]]] : acc), [])
);
const PORTA_TCP = Number(args['porta'] ?? 1883);
const PORTA_WS = Number(args['porta-ws'] ?? 9001);

const broker = new Aedes({ heartbeatInterval: 5000 });
const hora = () => new Date().toTimeString().slice(0, 8);

net.createServer(broker.handle).listen(PORTA_TCP, () => console.log(`${hora()} [broker] MQTT em tcp://0.0.0.0:${PORTA_TCP}`));

const servidorWs = http.createServer();
new WebSocketServer({ server: servidorWs }).on('connection', (ws) => broker.handle(createWebSocketStream(ws)));
servidorWs.listen(PORTA_WS, () => console.log(`${hora()} [broker] MQTT sobre WebSocket em ws://0.0.0.0:${PORTA_WS}`));

broker.on('client', (c) => console.log(`${hora()} [broker] conectado: ${c.id}`));
broker.on('clientDisconnect', (c) => console.log(`${hora()} [broker] desconectado: ${c.id}`));

const encerrar = () => broker.close(() => process.exit(0));
process.on('SIGTERM', encerrar);
process.on('SIGINT', encerrar);
