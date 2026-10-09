# Ladění cloud tunelu — stav k 9. 10. 2026

Cloud dashboard (https://irrigation-pi.vercel.app) po nasazení v1.3.0 nefunguje:
stránka visí na „Načítám...“, konzole hlásí 504, později 502. **Problém je otevřený.**
Lokální ovládání (`http://<IP ESP32>`) funguje bez problémů.

## Řetězec požadavku

```
Safari → POST /api/proxy → Redis fronta → ESP32 GET /api/device/poll
       → ESP32 přehraje požadavek sám na sobě (HTTP na vlastní IP)
       → ESP32 POST /api/device/response → proxy vrátí odpověď prohlížeči
```

## Co už je vyřešené

| Problém | Oprava | Commit |
|---|---|---|
| Fronta `queue` v Redisu se plnila zastaralými ID (po 504 zůstalo ID ve frontě, poll bral 1 ID / 12 s, přibývalo ≥1 / 5 s) → ESP32 nikdy nedostalo čerstvý požadavek | poll přeskakuje ID bez živého `req:<id>`, proxy při timeoutu dělá `lrem`, při offline zařízení (`lastSeen` > 45 s) selže hned | `cd3b4b3` |
| Firmware po pollu s požadavkem nic nelogoval (podezření na keep-alive TLS spojení z v1.3.0) | čerstvé TLS spojení na každý poll, logování chyb čtení odpovědi a odeslané odpovědi | `0b6104c` |

Ověřeno: poll jako zařízení (curl s `X-Device-Token`) vrací při otevřené stránce živé
požadavky, takže server a fronta fungují. ESP32 požadavky přijímá
(`[CLOUD] Požadavek ... GET /api/status`) a odpověď odesílá
(`[CLOUD] Odpověď odeslána (502, 50 B)`).

## Co je otevřené

**ESP32 nedokáže přehrát požadavek na vlastním webserveru.** `replayLocal()` v
`cloud_sync.cpp` vrací false po ~5–6 s (odpovídá výchozímu connect timeoutu
HTTPClientu 5 s), proto jde do cloudu záložní odpověď 502
(`{"ok":false,"error":"Lokální požadavek selhal"}`, 50 B).

Příčina zatím neznámá. Hypotézy:
1. Připojení na vlastní WiFi IP (`http://192.168.20.8/...`) z tasku na core 0 neprojde
   (loopback přes WiFi rozhraní, plný listen backlog WebServeru, blokovaný klient).
2. WebServer task (`WebUI_Handle`, core 0, priorita 1) nestíhá obsloužit klienta, když
   CloudSync task blokuje na síti.
3. Nedostatek heapu/soketů po TLS spojení s ověřením CA (`setCACert`) — TLS kontext
   zůstává naalokovaný, zatímco se otevírá druhé spojení.

## Poslední commit (`38d6ef9`) — zatím NENASAZENO do ESP32

`replayLocal()` zkouší nejdřív `127.0.0.1`, pak vlastní IP, s connect timeoutem 3 s a
do Serialu vypisuje důvod (`Lokálně <host>: <chyba> (<kód>)`). **Další krok:** nahrát
firmware, otevřít cloud dashboard, 30 s počkat a podívat se na řádky `[CLOUD]`.

Pokud selžou obě adresy, zvážit:
- logovat `ESP.getFreeHeap()` / `ESP.getMaxAllocHeap()` před přehráním,
- uvolnit TLS (`secureClient.stop()`) před lokálním voláním,
- obejít HTTP smyčku úplně: místo HTTP na sebe volat handlery přímo (např. sdílená
  tabulka routes nebo `WebServer` dispatch), čímž odpadne celá síťová vrstva.

## Jak to testovat

- Serial monitor 115200: hledat `[CLOUD]`.
- Poll jako zařízení (token z lokálního `config.h`):
  `curl -H "X-Device-Token: $T" https://irrigation-pi.vercel.app/api/device/poll`
  (odebere jeden čekající požadavek z fronty — používat jen při ladění).
- Vercel → Logs: `/api/proxy` (200 / 504), `/api/device/poll`.
- Překlad firmwaru na Apple Silicon vyžaduje Rosettu nebo stub za `ctags`
  (viz README, sekce Kompilace z příkazové řádky).
