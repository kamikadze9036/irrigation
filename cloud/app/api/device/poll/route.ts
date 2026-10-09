// ESP32 volá tohle periodicky (viz cloud_sync.cpp). Autentizace přes X-Device-Token
// hlavičku, ne přes session cookie — zařízení se nepřihlašuje jako člověk.
export const runtime = "nodejs";

import { NextRequest, NextResponse } from "next/server";
import { redis } from "@/lib/redis";
import { isDeviceAuthorized } from "@/lib/deviceAuth";

export async function GET(req: NextRequest) {
  if (!isDeviceAuthorized(req)) {
    return NextResponse.json({ error: "unauthorized" }, { status: 401 });
  }

  await redis.set("lastSeen", Date.now());

  // Ve frontě mohou ležet ID požadavků, jejichž `req:<id>` už vypršel (prohlížeč
  // dostal 504 a odešel, nebo ESP32 bylo offline). Poll proto přeskakuje
  // zastaralá ID, dokud nenajde živý požadavek — jinak by se při pomalejším
  // odběru než příjmu fronta nikdy nedostala k čerstvým požadavkům.
  for (let i = 0; i < 100; i++) {
    const id = await redis.lpop<string>("queue");
    if (!id) break;

    const raw = await redis.get<string>(`req:${id}`);
    if (!raw) continue;
    const parsed = typeof raw === "string" ? JSON.parse(raw) : raw;

    return NextResponse.json({
      requestId: id,
      method: parsed.method,
      path: parsed.path,
      body: parsed.body,
    });
  }
  return NextResponse.json({ requestId: null });
}
