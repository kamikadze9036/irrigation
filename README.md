# Irrigation Controller — ESP32-WROOM + 8-kanálové relé

Automatický zavlažovací systém pro ESP32-WROOM s 8-kanálovým relé modulem (Active HIGH).  
Webové admin rozhraní, týdenní rozvrhy, přeskočení zálivky podle počasí (Open-Meteo API), záložní WiFi AP mód s nastavením sítě přes web UI, NTP, mDNS.

---

## Hardware

| Komponenta | Detail |
|---|---|
| MCU | ESP32-WROOM-32 (Dev Board) |
| Relé modul | 8-kanál, Active HIGH, 5 V |
| Ventily | 24 V AC solenoid (např. Hunter, Rain Bird) |
| Napájení ESP32 + relé | 12 V DC (přes VIN) nebo 5 V (přes USB) |
| Napájení ventilů | 24 V AC transformátor |

### Přiřazení GPIO → relé → zóna

| Relé | GPIO | Funkce |
|---|---|---|
| 1 | 13 | Zóna 1 |
| 2 | 12 | Zóna 2 |
| 3 | 14 | Zóna 3 |
| 4 | 27 | Zóna 4 |
| 5 | 26 | Zóna 5 |
| 6 | 25 | Zóna 6 |
| 7 | 33 | Master ventil / čerpadlo |
| 8 | 32 | Rezerva |

> Piny lze změnit v `config.h` → pole `RELAY_PINS[8]`.

---

## Schéma zapojení

```
ESP32 GPIO ──► Relay IN  (Active HIGH — logická 1 = relé sepnuto)
Relay COM  ──► 24 V AC (pól A transformátoru)
Relay NO   ──► Solenoid ventil (cívka)
Solenoid   ──► 24 V AC (pól B transformátoru — přímý výstup)

Master ventil (Relay 7):
  sepne se PŘED spuštěním zóny (prodleva konfigurovatelná)
  rozepne se PO zastavení zóny (prodleva konfigurovatelná)
```

---

## Schéma rozvaděče

Rozvaděč obsahuje dva nezávislé napájecí okruhy — 24 V AC pro ventily a 12 V DC pro ESP32.

```
230V AC přívod
│
├─ L (fáze) ─┬─► MCB1 6A ──► TRAFO 24V AC ──► pól A → svorkovnice → COM vstupy relé
│            │                                         NO výstupy   → ventily (ven)
│            │               TRAFO 24V AC → pól B ──────────────────────────→ ven (nula ventilů)
│            │
│            └─► MCB2 6A ──► ZDROJ 12V DC ──► VIN desky (ESP32 + relé)
│
└─ N (nula) ──► N můstek ──► obě trafa (primár)

PE: nepoužito (SELV obvody — bezpečné napětí)
```

**Klíčový detail:** MCB1 větev (24 V AC) lze vypnout pro deaktivaci všech ventilů — ESP32 napájený z MCB2 větve zůstane spuštěný, zachová čas, WiFi a scheduler.

Princip ventilu: `COM (24V AC pól A) → Relé NO → cívka ventilu → pól B (nula)` — při sepnutém relé protéká 24V AC cívkou a ventil se otevře.

---

## Funkce

- **6 nezávislých zón**, každá s 3 programy (den v týdnu / čas / délka)
- **Týdenní rozvrh** — libovolná kombinace dní, nezávisle na každém programu; programy, které se sejdou nebo připadnou do běžící zálivky, se řadí do fronty (nic se tiše nezahodí); spuštění má 5min catch-up okno, takže ho nemine ani delší výpadek WiFi/NTP
- **Master ventil / čerpadlo** — automatická prodleva před a po zálivce (neblokující; při přepnutí zóny zůstává otevřený)
- **Přeskočení zálivky podle počasí** — Open-Meteo API (bez registrace, bez API klíče):
  - pokud pršelo více než X mm za posledních 24 h
  - pokud je v předpovědi více než X mm v příštích 24 h
- **Manuální spuštění** libovolné zóny na zvolený počet minut
- **Test relé** — každá zóna ~3 sekundy, běží na pozadí (ověření zapojení před instalací)
- **In-memory log** posledních 40 událostí (spuštění, zařazení do fronty, přeskočení kvůli počasí/pauze, test relé)
- **NTP** synchronizace času — automaticky po připojení, resync každých 24 h
- **mDNS** — admin dostupný jako `http://irrigation.local` (v STA módu)
- **WiFi STA reconnect** — automatické znovupřipojení k domácí síti každých 30 s
- **Záložní AP mód** — pokud domácí WiFi není dostupná, ESP32 spustí vlastní přístupový bod; každých 5 minut se tiše pokouší přepnout zpátky do STA
- **Dovolená mód (pauza zálivky)** — přes web rozhraní lze nastavit datum, do kdy se zálivka nepouští; po vypršení se automaticky obnoví normální provoz
- **Nastavení WiFi přes web UI** — v AP módu lze naskenovat dostupné sítě, vybrat a uložit credentials do NVS; ESP32 se restartuje a připojí k domácí WiFi
- **Ruční nastavení času** — pokud není internet (AP mód), čas lze synchronizovat z prohlížeče jedním kliknutím; scheduler pak funguje normálně
- **Maximální WiFi výkon** — TX výkon nastaven na 19.5 dBm v AP i STA módu

---

## WiFi chování

```
Start
  │
  ├─► Uložené credentials v NVS? → použij je (priorita před config.h)
  │
  ├─► Pokus o připojení k domácí WiFi (max 15 s)
  │     │
  │     ├─► Úspěch → STA mód
  │     │     http://irrigation.local  nebo  http://<IP>
  │     │     NTP sync, počasí funguje
  │     │
  │     └─► Neúspěch (nebo prázdné SSID) → AP mód
  │           Síť:  Zavlaha-AP  /  heslo: zavlaha123
  │           Admin: http://192.168.4.1
  │           Počasí a NTP nefungují (žádný internet)
  │           Každých 5 min zkusí přepnout zpět do STA
  │           Nastavení → WiFi → scan sítí → uložit & restart
  │
  └─► Za provozu: výpadek domácí WiFi → po neúspěšném reconnectu přepne do AP
```

> WiFi TX výkon nastaven na maximum (19.5 dBm) v obou módech.

---

## Webové rozhraní

| Záložka | Co tam najdeš |
|---|---|
| **Dashboard** | Přehled všech zón, běžící zálivka s progress barem, stav počasí, příští naplánovaná zálivka, karta Dovolená mód |
| **Zóny & Rozvrhy** | Název zóny, zapnutí/vypnutí, 3 programy (čas, délka, dny v týdnu) |
| **Manuální** | Spustit libovolnou zónu na zvolený čas (paralelně nebo sekvenčně), okamžité zastavení, test všech relé |
| **Počasí** | Aktuální srážky, předpověď, teplota + nastavení prahů pro přeskočení zálivky |
| **Nastavení** | Master ventil, prodlevy, NTP server, WiFi nastavení (scan + credentials), ruční čas, info o zařízení, restart |
| **Log** | Historie posledních 40 zálivek s časem, zónou, délkou a typem spuštění |

---

## Nahrání do ESP32

### Zapojení programátoru (ESP32 s USB-C jako flasher)

Na flashovacím ESP32 je **EN spojen s GND** — tím se jeho vlastní čip drží v resetu (neběží, nepřebíjí TX/RX linku) a USB-UART čip (CP2102/CH340) funguje jako čistý průchodový most.

```
Flashovací ESP32             Hlavní ESP32 (irrigation)
(EN → GND)

  TX0 ──────────────────────►  RX0  (GPIO3)
  RX0 ◄──────────────────────  TX0  (GPIO1)
  GND ────────────────────────  GND
```

> Použij TX0/RX0 (UART0) — bootloader/esptool poslouchá jen na UART0. Piny RX2/TX2 (UART2) na flashování nefungují.

> **Pozor, TX0/RX0 nemusí být vždy kříženě.** Na některých klonech desek jsou popisky TX/RX na desce už z pohledu USB-UART čipu (ne ESP32 čipu), takže **rovné** spojení (TX→TX, RX→RX) je pak elektricky správně. Když kříženě nefunguje ("No serial data received"), zkus to rovně.

> Napájení hlavní desky přes VIN **není nutně potřeba** — v praxi stačilo i jen **TX0/RX0/GND** (žádný VIN drát), pokud má hlavní ESP32 dost energie z toho, co je zrovna zapojené (např. zbytkové USB napájení, kondenzátory). Pokud by to nefungovalo, přidej **VIN (5V)** propojení z flashovacího ESP32, nebo napájej hlavní desku zvlášť — v tom případě musí být **GND obou desek propojená** (jinak esptool nedostane žádnou odpověď).

### Postup nahrání firmware

1. Připoj flashovací ESP32 přes USB-C k PC, v Arduino IDE vyber správný port
2. Klikni **Upload**
3. Jakmile se v konzoli objeví `Connecting....`, na **hlavním ESP32**: drž **BOOT**
4. Zatímco držíš BOOT, krátce zmáčkni a pusť **EN/RST**
5. Chvíli ještě drž BOOT, pak ho pusť — upload by měl naskočit (`Chip is ESP32-D0WDQ6...`)

> Pokud selže s `Failed to connect... No serial data received`, zkus to znovu — timing BOOT/EN je citlivý na pár desetin sekundy. Zkontroluj taky GND (viz výše) a TX/RX orientaci.

### Potřebné knihovny (Arduino Library Manager)
- **ArduinoJson** — Benoit Blanchon

### Nastavení Arduino IDE

| Volba | Hodnota |
|---|---|
| Board | `ESP32 Dev Module` |
| Upload Speed | `115200` nebo `460800` |
| Serial Monitor | `115200 baud` |

### Postup

1. Otevři složku `irrigation/` v Arduino IDE — `.ino` se načte automaticky se všemi `.cpp`/`.h` soubory
2. Uprav `config.h` — WiFi přihlašovací údaje, piny, souřadnice GPS
3. Nahraj do ESP32 (Ctrl+U)
4. Otevři Serial monitor (115200) — uvidíš průběh připojení a IP adresu
5. V prohlížeči otevři `http://irrigation.local` nebo přímo IP adresu

---

## Konfigurace (`config.h`)

```cpp
// Domácí WiFi — fallback pokud nejsou uloženy credentials v NVS přes web UI
// Nastav prázdné řetězce pokud chceš konfigurovat výhradně přes web (AP mód)
#define WIFI_SSID        ""               // nebo "nazev_site"
#define WIFI_PASSWORD    ""               // nebo "heslo"
#define WIFI_HOSTNAME    "irrigation"      // → http://irrigation.local

// Záložní AP (když domácí WiFi není dostupná)
#define WIFI_AP_SSID     "Zavlaha-AP"
#define WIFI_AP_PASSWORD "zavlaha123"      // → http://192.168.4.1

// GPIO piny relé (upravit dle tvého modulu)
static const int RELAY_PINS[8] = {13, 12, 14, 27, 26, 25, 33, 32};

// Souřadnice pro stahování počasí (Open-Meteo)
#define WEATHER_LAT  49.7469f
#define WEATHER_LON  13.3731f

// Ostatní
#define WEATHER_UPDATE_MIN    60   // interval aktualizace počasí (minuty)
#define MAX_PROGRAMS_PER_ZONE  3   // programy na zónu
#define LOG_MAX_ENTRIES       40   // velikost in-memory logu

// Vzdálený přístup přes cloud (volitelné, viz sekce "Vzdálený přístup" níže)
#define CLOUD_ENABLED           true
#define CLOUD_BASE_URL          ""   // prázdné = vypnuto; jinak "https://tvuj-projekt.vercel.app"
#define CLOUD_DEVICE_TOKEN      ""   // musí sedět s DEVICE_TOKEN nastaveným na Vercelu
#define CLOUD_POLL_INTERVAL_MS  4000     // rychlý poll chvíli po posledním požadavku
#define CLOUD_POLL_IDLE_MS      12000    // klidový poll (šetří Vercel/Redis limity)
#define CLOUD_TLS_VERIFY        true     // ověřovat certifikát appky (cloud_ca.h)
```

---

## Struktura projektu

```
irrigation/
├── config.h          – WiFi (STA + AP), GPIO piny, NTP, konstanty
├── storage.h/.cpp    – NVS persistence (Preferences) — zóny, počasí, systém
├── zones.h/.cpp      – Ovládání GPIO relé (active HIGH), fronta, master ventil a test relé jako neblokující stavový automat, in-memory log
├── scheduler.h/.cpp  – Týdenní rozvrhy, Scheduler_Tick() každých 15 s (catch-up okno 5 min, fronta při kolizi)
├── weather.h/.cpp    – Open-Meteo API přes HTTPClient (zvládá chunked encoding)
├── webui.h/.cpp      – WebServer na portu 80, REST API + celé HTML admin rozhraní
├── cloud_sync.h/.cpp – Vzdálený přístup: polling tunel na cloud relay appku (viz níže)
├── cloud_ca.h        – Kořenové CA pro ověření TLS spojení s appkou (GTS Root R1, ISRG Root X1, GlobalSign)
├── tools/sync_cloud_template.sh – generuje cloud/lib/dashboard.template.html z HTML ve webui.cpp
├── irrigation.ino    – setup(), loop(), WiFi STA/AP logika, NTP, mDNS, millis() časovače
└── cloud/            – Next.js appka pro Vercel (relay pro vzdálený přístup) — cloud/README.md
```

---

## REST API

| Metoda | Endpoint | Popis |
|---|---|---|
| GET | `/api/status` | Aktuální stav — zóny, počasí, čas, příští zálivka |
| GET | `/api/zones` | Konfigurace všech zón |
| POST | `/api/zones` | Uložit konfiguraci zón |
| POST | `/api/run` | `{"zone":1,"minutes":5}` — spustit zónu |
| POST | `/api/stop` | Zastavit zálivku |
| POST | `/api/test` | Test všech relé (~3 s každá) — vrátí hned, průběh v `/api/status` → `testRunning` |
| GET | `/api/weather` | Data počasí + nastavení |
| POST | `/api/weather` | Uložit nastavení počasí |
| POST | `/api/weather/refresh` | Vynutit okamžitou aktualizaci počasí |
| GET | `/api/system` | Systémové nastavení + info o zařízení (IP, uptime, FW verze) |
| POST | `/api/system` | Uložit systémové nastavení |
| GET | `/api/log` | Log zálivek (posledních 40) |
| POST | `/api/log/clear` | Vymazat log |
| POST | `/api/restart` | Restartovat ESP32 |
| GET | `/api/pause` | Stav pauzy — `{"active":true,"until":1780000000}` (unix čas) |
| POST | `/api/pause` | `{"until":1780000000}` — nastavit pauzu; `{"until":0}` — zrušit |
| GET | `/api/wifi` | Stav WiFi připojení + uložené SSID |
| POST | `/api/wifi` | `{"ssid":"...","password":"...","restart":true}` — uložit credentials |
| GET | `/api/wifi/scan` | Async scan sítí — volat opakovaně dokud `scanning=false` |
| POST | `/api/time` | `{"epoch":1234567890}` — ruční nastavení času (RAM, do restartu) |

> Všechny POST požadavky musí mít hlavičku `Content-Type: application/json`, jinak vrátí 415.
> Je to ochrana proti CSRF — cizí stránka otevřená v prohlížeči na domácí síti nemůže bez
> CORS preflightu poslat ESP32 příkaz. Lokální web ani cloud tunel to nijak neomezuje.

---

## Denní bitová maska

Dny v týdnu jsou uloženy jako bitová maska v jednom bajtu:

```
bit 0 = Pondělí
bit 1 = Úterý
bit 2 = Středa
bit 3 = Čtvrtek
bit 4 = Pátek
bit 5 = Sobota
bit 6 = Neděle

Příklady:
  Každý den          = 0b1111111 = 127
  Po + St + Pá       = 0b0010101 = 21
  Víkend (So + Ne)   = 0b1100000 = 96
```

---

## Vzdálený přístup (cloud relay)

Ovládání zálivky odkudkoliv (ne jen z domácí WiFi) — bez port forwardingu a bez VPN.
Místo Telegram bota (zvažováno dřív) appka jede jako malá appka na Vercelu, která
servíruje **totožné webové rozhraní** jako lokální `http://irrigation.local` — plný
přístup k rozvrhům, ne jen pár příkazů.

**Princip:** appka na Vercelu nemá jak se sama připojit k ESP32 (žádná veřejná IP,
žádný otevřený port), takže je to naopak — ESP32 appku sám pravidelně "pollne"
(v klidu každých `CLOUD_POLL_IDLE_MS` = 12 s, po požadavku 3 minuty rychleji každé
`CLOUD_POLL_INTERVAL_MS` = 4 s), jestli tam čeká nějaký požadavek od
přihlášeného uživatele, přehraje ho sám na sobě přes svoje lokální REST API výše,
a výsledek pošle zpátky. Appka na Vercelu tak nemá vlastní kopii žádné logiky
ani dat — je to čistě tunel chráněný device tokenem.

**Nastavení:** `cloud/README.md` — založení Vercel účtu, Redis (Upstash) přes
Vercel Marketplace, proměnné prostředí, a hodnoty do `CLOUD_BASE_URL` /
`CLOUD_DEVICE_TOKEN` v `config.h`.

**Stav připojení se dá zkontrolovat ze dvou stran:**
- appka na Vercelu ukazuje "Zařízení: online/offline" (podle toho, kdy ESP32 naposledy pollnulo)
- lokální dashboard (`http://irrigation.local`) ukazuje vedle WiFi ikonky "Cloud: připojeno/nedostupné/nenastaveno"
  (podle toho, kdy naposledy uspěl poll appky) — vidíš tak i doma v síti, aniž bys appku sám otevíral

**Nové soubory:** `cloud_sync.h/.cpp` (ESP32), `cloud/` (Next.js appka pro Vercel)
**Nové závislosti na ESP32:** žádné — `WiFiClientSecure` + `HTTPClient` jsou už
součást ESP32 Arduino core (stejně jako `weather.cpp`)
**Bezpečnost:** appka je chráněná heslem (session cookie, max. 10 pokusů za 15 min
z jedné IP), ESP32 appce heslo nezná — prokazuje se samostatným `DEVICE_TOKEN`;
TLS spojení ESP32 → appka se ověřuje proti kořenovým CA v `cloud_ca.h` (bez toho by
šel token odchytit "po cestě"); appka sama neukládá žádnou konfiguraci zálivky, jen
krátkodobou frontu požadavků

**Jedno HTML pro obojí:** dashboard je napsaný jen jednou ve `webui.cpp`; do
`cloud/lib/dashboard.template.html` ho kopíruje `tools/sync_cloud_template.sh`
(po každé úpravě UI spusť znovu, `--check` ověří shodu).

---

## Changelog

| Verze | Popis |
|---|---|
| 1.0.0 | Základní verze — 6 zón, 3 programy, počasí Open-Meteo, web admin, WiFi AP záložní mód |
| 1.1.0 | Paralelní a sekvenční spouštění více zón, dovolená mód (pauza zálivky do nastaveného data) |
| 1.2.0 | WiFi nastavení přes web UI (scan + NVS credentials), ruční nastavení času, max TX výkon 19.5 dBm |
| 1.3.0 | Oprava scheduleru (denní program se spouštěl jen jednou), catch-up okno a fronta při kolizi programů, neblokující master ventil / test relé, mutex nad stavem zón, ověření TLS pro cloud, adaptivní poll interval, CSRF ochrana POST API, log přeskočení (počasí/pauza), jedno HTML pro lokální i cloud UI |
