// Browser (přihlášený session cookiem, viz middleware.ts) sem pošle požadavek určený
// pro ESP32. Uloží se do fronty v Redisu; ESP32 ho vyzvedne při dalším pollu
// (viz /api/device/poll), přehraje na sobě samém a výsledek pošle zpět
// (/api/device/response). Tady se na tu odpověď krátce čeká, aby to pro
// prohlížeč vypadalo jako běžný synchronní fetch.
export const runtime = "nodejs";
export const maxDuration = 15;

import { NextRequest, NextResponse } from "next/server";
import { randomUUID } from "crypto";
import { redis } from "@/lib/redis";

const WAIT_MS = 12000;
const POLL_INTERVAL_MS = 350;

export async function POST(req: NextRequest) {
  const { path, method, body } = await req.json().catch(() => ({}));
  if (!path || typeof path !== "string") {
    return NextResponse.json({ error: "chybí path" }, { status: 400 });
  }

  const id = randomUUID();
  await redis.set(
    `req:${id}`,
    JSON.stringify({ method: method || "GET", path, body: body ?? null }),
    { ex: 30 }
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

  await redis.del(`req:${id}`);
  return NextResponse.json({ error: "Zařízení neodpovídá (offline?)" }, { status: 504 });
}
