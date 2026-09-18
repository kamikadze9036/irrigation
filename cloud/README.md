# Zálivka — vzdálený přístup (cloud relay)

Malá appka na Vercelu, která umožní ovládat zálivku odkudkoli — ne jen z domácí
WiFi. ESP32 nemá otevřený port ani veřejnou IP; místo toho appku sám pravidelně
"pollne" (zeptá se, jestli něco čeká), takže není potřeba žádný port forwarding.

Appka nemá vlastní kopii ovládacího rozhraní — servíruje úplně stejné UI, jaké
běží lokálně na ESP32 (`../webui.cpp`), jen s jinou transportní vrstvou pod
kapotou. Soubor `lib/dashboard.template.html` generuje `../tools/sync_cloud_template.sh`
z `webui.cpp` — neupravuj ho ručně. Takže "vzdálený přístup" = doslova stejné ovládání, se stejným
scheduler editorem, jako doma.

## Jak to funguje

```
prohlížeč  →  Vercel appka (fronta v Redisu)  ←  ESP32 (pollne každých 12 s, po požadavku 4 s)
   (přihlášený heslem)         (bez cache dat, jen relay)      (přehraje požadavek sám na sobě)
```

1. Přihlásíš se heslem → appka ti dá session cookie.
2. Appka zobrazí přesně to samé UI jako lokální `http://irrigation.local`.
3. Klik na cokoliv (spustit zónu, uložit rozvrh...) appka zafrontuje požadavek.
4. ESP32 požadavek vyzvedne (první klik do ~13 s, další už do ~4 s), provede ho sám na sobě a pošle výsledek zpět.
5. Appka výsledek vrátí prohlížeči — vypadá to jako běžný fetch.

## Struktura

```
cloud/
├── middleware.ts                 – session cookie kontrola; bez ní 401 (API) / redirect na /login
├── app/route.ts                  – GET /  → servíruje dashboard (lib/dashboard.template.html)
├── app/login/page.tsx            – přihlašovací formulář
├── app/api/login/route.ts        – POST {password} → session cookie (rate limit 10/15 min z IP)
├── app/api/logout/route.ts       – POST → smaže cookie
├── app/api/proxy/route.ts        – POST {path,method,body} → zafrontuje pro ESP32, čeká na odpověď (max 25 s)
├── app/api/device/poll/route.ts  – GET (X-Device-Token) → ESP32 si vyzvedne čekající požadavek
├── app/api/device/response/route.ts – POST (X-Device-Token) → ESP32 vrátí výsledek
├── app/api/device/status/route.ts   – GET → {online, lastSeen} pro indikátor v dashboardu
├── lib/auth.ts                   – HMAC session tokeny + safeEqual (konstantní čas)
├── lib/deviceAuth.ts             – ověření X-Device-Token
├── lib/redis.ts                  – Upstash Redis klient (KV_REST_API_URL/TOKEN)
├── lib/dashboardHtml.ts          – načte šablonu a doplní window.CLOUD_MODE
└── lib/dashboard.template.html   – GENEROVANÉ z ../webui.cpp (tools/sync_cloud_template.sh)
```

**Klíče v Redisu:** `queue` (seznam ID čekajících požadavků), `req:<id>` a
`res:<id>` (obsah požadavku / odpovědi, TTL 60 s), `lastSeen` (čas posledního
pollu ESP32), `login:fail:<ip>` (počítadlo neúspěšných přihlášení, TTL 15 min).

## Nasazení (poprvé)

1. **Vercel účet** — jdi na [vercel.com](https://vercel.com), přihlas se přes
   GitHub (zdarma).
2. **Import projektu** — v repozitáři musí být tato složka `cloud/` (je tam).
   Ve Vercelu: *Add New → Project* → vyber tenhle GitHub repozitář → v
   nastavení **Root Directory** nastav na `cloud`.
3. **Databáze (fronta požadavků)** — v projektu ve Vercelu: *Storage → Marketplace
   Database Providers → Upstash for Redis* → vytvoř a propoj s projektem.
   Vercel sám doplní proměnné `KV_REST_API_URL` a `KV_REST_API_TOKEN` — nic
   ručně vyplňovat nemusíš.
4. **Proměnné prostředí** — v *Settings → Environment Variables* přidej:
   - `SITE_PASSWORD` — heslo, kterým se budeš přihlašovat do webu (vymysli si vlastní).
   - `AUTH_SECRET` — náhodný řetězec pro podpis session. Vygeneruj třeba:
     ```
     openssl rand -hex 32
     ```
   - `DEVICE_TOKEN` — náhodný řetězec, kterým se ESP32 appce prokazuje. Vygeneruj stejně:
     ```
     openssl rand -hex 32
     ```
     **Tenhle token musí být identický** s `CLOUD_DEVICE_TOKEN` v `config.h` na ESP32 (viz níže).
5. **Deploy** — Vercel appku sám sestaví a nasadí. Dostaneš URL typu
   `https://tvuj-projekt.vercel.app`.
6. **Nastav ESP32** — v `../config.h`:
   ```c
   #define CLOUD_BASE_URL      "https://tvuj-projekt.vercel.app"
   #define CLOUD_DEVICE_TOKEN  "stejný-řetězec-jako-DEVICE_TOKEN-ve-Vercelu"
   ```
   Nahraj firmware znovu do ESP32 (přes Arduino IDE).
7. **Test** — otevři appku ve Vercelu, přihlas se heslem. V horní liště by se
   mělo do ~15 s objevit "Zařízení: online" (ESP32 musí být připojené k domácí
   WiFi s internetem — v AP fallback módu vzdálený přístup nefunguje, protože
   tam žádný internet není).

## Update appky

Stačí pushnout změny do GitHubu (větev, kterou má Vercel projekt napojenou) —
redeploy proběhne automaticky.

## Bezpečnost — co si pohlídat

- `SITE_PASSWORD`, `AUTH_SECRET` a `DEVICE_TOKEN` drž jako tajemství — nikam
  je needituj do gitu (jsou jen ve Vercel env proměnných a v tvém lokálním
  `config.h`, který stejně nikam nepushuješ s reálnými hodnotami veřejně).
- `DEVICE_TOKEN` je jediné, co brání cizímu zařízení předstírat, že je "tvoje"
  ESP32 a posílat appce falešné odpovědi — drž ho stejně tajný jako heslo.
- Appka sama o sobě neukládá žádná data o zálivce (žádné programy, log,
  hesla k WiFi) — je to čistě tunel. Veškerá konfigurace zůstává jen na ESP32.
- Přihlášení je omezené na 10 neúspěšných pokusů za 15 minut z jedné IP
  (počítadlo v Redisu), hesla i tokeny se porovnávají v konstantním čase.
- ESP32 ověřuje TLS certifikát appky proti kořenovým CA v `../cloud_ca.h`
  (Google Trust Services, Let's Encrypt, GlobalSign). Pokud Vercel někdy
  přejde na jiného vydavatele, poll začne selhávat s TLS chybou — pak stačí
  do `cloud_ca.h` přidat nový kořen (viz komentář v souboru).

## Limity free tierů

ESP32 v klidu polluje každých 12 s = ~7 200 invokací denně a ~14 400 Redis
příkazů (2 na poll). Měsíčně tedy ~430 k Redis příkazů — pod limitem Upstash
free tieru (500 k/měsíc), ale ne s velkou rezervou. Pokud se limit blíží,
zvyš `CLOUD_POLL_IDLE_MS` v `config.h`.

## Lokální vývoj

```
cp .env.example .env.local   # vyplň SITE_PASSWORD, AUTH_SECRET, DEVICE_TOKEN, KV_*
npm install
npm run dev                   # http://localhost:3000
```
