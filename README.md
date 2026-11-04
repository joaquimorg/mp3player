# Leitor de MP3 para o LilyGO T-Watch-2020 V1

Firmware para o relógio **LilyGO T-Watch-2020 V1** que o transforma num leitor de MP3 com auscultadores/colunas Bluetooth. As músicas são guardadas na memória do relógio (4 espaços, ou "slots") e enviadas por Wi-Fi (ou por cabo USB) a partir de uma app para Windows, ou por Wi-Fi a partir de uma página web (telemóvel ou PC).

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

Muda-se de modo com o botão **Programar** (no Menu de Reprodução) ou **Reprodução** (em Programação). A mudança demora alguns segundos ("A mudar de modo..."). Ao ligar o relógio ele arranca em Reprodução.

### Ecrãs e botões

Em Reprodução há três ecrãs: **Leitor** (o principal), **Músicas** e **Menu**. Todos os botões têm pelo menos 50 px de altura e ficam separados uns dos outros; enquanto se carrega num botão ele muda de cor.

**Leitor** (ecrã principal)

```
┌──────────────────────────────┐
│ ● Redmi Buds 6 Active        │  estado do Bluetooth (não é botão)
│ ▶ 2 · Nome da faixa…         │  o que está a tocar
│ ┌──────┐ ┌────────┐ ┌──────┐ │
│ │  ⏮   │ │   ⏯    │ │  ⏭   │ │  anterior · tocar/pausa · seguinte
│ └──────┘ └────────┘ └──────┘ │
│ ┌──────┐   Vol 50   ┌──────┐ │
│ │  −   │            │  +   │ │  volume (0 a 100, de 5 em 5)
│ └──────┘            └──────┘ │
│ ┌───────────┐ ┌────────────┐ │
│ │  Músicas  │ │    Menu    │ │
│ └───────────┘ └────────────┘ │
└──────────────────────────────┘
```

- A **linha do Bluetooth** (no topo) mostra o estado: `●` verde e o nome do dispositivo quando os auscultadores estão ligados, `●` amarelo e "A ligar..." enquanto se liga (um dispositivo guardado volta a ligar-se sozinho durante uns segundos depois de entrares em Reprodução), e `○` cinzento e "Sem auscultadores" quando não há ligação.
- A linha de baixo mostra a faixa atual ("▶ 2 · nome" a tocar, "❚❚ 2 · nome" em pausa), "Parado" ou "Nenhuma música" se todos os slots estiverem vazios.
- **⏯** toca/pausa (o maior botão; mostra ❚❚ enquanto toca). Parado, toca a faixa atual ou a primeira que houver.
- **⏮ / ⏭**: faixa anterior / seguinte.
- **− / +**: volume; fica gravado.

**Músicas**
- Quatro **mosaicos 2×2**, um por slot, com o número, o nome da faixa (quebrado em até 3 linhas) ou "Vazio"; a faixa atual tem o ▶ (a tocar) ou ❚❚ (em pausa).
- Tocar num mosaico **toca a faixa e volta ao Leitor**. Tocar na faixa atual faz pausa/retoma e fica neste ecrã. Um mosaico "Vazio" não faz nada.
- **Voltar** (barra larga em baixo) regressa ao Leitor.

**Menu** (quatro botões grandes)
- **Bluetooth** (por baixo: `BT: nome` ou `desligado`): abre o ecrã Bluetooth.
- **Repetir faixa / Repetir todas**: carregar alterna; o botão mostra o modo atual (repete a faixa atual, ou passa de uma faixa para a seguinte em ciclo).
- **Programar**: passa para o modo Programação.
- **Voltar**: regressa ao Leitor.

**Bluetooth**
- Lista dos dispositivos encontrados, **3 por página** (**Mais n/N** muda de página). O dispositivo ligado fica a verde com "ligado".
- **Procurar / Parar** inicia e termina a pesquisa (o botão fica cor de laranja enquanto procura). Cada pesquisa nova começa com a lista vazia (só fica o dispositivo ligado). Dispositivos sem nome aparecem pelo endereço (`aa:bb:cc:dd:ee:ff`) até o nome chegar.
- Sem nada ligado, a pesquisa começa ao abrir o ecrã e continua até **Parar**, até se ligar a um dispositivo ou até o ecrã se apagar.
- Com auscultadores ligados, abrir o ecrã **não** pesquisa; **Procurar** faz uma só pesquisa curta (cerca de 13 s) e pára sozinho. **Pesquisar enquanto a música toca pode causar pequenos cortes no som**, porque o rádio Bluetooth do relógio é partilhado.
- Põe os auscultadores em modo de emparelhamento e toca no nome deles. O relógio liga-se e memoriza o dispositivo: nas vezes seguintes liga-se sozinho.
- **Voltar** regressa ao Leitor.

**Programação**
- Mostra o endereço IP do relógio (por exemplo `192.168.1.50`) ou, enquanto liga, "A ligar ao Wi-Fi...". Se a ligação à rede cair, mostra "Sem ligação Wi-Fi".
- Durante um envio, **em qualquer ecrã**, aparece o ecrã **A receber música**: `Slot N · nome`, uma barra de progresso, `2,1 de 4,3 MB · 49%` e "falta cerca de X s". O ecrã não se apaga, os toques são ignorados (incluindo **Reprodução**) e convém não desligar o relógio. No fim mostra "Recebido: nome" e o tamanho (por exemplo `4,3 MB`) durante 4 s; se o envio falhar, mostra o motivo a vermelho durante 6 s. Depois volta ao ecrã anterior.
- **Reprodução** volta ao modo de música; o relógio volta sempre ao Leitor.

### Botão lateral e ecrã

- **Ao ligar** (ou quando o relógio reinicia) aparece durante **5 s** um ecrã de entrada com o ícone de música, o nome "T-Watch MP3", a versão e uma barra de progresso; os toques e o botão lateral são ignorados nesse tempo. Depois abre o Player (ou a Programação).
- **A bateria**: o ecrã apaga-se sozinho ao fim de 15 s sem toques.
- **Ligado por USB** (cabo ligado ao PC ou a um carregador): ao fim de 15 s o ecrã **só escurece** e não se apaga. Um toque volta ao brilho normal e carrega logo no botão que tocaste (não é preciso "acordar" primeiro). Se tirares o cabo com o ecrã escurecido, ele apaga-se; se ligares o cabo com o ecrã apagado pelo tempo, volta escurecido. O botão lateral continua a apagar o ecrã de vez, nos dois casos.
- O **botão lateral** (clique curto) liga/desliga o ecrã.
- Com o ecrã apagado, o **primeiro toque só acorda o ecrã**; não carrega em nenhum botão.
- **Para desligar completamente**: mantém o botão lateral premido cerca de **6 s**. Para ligar de novo, um clique no botão lateral.

## Primeira configuração do Wi-Fi

Na primeira vez (ou quando o relógio não consegue ligar à rede guardada, por exemplo por a palavra-passe estar errada) ele cria uma rede Wi-Fi própria:

1. No relógio, toca em **Programar**. Se não houver rede guardada, ou ao fim de cerca de 45 s (3 tentativas) sem conseguir ligar, o ecrã mostra `192.168.4.1` e "Liga-te à rede Wi-Fi TWATCH-SETUP".
2. No telemóvel ou PC, liga-te à rede Wi-Fi **TWATCH-SETUP** (aberta, sem palavra-passe).
3. Abre `http://192.168.4.1` no navegador. Escreve o nome da rede (2,4 GHz) e a palavra-passe e carrega em **Guardar e reiniciar**.
4. O relógio reinicia, liga-se à tua rede e mostra o seu IP no ecrã de Programação.

As credenciais ficam guardadas na memória do relógio (NVS) e sobrevivem a atualizações do firmware.

## Adicionar músicas

Com a app Windows não precisas de fazer nada no relógio. Para a página web, entra primeiro em **Programação** no relógio e vê o IP no ecrã (o telemóvel/PC tem de estar na mesma rede Wi-Fi).

### App Windows (recomendada)

A pasta `app` tem a **app T-Watch MP3** para Windows 10/11, com janela e sem instaladores. Compila-a uma vez com `app\compilar.bat` (usa o compilador de C# que já vem no Windows); fica `app\TWatchMP3.exe`, que se abre com duplo clique. Liga o relógio ao PC com o cabo USB.

**Músicas** (o ecrã inicial)
- Mostra as 4 músicas do relógio (nome e tamanho), o espaço livre e um botão **Apagar** em cada uma.
- Para adicionar uma música: cola um **link** (por exemplo do YouTube) ou escolhe/arrasta um **ficheiro de áudio** (mp3, m4a, wav, flac...) para a janela, escolhe o lugar em *Guardar em* (a app propõe o primeiro vazio; também podes carregar num lugar da lista ou largar o ficheiro nele) e carrega em **Adicionar ao relógio**. Se o lugar já tiver uma música, a app pergunta se queres substituir.
- A app converte para MP3 e, se não couber, comprime mais (64 kbps mono). O envio usa o **Wi-Fi se o relógio tiver uma rede guardada**; senão usa o **cabo USB**. Durante o envio o relógio mostra o progresso no ecrã.
- No canto superior direito, um ponto verde mostra que o relógio está ligado (a vermelho: não encontrado).

**Definições**
- *O relógio*: estado da ligação e espaço livre, com *Procurar outra vez*.
- *Wi-Fi do relógio*: escreve o nome e a palavra-passe da tua rede (2,4 GHz) e carrega em *Guardar no relógio*. A palavra-passe viaja só pelo cabo USB e fica guardada no relógio.
- *Opções avançadas* (escondidas por defeito): enviar sempre por USB, instalar/atualizar o `yt-dlp` e o `ffmpeg` (programas auxiliares que a app instala sozinha quando precisa; se o YouTube deixar de funcionar, atualiza o yt-dlp) e os detalhes técnicos.

Notas: a primeira vez que abres o `.exe` o Windows pode avisar que o programa não é conhecido (não está assinado). Se outro programa estiver a usar o relógio (um monitor série, por exemplo), fecha-o. A app guarda a última porta e o último endereço em `app\twatch-config.txt`.

### Pela página web (telemóvel ou PC)

1. Abre `http://<IP-do-relógio>/` no navegador.
2. Em cada slot podes **Enviar** um ficheiro MP3 ou **Apagar** o slot. A página mostra o espaço livre.
3. Para substituir uma faixa que já existe é preciso apagar primeiro o slot (ou ter espaço livre para a nova); a página avisa.
4. Depois do envio, toca em **Reprodução** no relógio.

### A partir do YouTube (Windows)

(Alternativa mais simples de usar: a app acima.) A pasta `tools/pc` tem uma ferramenta que descarrega o áudio de um link do YouTube (ou converte um ficheiro de áudio), converte-o para MP3 e envia-o para o relógio. Instruções em [tools/pc/LEIAME.txt](tools/pc/LEIAME.txt). Em resumo: `obter-ferramentas.bat` (uma vez), depois `youtube-para-relogio.bat`.

### Limites

- **4 slots**.
- **Não há limite fixo por música**: o espaço total (cerca de 12 MB, ou seja, cerca de 14 minutos a 112 kbps) é **partilhado** pelos 4 slots. Uma música grande reduz o espaço das outras; a página mostra "Livre: X MB". Um ficheiro só é recusado se não couber no espaço livre (com uma pequena margem de 16 KB), e isso é detetado logo no início do envio.
- A ferramenta do YouTube tenta 64 kbps mono (metade do tamanho) quando o ficheiro não cabe no espaço livre.
- Só ficheiros MP3. O envio demora cerca de **1 minuto por cada 3 MB** e o relógio fica ocupado enquanto recebe.

## "Toque indisponível"

Se ao ligar o ecrã aparecer a faixa vermelha **"Toque indisponível - desliga e liga o relógio"**, o controlador de toque não respondeu (fica por vezes preso depois de gravar firmware ou de uma reposição por software). **Remédio**: desliga o relógio por completo, mantendo o botão lateral premido cerca de 6 s, e liga-o de novo com um clique. Um simples reinício não chega.

## Resolução de problemas

- **Não ouço nada**: confirma que há um dispositivo Bluetooth emparelhado (ecrã Bluetooth) e que o botão Bluetooth do Menu mostra `BT: nome`. Os auscultadores não podem estar ligados a outro aparelho.
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

### Protocolo série (USB)

O firmware final responde a comandos de uma linha em 115200 baud (a app usa-os; podes testá-los com `tools\pc\serial-cmd.ps1`, que precisa de `-ExecutionPolicy Bypass`). Manda uma linha vazia antes do primeiro comando. As respostas começam por `OK` ou `ERRO`:

| Comando | Resposta / efeito |
|---|---|
| `TWATCH?` | `OK TWATCH <versão> modo=<REPRODUCAO\|PROGRAMACAO> wifi=<rede\|-> ip=<ip\|-> livre=<bytes>` (a rede pode ter espaços: lê `wifi=` até ao último ` ip=`) |
| `WIFI <rede><TAB><palavra-passe>` / `WIFI? ...` | grava o Wi-Fi / só valida |
| `MODO PROG` / `MODO PLAY` | pede a mudança de modo (a resposta `OK` é de aceitação: vê `TWATCH?` para saber quando acabou) |
| `LISTA` | `OK LISTA <livre> <tamanho>:<nome em hex> x4` |
| `APAGAR <1-4>` | apaga um slot |
| `BAUD 921600` / `BAUD 115200` | muda a velocidade (volta sozinha a 115200 ao fim de um envio, ou ao fim de 10 s sem comandos) |
| `PUT <slot> <tamanho> <crc32 hex> <nome em hex>` | envia uma música em blocos `[seq][dados ≤4096][crc16 CCITT, byte mais alto primeiro]`, com resposta `K`/`R` por bloco e `OK <crc32>` no fim; `CANCELAR` aborta. Cliente de referência: `tools\pc\serial-put.ps1` |

A descrição completa e as regras de recuperação estão em `docs/superpowers/specs/2026-11-03-pc-app-design.md` e em `lib/serproto/serproto.h`.

### A app (código)

`app\AppLogic.cs` tem a lógica pura (escolha Wi-Fi/USB, planos de recodificação, somas de verificação, validações) com testes de consola em `app\tests\LogicTests.cs`; `app\compilar.bat testes` corre só esses testes. `SerialLink.cs` (USB), `WatchApi.cs` (HTTP), `Converter.cs` e `Tools.cs` (yt-dlp/ffmpeg) e as janelas (`MainForm.cs`, `MusicasTab.cs`, `DefinicoesTab.cs`, `Controls.cs` para o aspeto) são C# 5. Para testes sem janela: `TWatchMP3.exe --autosend <ficheiro-ou-link> <slot> [usb]` faz o mesmo que o botão *Enviar* e escreve o registo em `app\twatch-autosend.log`.

### Testes

```
pio test -e watch_test                 # test_audio, test_native, test_net, test_serproto, test_ui
pio test -e watch_test -f test_net     # só uma pasta
```

Os testes de áudio tocam uma faixa real, por isso precisam de pelo menos uma música num slot.

> **AVISO - testes destrutivos.** A pasta `test_device` **apaga os 4 slots e o espaço `mp3` da memória NVS, incluindo as credenciais de Wi-Fi**, o volume e o dispositivo Bluetooth memorizado. Por isso `pio test -e watch_test` já **não** a executa. Para a correr de propósito, assumindo que perdes as músicas e o Wi-Fi (terás de os configurar de novo):
>
> ```
> pio test -e watch_test_destructive
> ```

Ao testar à mão, ouve se há **cliques** quando mudas o volume ou o modo de repetição durante a reprodução: essas alterações são gravadas na NVS (escrita na flash) e podem interromper brevemente o áudio.
