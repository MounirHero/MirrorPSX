# Broker esterno condiviso OpenProspero

Un unico payload `OpenProspero-SharedBroker.elf` serve più applicazioni
compatibili. Non viene inserito nei PKG e non viene avviato da MirrorPSX.
Riutilizza il daemon e il protocollo pubblico dell'SDK OpenProspero, senza
modificare Hyperion o le sorgenti del daemon. Il payload resta attivo quando
un client si chiude; va ricaricato dopo un riavvio della console.

## Primo test

1. Caricare `dist/OpenProspero-SharedBroker.elf` con il loader ELF già in uso
   sulla PS5. Avviarlo una sola volta. Serve un ambiente payload con le
   funzionalità kernel richieste dal companion OpenProspero.
2. Installare `dist/MirrorPSX-PS5-test-r10.pkg` e avviarlo dall'icona.
3. Verificare nel klog `External broker promoted own PID=...`, poi
   `Receiver listening: 1`. Provare Duplica schermo dall'iPad.
4. Se il broker manca o non risponde, avviarlo e premere Riprova nell'app.
   Un timeout non viene considerato successo.

La coppia ELF/PKG deve essere costruita con la stessa configurazione. Un
companion Hyperion già avviato con un altro token non è automaticamente
compatibile con questo client: non viene terminato o sostituito dall'app.

## Contratto per altre app

- Collegare `libopenprospero_companion.a` e usare
  `openprospero/companion_privileged.h` dell'SDK.
- Condividere il token di build, senza copiarlo nel sorgente o nei log.
- Selezionare la PS5 stessa: loopback o il suo IPv4 locale, porte 9069–9072.
- In un titolo con TCP limitato, usare la richiesta UDP del client SDK.
- Per l'elevazione chiedere il proprio PID; controllare il risultato e tutte
  le fasi rootdir, jaildir e credentials prima di proseguire.
- Caricare gli asset di `/app0` prima dell'elevazione, oppure conservare
  descrittori validi o usare il mount fisico corretto dopo il cambio di root.
- Non caricare una nuova istanza a ogni avvio di un'app e non arrestare il
  servizio condiviso quando un client termina.

MirrorPSX prepara gli asset prima della richiesta e mantiene l'atlante e i font
in memoria. Le chiavi vengono create dopo l'elevazione in
`/data/mirrorpsx/pairing.key`; il generatore continua a usare KERN_ARND.

## Build e limiti

Eseguire `tools/build-shared-broker.ps1`, poi `tools/build-ps5.sh` e
`tools/package-ps5.ps1`. La configurazione condivisa è conservata in
`%LOCALAPPDATA%/OpenProspero/shared-broker/token.txt`; il relativo header è
generato solo in `build/broker-config`. Il rebuild conserva il token.
Altre app devono adottare questo stesso contratto; non sono state modificate.

La build usa l'opzione SDK AllowPrivileged/Lan. Il protocollo storico usa un
token a 32 bit, non un canale cifrato. Il daemon limita i peer TCP alla console
stessa e richiede il token per i frame UDP. Non è un servizio da esporre a
Internet. Non viene abilitata l'opzione di shutdown o lo spawn OPCP opzionale;
restano le operazioni privilegiate fornite dal daemon SDK.

Il log R8 conferma elevazione del processo, chiavi persistenti, annuncio mDNS
e connessione IPv6 dell�iPad. La R9 corregge il successivo crash della
sincronizzazione. Il video e la correzione IPv6 richiedono una prova reale. L'app non dichiara il
ricevitore disponibile in caso di errore.
