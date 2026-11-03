# Leitor de MP3 para o LilyGO T-Watch-2020 V1

Firmware para o relógio **LilyGO T-Watch-2020 V1** que o transforma num leitor de MP3 com auscultadores/colunas Bluetooth. As músicas são guardadas na memória do relógio (4 espaços, ou "slots") e enviadas por Wi-Fi a partir de uma página web (telemóvel ou PC) ou do YouTube, com uma ferramenta para Windows.

## Hardware

- LilyGO T-Watch-2020 V1 (ESP32 com 16 MB de flash e 8 MB de PSRAM, ecrã tátil de 240x240, chip de energia AXP202).
- Auscultadores ou coluna Bluetooth com perfil A2DP (música). O áudio sai só por Bluetooth: o relógio não tem altifalante.
- Ligação USB ao PC só para instalar o firmware (porta `COM8` neste computador).

## Compilar e instalar

Requer o [PlatformIO](https://platformio.org/) (CLI ou extensão do VS Code). Com o relógio ligado por USB:

```
pio run -e watch -t upload
```

A porta está definida em `platformio.ini` (`upload_port = COM8`); altera-a se o relógio aparecer noutra porta. Não deixes um monitor série aberto durante o `upload`.

## Os dois modos

O relógio tem dois modos e **só um de cada vez** (o Bluetooth e o Wi-Fi não funcionam em simultâneo, por falta de memória):

| Modo | Para quê | O que está ligado |
|---|---|---|
| **Reprodução** | Ouvir música | Bluetooth |
| **Programação** | Enviar/apagar músicas, configurar o Wi-Fi | Wi-Fi e servidor web |

Muda-se de modo com o botão **Programar** (em Reprodução) ou **Reprodução** (em Programação). A mudança demora alguns segundos ("A mudar de modo..."). Ao ligar o relógio ele arranca em Reprodução.

### Ecrãs e botões

**Reprodução**
- Quatro linhas, uma por slot, com o nome da faixa ou "Vazio". Tocar numa linha toca essa faixa; tocar na faixa que está a tocar faz pausa/retoma.
- Botões de transporte: faixa anterior, play/pausa, faixa seguinte, volume - e + (o botão "-" mostra o volume atual, 0 a 100).
- **Repetir faixa / todas**: repete a faixa atual ou passa de uma faixa para a seguinte em ciclo.
- **BT: nome**: mostra o estado do Bluetooth e abre o ecrã Bluetooth.
- **Programar**: passa para o modo Programação.

**Bluetooth**
- **Procurar / Parar** inicia e termina a pesquisa; os dispositivos encontrados aparecem em lista (4 por página, **Mais** muda de página).
- Põe os auscultadores em modo de emparelhamento e toca no nome deles. O relógio liga-se e memoriza o dispositivo: nas vezes seguintes liga-se sozinho.
- **Voltar** regressa à Reprodução.

**Programação**
- Mostra o endereço IP do relógio (por exemplo `192.168.1.50`) ou, enquanto liga, "A ligar ao Wi-Fi...". Se a ligação à rede cair, mostra "Sem ligação Wi-Fi".
- Durante um envio mostra "A receber...".
- **Reprodução** volta ao modo de música.

### Botão lateral e ecrã

- O ecrã apaga-se sozinho ao fim de 15 s sem toques.
- O **botão lateral** (clique curto) liga/desliga o ecrã.
- Com o ecrã apagado, o **primeiro toque só acorda o ecrã**; não carrega em nenhum botão.
- **Para desligar completamente**: mantém o botão lateral premido cerca de **6 s**. Para ligar de novo, um clique no botão lateral.

## Primeira configuração do Wi-Fi

Na primeira vez (ou quando o relógio não consegue ligar à rede guardada, por exemplo por a palavra-passe estar errada) ele cria uma rede Wi-Fi própria:

1. No relógio, toca em **Programar**. Se não houver rede guardada, ou ao fim de 15 s sem conseguir ligar, o ecrã mostra `192.168.4.1` e "Liga-te à rede Wi-Fi TWATCH-SETUP".
2. No telemóvel ou PC, liga-te à rede Wi-Fi **TWATCH-SETUP** (aberta, sem palavra-passe).
3. Abre `http://192.168.4.1` no navegador. Escreve o nome da rede (2,4 GHz) e a palavra-passe e carrega em **Guardar e reiniciar**.
4. O relógio reinicia, liga-se à tua rede e mostra o seu IP no ecrã de Programação.

As credenciais ficam guardadas na memória do relógio (NVS) e sobrevivem a atualizações do firmware.

## Adicionar músicas

Primeiro entra em **Programação** no relógio e vê o IP no ecrã. O telemóvel/PC tem de estar na mesma rede Wi-Fi.

### Pela página web (telemóvel ou PC)

1. Abre `http://<IP-do-relógio>/` no navegador.
2. Em cada slot podes **Enviar** um ficheiro MP3 ou **Apagar** o slot. A página mostra o espaço livre.
3. Para substituir uma faixa que já existe é preciso apagar primeiro o slot (ou ter espaço livre para a nova); a página avisa.
4. Depois do envio, toca em **Reprodução** no relógio.

### A partir do YouTube (Windows)

A pasta `tools/pc` tem uma ferramenta que descarrega o áudio de um link do YouTube (ou converte um ficheiro de áudio), converte-o para MP3 e envia-o para o relógio. Instruções em [tools/pc/LEIAME.txt](tools/pc/LEIAME.txt). Em resumo: `obter-ferramentas.bat` (uma vez), depois `youtube-para-relogio.bat`.

### Limites

- **4 slots**.
- **Máximo de 3 MB por ficheiro** (3.000.000 bytes, cerca de 3,5 minutos a 112 kbps). Quatro ficheiros deste tamanho cabem sempre na memória do relógio. A ferramenta do YouTube recomprime para 64 kbps mono (cerca de 6 minutos) quando o ficheiro é maior do que o limite ou não cabe no espaço livre.
- Só ficheiros MP3. O envio de uma música grande demora cerca de **1 minuto** e o relógio fica ocupado enquanto recebe.

## "Toque indisponível"

Se ao ligar o ecrã aparecer a faixa amarela **"Toque indisponível - desliga e liga o relógio"**, o controlador de toque não respondeu (fica por vezes preso depois de gravar firmware ou de uma reposição por software). **Remédio**: desliga o relógio por completo, mantendo o botão lateral premido cerca de 6 s, e liga-o de novo com um clique. Um simples reinício não chega.

## Resolução de problemas

- **Não ouço nada**: confirma que há um dispositivo Bluetooth emparelhado (ecrã Bluetooth) e que o botão mostra `BT: nome`. Os auscultadores não podem estar ligados a outro aparelho.
- **Os auscultadores não aparecem na pesquisa**: põe-nos em modo de emparelhamento e carrega em **Procurar** de novo.
- **A página não abre / a ferramenta diz que o relógio está inacessível**: o relógio tem de estar em Programação, na mesma rede Wi-Fi do PC, e o IP tem de ser o do ecrã.
- **A ferramenta diz que o relógio está em modo de configuração Wi-Fi**: o relógio não conseguiu ligar à rede guardada e criou a rede `TWATCH-SETUP`; volta a guardar o Wi-Fi como descrito acima.
- **"Sem espaço"**: apaga um slot e envia de novo.
- **O envio falha a meio**: a faixa antiga mantém-se intacta; tenta outra vez.
- **Estalidos ao mudar o volume ou a repetição durante a música**: o relógio grava essa definição na memória permanente e isso pode causar um pequeno clique; ouve com atenção ao testar e, se o notares, reporta-o.
- **O relógio não responde**: desliga-o por completo (botão lateral premido ~6 s).

## Notas para programadores

Ambientes do `platformio.ini`:

| Ambiente | Para quê |
|---|---|
| `watch` | Firmware final. O `build_flags` inclui `-mfix-esp32-psram-cache-issue -mfix-esp32-psram-cache-strategy=memw` (correção da cache da PSRAM, como na definição da placa `ttgo-t-watch` do PlatformIO). |
| `watch_test` | Testes unitários executados **no relógio** (`pio test -e watch_test`). Salta a pasta `test_device`. |
| `watch_test_destructive` | Só a pasta `test_device`. **Destrutivo** (ver abaixo). |
| `watch_uidebug` | Firmware real mais uma consola série (115200) para pilotar o ecrã a partir do PC: `tap x y`, `screen`, `heap`, `scan`, `reboot`, `stall`, `soak`. O script `tools/watch_serial.py` ajuda a usá-la. Não é para uso normal. |
| `native` | Testes no PC, mas **não corre aqui** (não há `g++` instalado neste computador); o `test_filter` lista só as pastas sem dependências do ESP32. Os testes correm no relógio. |

Código: `lib/slotlogic`, `lib/netlogic`, `lib/uilogic` (lógica pura, testável), `lib/audiocore` (anel de áudio e reamostragem), `src/` (player, Bluetooth, Wi-Fi/servidor, armazenamento, ecrã).

### Testes

```
pio test -e watch_test                 # test_audio, test_native, test_net, test_ui
pio test -e watch_test -f test_net     # só uma pasta
```

Os testes de áudio tocam uma faixa real, por isso precisam de pelo menos uma música num slot.

> **AVISO - testes destrutivos.** A pasta `test_device` **apaga os 4 slots e o espaço `mp3` da memória NVS, incluindo as credenciais de Wi-Fi**, o volume e o dispositivo Bluetooth memorizado. Por isso `pio test -e watch_test` já **não** a executa. Para a correr de propósito, assumindo que perdes as músicas e o Wi-Fi (terás de os configurar de novo):
>
> ```
> pio test -e watch_test_destructive
> ```

Ao testar à mão, ouve se há **cliques** quando mudas o volume ou o modo de repetição durante a reprodução: essas alterações são gravadas na NVS (escrita na flash) e podem interromper brevemente o áudio.
