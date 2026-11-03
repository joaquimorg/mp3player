# T-Watch 2020 V1 — Leitor MP3 Bluetooth (design)

Data: 2026-11-02

## Objetivo
Transformar o LILYGO T-Watch-2020 V1 (ESP32-D0WDQ6, 16 MB flash, porta COM8) num leitor MP3 de 4 slots.
Toca em loop, com saída de áudio para dispositivos Bluetooth (A2DP) que se emparelham a partir do relógio.
Os MP3 são enviados por Wi-Fi a partir do PC ou do telemóvel, e o áudio de vídeos do YouTube pode ser convertido e enviado a partir do PC.

## Fora de âmbito (YAGNI)
- Mais de 4 slots, playlists, tags ID3, equalizador, ecrã de capas.
- Conversão de YouTube dentro do relógio (impossível no ESP32).
- Reprodução com Wi-Fi e Bluetooth ligados em simultâneo.
- Envio por USB.

## Dois modos
| Modo | Wi-Fi | Bluetooth | Função |
|---|---|---|---|
| Reprodução | desligado | ligado (A2DP source) | Tocar os slots em loop |
| Programação | ligado (cliente) | desligado | Servir a página web para gerir os slots |

A troca de modo faz-se no ecrã. Os dois modos nunca estão ativos ao mesmo tempo, porque Wi-Fi e Bluetooth em simultâneo prejudicam o áudio e a memória.

## Arquitetura
Projeto Arduino + PlatformIO em `mp3player/`.

Bibliotecas:
- ESP32-A2DP (pschatzmann) para o Bluetooth.
- libhelix-mp3 (via arduino-audio-tools) para descodificar.
- LittleFS ou FATFS para o armazenamento.
- ESPAsyncWebServer (ou WebServer da core) para a página.
- Driver do ecrã ST7789 e do touch FT6236 (pinos em `twatch2020_v1.h`).

Unidades:
1. **storage** — partição de ~12 MB com 4 ficheiros fixos `slot1.mp3` a `slot4.mp3` e um ficheiro de metadados (nome do slot). Interface: ler, escrever e apagar um slot, e saber o espaço livre.
2. **player** — tarefa no core 1: lê o MP3 por blocos, descodifica e entrega PCM ao A2DP. Loop configurável: repetir a faixa, ou percorrer os 4 slots. Interface: play, pause, seguinte, anterior, volume.
3. **bt** — scan, lista de dispositivos, emparelhamento e reconexão automática ao último dispositivo guardado (no core 0).
4. **wifi/web** — liga ao Wi-Fi guardado, serve a página e recebe uploads. Se não houver credenciais, ou se a ligação falhar, cria uma rede "TWATCH-SETUP" onde se preenche SSID e palavra-passe.
5. **ui** — ecrã de toque 240×240: lista dos 4 slots, play/pause, anterior/seguinte, volume, modo de loop, ecrã Bluetooth, botão de troca de modo. O ecrã apaga-se ao fim de alguns segundos.

## Página web (modo Programação)
- O ecrã do relógio mostra o IP quando liga.
- A página mostra os 4 slots (nome, tamanho) e o espaço livre.
- Por slot: enviar MP3 (escolher ficheiro), apagar.
- Funciona no PC e no telemóvel, sem app.
- Endpoints HTTP: listar, enviar para o slot n (POST multipart), apagar o slot n.

## Ferramenta de PC (sem instalação)
Uma pasta portátil, sem Python nem instalador, para Windows 11:
- `yt-dlp.exe` e `ffmpeg.exe` na mesma pasta.
- `youtube-para-relogio.bat`: pergunta o link, o slot e o IP do relógio, descarrega o áudio, converte para MP3 (44,1 kHz, ~112 kbps) e envia para o endpoint do relógio com o `curl.exe` que já vem no Windows.
- A recodificação garante um formato aceite pelo descodificador e que cabe nos 4 slots.

## Orçamento de espaço
~12 MB de dados partilhados pelos 4 slots (≈14 minutos a 112 kbps no total). Não há limite fixo por música (alterado após a implementação, a pedido do utilizador): um ficheiro cabe se `tamanho + 16 KiB de margem <= espaço livre`. Substituir uma música exige espaço para a antiga e a nova ao mesmo tempo, por isso a página e a ferramenta pedem para apagar o slot primeiro. A ferramenta recodifica a 64 kbps mono quando o ficheiro não cabe, e a página recusa uploads que não cabem.

## Riscos a verificar no hardware
- Estabilidade do A2DP com o ecrã e o descodificador a competir pela CPU (descodificar no core 1, Bluetooth no core 0).
- Memória: o MP3 é lido por blocos, nunca inteiro em RAM.
- Tamanho real da partição de dados na flash de 16 MB.
- Descarregamento do `yt-dlp.exe` e do `ffmpeg.exe` (licenças, atualizações do `yt-dlp` quando o YouTube muda).

## Testes
- Compilar com PlatformIO e gravar via COM8.
- Verificar no log serial: arranque, mudança de modo, ligação Wi-Fi, upload, listagem e gravação em flash.
- Testar o upload com `curl` e pelo browser.
- O áudio e o emparelhamento só podem ser confirmados com o utilizador, com uns auscultadores ou uma coluna Bluetooth.
