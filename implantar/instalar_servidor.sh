#!/usr/bin/env bash
# Prepara uma VM Ubuntu 22.04/24.04 para deixar o sistema no ar: dependências, compilação, SCHED_FIFO,
# serviço systemd (sobe no boot e reinicia se cair) e, opcionalmente, um link público HTTPS pelo Cloudflare.
#   bash implantar/instalar_servidor.sh            # serviço na porta 8080
#   LINK_PUBLICO=1 bash implantar/instalar_servidor.sh   # também cria o link https://....trycloudflare.com
set -euo pipefail
cd "$(dirname "$0")/.."
RAIZ="$(pwd)"
ROBOS="${ROBOS:-8}"

sudo apt-get update -q
sudo apt-get install -y -q build-essential git curl python3-matplotlib
if ! command -v node > /dev/null || [[ "$(node -p 'process.versions.node.split(".")[0]')" -lt 18 ]]; then
    curl -fsSL https://deb.nodesource.com/setup_20.x | sudo -E bash -
    sudo apt-get install -y -q nodejs
fi

make -s
(cd interface && npm install --no-audit --no-fund --silent)
for b in bin/amr_robo bin/amr_organizador; do sudo setcap cap_sys_nice+ep "$b"; done

sudo tee /etc/systemd/system/amr.service > /dev/null <<UNIT
[Unit]
Description=Logistica colaborativa com AMRs
After=network-online.target

[Service]
User=$USER
WorkingDirectory=$RAIZ
ExecStart=$RAIZ/run.sh --robos $ROBOS --nao-abrir
KillSignal=SIGINT
Restart=on-failure
RestartSec=5

[Install]
WantedBy=multi-user.target
UNIT

if [[ "${LINK_PUBLICO:-0}" == 1 ]]; then
    if ! command -v cloudflared > /dev/null; then
        arq=$(dpkg --print-architecture)
        curl -fsSL -o /tmp/cloudflared.deb "https://github.com/cloudflare/cloudflared/releases/latest/download/cloudflared-linux-$arq.deb"
        sudo dpkg -i /tmp/cloudflared.deb
    fi
    sudo tee /etc/systemd/system/amr-link.service > /dev/null <<UNIT
[Unit]
Description=Link publico da tela (Cloudflare quick tunnel)
After=amr.service

[Service]
ExecStart=/usr/bin/cloudflared tunnel --no-autoupdate --url http://localhost:8080
Restart=on-failure

[Install]
WantedBy=multi-user.target
UNIT
fi

sudo systemctl daemon-reload
sudo systemctl enable amr.service
sudo systemctl restart amr.service   # roda de novo com os executáveis recém-compilados
[[ "${LINK_PUBLICO:-0}" == 1 ]] && sudo systemctl enable --now amr-link.service
sleep 15
echo
echo "Sistema no ar: http://<ip-da-vm>:8080 (libere a porta 8080 no firewall do provedor)"
if [[ "${LINK_PUBLICO:-0}" == 1 ]]; then
    echo "Link público:"
    sudo journalctl -u amr-link --no-pager | grep -o 'https://[a-z0-9-]*\.trycloudflare\.com' | tail -1
fi
echo "Logs: journalctl -u amr -f    Reiniciar: sudo systemctl restart amr"
