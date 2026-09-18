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

  const id = await redis.lpop<string>("queue");
  if (!id) {
    return NextResponse.json({ requestId: null });
  }

  const raw = await redis.get<string>(`req:${id}`);
  if (!raw) {
    return NextResponse.json({ requestId: null });
  }
  const parsed = typeof raw === "string" ? JSON.parse(raw) : raw;

  return NextResponse.json({
    requestId: id,
    method: parsed.method,
    path: parsed.path,
    body: parsed.body,
  });
}
