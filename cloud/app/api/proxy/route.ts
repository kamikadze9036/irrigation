// Browser (přihlášený session cookiem, viz middleware.ts) sem pošle požadavek určený
// pro ESP32. Uloží se do fronty v Redisu; ESP32 ho vyzvedne při dalším pollu
// (viz /api/device/poll), přehraje na sobě samém a výsledek pošle zpět
// (/api/device/response). Tady se na tu odpověď krátce čeká, aby to pro
// prohlížeč vypadalo jako běžný synchronní fetch.
//
// ESP32 v klidu polluje každých 12 s (CLOUD_POLL_IDLE_MS) a po prvním požadavku
// přepne na 4 s — první klik tedy může trvat až ~13 s, další už jsou rychlé.
// WAIT_MS proto musí být s rezervou delší než klidový interval.
export const runtime = "nodejs";
export const maxDuration = 30;

import { NextRequest, NextResponse } from "next/server";
import { randomUUID } from "crypto";
import { redis } from "@/lib/redis";

const WAIT_MS = 25000;
const POLL_INTERVAL_MS = 350;
const ALLOWED_METHODS = new Set(["GET", "POST"]);
// ESP32 polluje nejpozději každých ~13 s; když se neozvalo déle, je offline
// a nemá smysl 25 s čekat (každé čekání = desítky Redis příkazů).
const OFFLINE_AFTER_MS = 45000;

export async function POST(req: NextRequest) {
  const { path, method, body } = await req.json().catch(() => ({}));
  // Dashboard volá jen /api/... na ESP32 — nic jiného tunelem nepouštíme
  // (např. "/" by přes frontu tahalo 40 kB HTML).
  if (!path || typeof path !== "string" || !path.startsWith("/api/")) {
    return NextResponse.json({ error: "neplatná path" }, { status: 400 });
  }
  const m = typeof method === "string" ? method.toUpperCase() : "GET";
  if (!ALLOWED_METHODS.has(m)) {
    return NextResponse.json({ error: "neplatná metoda" }, { status: 400 });
  }

  const last = Number((await redis.get<number>("lastSeen")) ?? 0);
  if (!last || Date.now() - last > OFFLINE_AFTER_MS) {
    return NextResponse.json({ error: "Zařízení neodpovídá (offline?)" }, { status: 504 });
  }

  const id = randomUUID();
  await redis.set(
    `req:${id}`,
    JSON.stringify({ method: m, path, body: typeof body === "string" ? body : null }),
    { ex: 60 }
  );
  await redis.rpush("queue", id);

  const deadline = Date.now() + WAIT_MS;
  while (Date.now() < deadline) {
    const raw = await redis.get<string>(`res:${id}`);
    if (raw) {
      await redis.del(`res:${id}`);
      const parsed = typeof raw === "string" ? JSON.parse(raw) : raw;
      return new NextResponse(parsed.body ?? "{}", {
        status: parsed.status || 200,
        headers: { "content-type": "application/json" },
      });
    }
    await new Promise((r) => setTimeout(r, POLL_INTERVAL_MS));
  }

  // Požadavek odebrat i z fronty, ať tam nezůstane zastaralé ID.
  await redis.del(`req:${id}`);
  await redis.lrem("queue", 0, id);
  return NextResponse.json({ error: "Zařízení neodpovídá (offline?)" }, { status: 504 });
}
