# Confronto main e lavoro locale — 13 ottobre 2026

Confronto dopo `git fetch origin`: base locale `5b0be255`, main remoto
`9af05b867e1cf7f03c777dd569eed1d62d2f8e7a`. Il ramo locale è indietro di
10 commit. Nessun merge, reset, checkout o sovrascrittura è stato eseguito.
La simulazione a tre vie è salvata in `build/main-comparison/results.json`;
considera anche i file locali non ancora tracciati e normalizza CRLF/LF.

## Collisioni confermate

| Area | File | Risultato e scelta consigliata |
| --- | --- | --- |
| Build | `.gitignore`, `CMakeLists.txt` | Conflitti testuali. Conservare build PS5 OpenProspero, SDL, FFmpeg avformat, DLNA e asset locali; adattare i test aggiunti da main. |
| Desktop | `src/platform/desktop.cpp` | Conflitto testuale: entrambi gestiscono input, rendering e ricevitore. Conservare l'HUD e la gestione dei due modi; valutare separatamente i controlli di main. |
| Ricevitore | `src/receiver/airplay.cpp`, `airplay.hpp` | Conflitti testuali: tracker connessioni, PIN e callback diversi. Recuperare la gestione esplicita di fine stream, l'inizializzazione dei parametri di callback e la rimozione del PIN dai log. Conservare la verifica RNG console e l'identità distinta PC/PS5. |
| Decoder | `src/receiver/decoder.cpp`, `decoder.hpp` | Conflitti testuali. Main gestisce EAGAIN e ripulisce le allocazioni fallite; conservare il nostro FrameConverter, la conversione del colore, il padding, FF_THREAD_SLICE e la mailbox senza copia. |
| UI | `src/ui/screen.cpp`, `screen.hpp` | File aggiunti indipendentemente, contenuti diversi: main usa render_screen/ScreenUiState e font icone; locale usa Screen/UiAssets e PNG PlayStation. Non sono intercambiabili. |
| GPU video | `src/platform/video_texture.cpp`, `video_texture.hpp` | File aggiunti indipendentemente, API diverse. Entrambi riutilizzano staging e fence. Confrontare controlli e contratti con i rispettivi host prima di selezionare le singole migliorie. |
| Test | `tests/decoder_test.cpp` | File aggiunti indipendentemente, contenuti diversi. Unire i casi di test dopo aver stabilizzato il decoder. |
| Documentazione | `README.md` | File aggiunti indipendentemente: unire solo le istruzioni coerenti con la build scelta e le prove effettive sulla console. |

Totale: 8 conflitti su file tracciati, 5 collisioni aggiunta/aggiunta.

## Sovrapposizioni che Git non identifica come conflitti

- Main aggiunge `src/platform/ps5_main.cpp` e `scripts/build-ps5*.ps1`;
  locale usa `src/platform/ps5.cpp`, `cmake/ps5.cmake` e `tools/build-ps5*.sh`.
  Sono due port console paralleli. Conservare quello locale già arrivato alla
  visualizzazione effettiva sulla PS5. Il main termina con return 0; il nostro
  host chiede l'uscita alla shell, come richiede AGENTS.md.
- I nuovi test UI `screen_model_test` e `screen_render_test` dipendono
  dall'interfaccia di main e devono essere adattati. I test state e lifecycle
  vanno valutati separatamente: il locale conserva frame_after per compatibilità,
  ma il percorso di rendering usa latest_frame, che restituisce un frame condiviso.
- Main modifica `third_party/uxplay/lib/{crypto.c,pairing.c,raop.c,raop_handlers.h}`:
  gestione delle chiavi effimere, controlli sui messaggi di abbinamento e
  distruzione di server parzialmente inizializzati. Questi file sono invariati
  localmente: Git li applicherebbe senza conflitto, ma incidono sul protocollo
  e vanno verificati con i test di pairing e con l'iPad. Le modifiche di
  terze parti devono essere motivate e documentate secondo AGENTS.md.
- Main aggiunge risorse Figtree/FontAwesome; il locale usa Inter e un atlante
  di immagini reali. Conservare le licenze associate agli asset effettivi.
- Il percorso PacBrew di main non dimostra disponibilità del generatore
  casuale nella nostra app nativa. Nel log R5 tutti i percorsi libSceRandom
  restituiscono 0x80020002. La build locale R6 prova KERN_ARND attraverso il
  gate libkernel validato e mantiene il controllo completo degli errori.

## Ordine di integrazione consigliato

1. Salvare un checkpoint del lavoro locale, inclusi i file non tracciati.
2. Conservare UI, DLNA e port console locali; allineare build e dipendenze.
3. Adattare le correzioni decoder, callback, fine stream e log PIN di main.
4. Importare le correzioni di pairing con documentazione e test pertinenti.
5. Adattare e avviare i test di main, poi provare pairing e video sulla PS5.

Questa è una verifica dei conflitti, non un'integrazione. La build R6 non
contiene modifiche recuperate da main.
