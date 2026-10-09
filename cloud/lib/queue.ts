import { redis } from "@/lib/redis";

export type DeviceRequest =
  | { requestId: string; method: string; path: string; body: string | null }
  | { requestId: null };

// Vyzvedne další živý požadavek pro ESP32 z fronty. Volá ho /api/device/poll
// i /api/device/response — ESP32 tak po odeslání odpovědi rovnou dostane další
// požadavek bez nového pollu (= o jeden TLS handshake na požadavek méně).
//
// Ve frontě mohou ležet ID požadavků, jejichž `req:<id>` už vypršel (prohlížeč
// dostal 504 a odešel, nebo ESP32 bylo offline). Proto se zastaralá ID
// přeskakují, dokud se nenajde živý požadavek — jinak by se při pomalejším
// odběru než příjmu fronta nikdy nedostala k čerstvým požadavkům.
export async function takeNextRequest(): Promise<DeviceRequest> {
  await redis.set("lastSeen", Date.now());

  for (let i = 0; i < 100; i++) {
    const id = await redis.lpop<string>("queue");
    if (!id) break;

    const raw = await redis.get<string>(`req:${id}`);
    if (!raw) continue;
    const parsed = typeof raw === "string" ? JSON.parse(raw) : raw;

    return {
      requestId: id,
      method: parsed.method,
      path: parsed.path,
      body: parsed.body,
    };
  }
  return { requestId: null };
}
