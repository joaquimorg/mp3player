# App Windows "T-Watch MP3" (design)

Data: 2026-11-03. Complementa `2026-11-02-watch-mp3-player-design.md`.

## Objetivo
Substituir o uso do `youtube-para-relogio.bat` por uma aplicação Windows com janela, simples de usar, que:
- configura o Wi-Fi do relógio pela porta série (USB), sem telemóvel;
- mostra e gere os 4 slots do relógio;
- converte um link do YouTube (ou um ficheiro local) em MP3 e envia-o para um slot.

## Fora de âmbito
- Instalador, assinatura digital do `.exe`, atualização automática da app.
- Outros sistemas operativos. Só Windows 10/11.
- Gestão do Bluetooth (continua a ser feita no ecrã do relógio).
- Remover o `.bat` (fica numa subpasta como alternativa até o utilizador confirmar a app).

## Tecnologia
- C# com Windows Forms, compilado com `csc.exe` do .NET Framework 4.x que vem no Windows (`C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe`). Sem SDK, Python nem instalador. Um script `compilar.bat` gera `TWatchMP3.exe`.
- O `.exe` abre com duplo clique. Usa `System.IO.Ports` (série), `System.Net.Http` ou `WebClient` (HTTP) e executa `yt-dlp.exe` e `ffmpeg.exe` como processos filhos (ficam na mesma pasta; a app descarrega-os).
- Todo o texto em português (Portugal).

## Janela (3 separadores)
1. **Relógio**
   - Estado da ligação: porta COM detetada automaticamente (identificada pelo protocolo `TWATCH?`), versão, modo (Reprodução/Programação), rede Wi-Fi atual, endereço.
   - Campos *Nome da rede* e *Palavra-passe* (com opção mostrar) e botão **Enviar Wi-Fi para o relógio**. Confirma com a resposta do relógio.
   - Botão **Atualizar** e botão **Ir para Programação** (comando `MODO PROG`).
2. **Músicas**
   - Tabela dos 4 slots (nome, tamanho) e espaço livre, lidos de `GET /api/slots`, com **Apagar** por slot (`DELETE /api/slot/N`, com confirmação).
   - Campo de **link do YouTube** ou **Escolher ficheiro**, escolha do slot (1-4) e botão **Enviar**.
   - Passos com barra de progresso e texto: a descarregar, a converter, a enviar (`POST /api/slot/N`, progresso real do envio), concluído.
   - Mesma lógica do `.bat` atual: codificar a 44,1 kHz e 112 kbps; se `tamanho + 16 KiB` > espaço livre, repetir a 64 kbps mono; se ainda não couber, dizer quanto falta e sugerir apagar um slot. Recusa resultados acima de 12 MB.
   - Endereço do relógio: tenta `twatch.local`, depois o último endereço guardado, depois o que o relógio indicar pela série. Campo manual de IP como último recurso.
3. **Ferramentas**
   - Estado do `yt-dlp.exe` e do `ffmpeg.exe` (versão ou "em falta"), botões **Descarregar** e **Atualizar yt-dlp** (mesmas fontes oficiais HTTPS do `obter-ferramentas.bat`).

## Protocolo série no relógio (firmware definitivo)
USB-CDC/UART a 115200, linhas terminadas em `\n`, no firmware `watch` (não só nos de depuração). Cada comando responde com uma linha `OK ...` ou `ERRO <motivo>`.
- `TWATCH?` -> `OK TWATCH <versão> modo=<REPRODUCAO|PROGRAMACAO> wifi=<ssid|-> ip=<ip|-> livre=<bytes>`.
- `WIFI <ssid>\t<palavra-passe>` (campos separados por TAB; a palavra-passe pode ter espaços) -> grava em NVS (`settings::setWifi`) -> `OK`. Valida 1-32 e 0 ou 8-63 caracteres. Não repete a palavra-passe na resposta nem em registos.
- `MODO PROG` / `MODO PLAY` -> pede a mudança de modo à máquina de estados existente -> `OK`.
- Comandos desconhecidos -> `ERRO comando`. Linha com mais de 200 bytes -> `ERRO tamanho`. Sem eco do que entra.
- O protocolo corre numa tarefa que lê a série sem bloquear a interface nem a reprodução, e ignora lixo (como os registos de arranque do ESP32).
- Nota de segurança: o acesso é físico (cabo USB). A palavra-passe do Wi-Fi continua guardada em claro no NVS, como já aceite.

## Envio de música: Wi-Fi ou série (decisão do utilizador)
O envio usa **Wi-Fi se o relógio tiver Wi-Fi configurado, senão a porta série**.
- A app lê `wifi=` em `TWATCH?`. Se houver rede configurada: pede `MODO PROG` se necessário, espera pelo endereço e envia por HTTP (`POST /api/slot/N`). Se o relógio não ficar acessível por Wi-Fi em 40 s, a app pergunta se quer enviar por USB.
- Se não houver Wi-Fi configurado (ou o utilizador marcar "Forçar USB"), envia pela série.
- **Envio pela série** (qualquer modo; o relógio pausa a reprodução durante a transferência e retoma no fim):
  - `PUT <slot> <tamanho> <crc32> <nome em hexadecimal UTF-8>` -> `OK PUT` ou `ERRO <motivo>` (slot inválido, não é MP3 (primeiros 4 bytes enviados no bloco 0), sem espaço, com a mesma regra `tamanho + 16 KiB <= livre`).
  - Depois, blocos de até 4096 bytes: `<seq 1 byte (número do bloco módulo 256)><dados><crc16 CCITT (init 0xFFFF) de seq+dados, 2 bytes, byte mais alto primeiro>`; o relógio responde `K` (aceite) ou `R` (repetir). Um bloco repetido com o mesmo `seq` que já foi aceite (ACK perdido) é confirmado com `K` sem ser escrito outra vez. A app repete até 5 vezes um bloco com `R` ou sem resposta em 3 s; depois cancela (`CANCELAR`).
  - No fim o relógio compara o CRC32 total e responde `OK <crc32>` e faz o `commit()` atómico do `SlotWriter`. Qualquer falha, cancelamento ou 5 s sem dados faz `abort()` e o slot anterior mantém-se.
  - Antes do `PUT`, a app pode mudar a velocidade com `BAUD 921600` -> `OK`; o relógio muda e a app muda a seguir; o relógio volta a 115200 no fim da transferência ou ao falhar.
- O relógio mostra o mesmo ecrã de progresso nos dois métodos (fonte: Wi-Fi ou USB), mantendo o ecrã ligado.

## mDNS no relógio
Em modo Programação (Wi-Fi ligado), o relógio anuncia `twatch.local` (serviço HTTP na porta 80). O Windows 10/11 resolve nomes `.local`. O IP continua visível no ecrã.

## Estrutura de ficheiros
- `app/` — código fonte C# (Program, MainForm e uma classe por responsabilidade: `SerialLink`, `WatchApi`, `Converter`, `Tools`), `compilar.bat`, `LEIAME.txt`.
- `tools/pc/` — os `.bat` atuais mantidos como alternativa.

## Erros e casos limite a tratar
- Relógio não ligado por USB / porta ocupada por outro programa (por exemplo, o monitor série): mensagem clara.
- Várias portas COM: testa cada uma com `TWATCH?` e usa a que responder.
- Wi-Fi 5 GHz ou palavra-passe errada: a app só grava; o estado real vê-se depois em `TWATCH?` e no ecrã.
- `twatch.local` não resolve: usa o IP guardado ou manual.
- Cancelar durante a conversão ou o envio: termina os processos filhos e apaga os temporários.
- Nomes de ficheiro com acentos, `&`, `;` ou `,`: passam por ficheiro temporário com nome seguro.
- Envio grande (até ~12 MB, perto de 4 minutos): barra de progresso e aviso para não desligar o relógio.

## Testes
- Protocolo série no relógio: testes unitários no relógio para o analisador de comandos e o enquadramento de blocos/CRC (lib pura), e verificação real com a app ou com uma ferramenta de linha de comandos (envio de 1 MB e 4,4 MB; corte do cabo a meio com o slot anterior intacto; bloco corrompido repetido).
- App: compilar sem avisos; lógica pura (decisão de recodificar, cálculo de espaço, nomes seguros) em testes de consola executáveis sem a janela; verificação ponta a ponta com o relógio real (Wi-Fi por série, listar, enviar ficheiro, enviar YouTube, apagar).
- Verificação visual da janela pelo utilizador.
