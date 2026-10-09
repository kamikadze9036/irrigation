# Ladění cloud tunelu — stav k 9. 10. 2026

Cloud dashboard (https://irrigation-pi.vercel.app) po nasazení v1.3.0 nefunguje:
stránka visí na „Načítám...“, konzole hlásí 504, později 502. **Oprava je v kódu
(přímé volání handlerů), čeká na ověření na zařízení** — viz níže.
Lokální ovládání (`http://<IP ESP32>`) funguje bez problémů.

## Řetězec požadavku

```
Safari → POST /api/proxy → Redis fronta → ESP32 GET /api/device/poll
       → ESP32 zavolá handler přímo (WebUI_Dispatch, bez HTTP)
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

## Lokální přehrání — vyřešeno obejitím HTTP

**Původní problém:** `replayLocal()` posílalo požadavek přes HTTP na vlastní IP
(`http://192.168.20.8/...`) a po ~5–6 s (connect timeout HTTPClientu) vracelo false,
takže do cloudu šla záložní odpověď 502 (`Lokální požadavek selhal`, 50 B).
Přesná příčina selhání spojení ESP32 samo na sebe se nezjišťovala (hypotézy:
loopback přes WiFi rozhraní, plánování tasků na core 0, sokety/heap po TLS).

**Oprava:** cloud tunel už HTTP smyčku nepoužívá. Handlery v `webui.cpp` dostávají
`ApiCtx` (tělo + výstup) místo přímého přístupu k `server`, jsou v jedné tabulce
`API_ROUTES` a volá je jak WebServer, tak `WebUI_Dispatch()` z `cloud_sync.cpp`.
`apiMutex` zajišťuje, že naráz běží jen jeden handler. Restart (`/api/restart`,
WiFi s restartem) se provede až po odeslání odpovědi (`restartAfter`).

Zároveň: dashboard nepouští další `/api/status`, dokud předchozí nedoběhl
(`_dashBusy`) — v cloud módu se jinak požadavky hromadily ve frontě.

**Odezva:** `/api/device/response` vrací rovnou další čekající požadavek (sdílené
`lib/queue.ts` s pollem), ESP32 ho obslouží bez nového pollu — na jeden požadavek
připadá jedno HTTPS spojení místo dvou. Firmware se starší cloud appkou funguje dál
(bez `requestId` v odpovědi se vrátí k pollu).

**Co ověřit po nahrání firmwaru:** v Serialu po otevření cloud dashboardu
`[CLOUD] Požadavek ... GET /api/status` a hned `[CLOUD] Odpověď odeslána (200, ~700 B)`
— ne 502 / 50 B. Dashboard na Vercelu má načíst data.

## Jak to testovat

- Serial monitor 115200: hledat `[CLOUD]`.
- Poll jako zařízení (token z lokálního `config.h`):
  `curl -H "X-Device-Token: $T" https://irrigation-pi.vercel.app/api/device/poll`
  (odebere jeden čekající požadavek z fronty — používat jen při ladění).
- Vercel → Logs: `/api/proxy` (200 / 504), `/api/device/poll`.
- Překlad firmwaru na Apple Silicon vyžaduje Rosettu nebo stub za `ctags`
  (viz README, sekce Kompilace z příkazové řádky).
