# Irrigation Controller — ESP32-WROOM + 8-kanálové relé

Automatický zavlažovací systém pro ESP32-WROOM s 8-kanálovým relé modulem (Active HIGH).  
Webové admin rozhraní, týdenní rozvrhy, přeskočení zálivky podle počasí (Open-Meteo API), záložní WiFi AP mód, NTP, mDNS.

---

## Hardware

| Komponenta | Detail |
|---|---|
| MCU | ESP32-WROOM-32 (Dev Board) |
| Relé modul | 8-kanál, Active HIGH, 5 V |
| Ventily | 24 V AC solenoid (např. Hunter, Rain Bird) |
| Napájení ESP32 + relé | 5 V / min. 1 A |
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
Relay COM  ──► 24 V AC (fáze transformátoru)
Relay NO   ──► Solenoid ventil (cívka)
Solenoid   ──► 24 V AC (nula transformátoru)

Master ventil (Relay 7):
  sepne se PŘED spuštěním zóny (prodleva konfigurovatelná)
  rozepne se PO zastavení zóny (prodleva konfigurovatelná)
```

---

## Funkce

- **6 nezávislých zón**, každá s 3 programy (den v týdnu / čas / délka)
- **Týdenní rozvrh** — libovolná kombinace dní, nezávisle na každém programu
- **Master ventil / čerpadlo** — automatická prodleva před a po zálivce
- **Přeskočení zálivky podle počasí** — Open-Meteo API (bez registrace, bez API klíče):
  - pokud pršelo více než X mm za posledních 24 h
  - pokud je v předpovědi více než X mm v příštích 24 h
- **Manuální spuštění** libovolné zóny na zvolený počet minut
- **Test relé** — každá zóna ~3 sekundy (ověření zapojení před instalací)
- **In-memory log** posledních 40 zálivek (zóna, délka, typ, čas)
- **NTP** synchronizace času — automaticky po připojení, resync každých 24 h
- **mDNS** — admin dostupný jako `http://irrigation.local` (v STA módu)
- **WiFi STA reconnect** — automatické znovupřipojení k domácí síti každých 30 s
- **Záložní AP mód** — pokud domácí WiFi není dostupná, ESP32 spustí vlastní přístupový bod; každých 5 minut se tiše pokouší přepnout zpátky do STA

---

## WiFi chování

```
Start
  │
  ├─► Pokus o připojení k domácí WiFi (max 15 s)
  │     │
  │     ├─► Úspěch → STA mód
  │     │     http://irrigation.local  nebo  http://<IP>
  │     │     NTP sync, počasí funguje
  │     │
  │     └─► Neúspěch → AP mód
  │           Síť:  Zavlaha-AP  /  heslo: zavlaha123
  │           Admin: http://192.168.4.1
  │           Počasí a NTP nefungují (žádný internet)
  │           Každých 5 min zkusí přepnout zpět do STA
  │
  └─► Za provozu: výpadek domácí WiFi → po neúspěšném reconnectu přepne do AP
```

---

## Webové rozhraní

| Záložka | Co tam najdeš |
|---|---|
| **Dashboard** | Přehled všech zón, běžící zálivka s progress barem, stav počasí, příští naplánovaná zálivka |
| **Zóny & Rozvrhy** | Název zóny, zapnutí/vypnutí, 3 programy (čas, délka, dny v týdnu) |
| **Manuální** | Spustit libovolnou zónu na zvolený čas, okamžité zastavení, test všech relé |
| **Počasí** | Aktuální srážky, předpověď, teplota + nastavení prahů pro přeskočení zálivky |
| **Nastavení** | Master ventil, prodlevy, NTP server, info o zařízení, restart |
| **Log** | Historie posledních 40 zálivek s časem, zónou, délkou a typem spuštění |

---

## Nahrání do ESP32

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
// Domácí WiFi
#define WIFI_SSID        "nazev_site"
#define WIFI_PASSWORD    "heslo"
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
```

---

## Struktura projektu

```
irrigation/
├── config.h          – WiFi (STA + AP), GPIO piny, NTP, konstanty
├── storage.h/.cpp    – NVS persistence (Preferences) — zóny, počasí, systém
├── zones.h/.cpp      – Ovládání GPIO relé (active HIGH), in-memory kruhový log
├── scheduler.h/.cpp  – Týdenní rozvrhy, Scheduler_Tick() volaný každou minutu
├── weather.h/.cpp    – Open-Meteo API přes HTTPClient (zvládá chunked encoding)
├── webui.h/.cpp      – WebServer na portu 80, REST API + celé HTML admin rozhraní
└── irrigation.ino    – setup(), loop(), WiFi STA/AP logika, NTP, mDNS, millis() časovače
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
| POST | `/api/test` | Test všech relé (~3 s každá) |
| GET | `/api/weather` | Data počasí + nastavení |
| POST | `/api/weather` | Uložit nastavení počasí |
| POST | `/api/weather/refresh` | Vynutit okamžitou aktualizaci počasí |
| GET | `/api/system` | Systémové nastavení + info o zařízení (IP, uptime, FW verze) |
| POST | `/api/system` | Uložit systémové nastavení |
| GET | `/api/log` | Log zálivek (posledních 40) |
| POST | `/api/log/clear` | Vymazat log |
| POST | `/api/restart` | Restartovat ESP32 |

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

## Changelog

| Verze | Popis |
|---|---|
| 1.0.0 | Základní verze — 6 zón, 3 programy, počasí Open-Meteo, web admin, WiFi AP záložní mód |
