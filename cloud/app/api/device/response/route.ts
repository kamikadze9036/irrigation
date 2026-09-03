// ESP32 sem posílá výsledek požadavku, který si vyzvedlo přes /api/device/poll.
export const runtime = "nodejs";

import { NextRequest, NextResponse } from "next/server";
import { redis } from "@/lib/redis";

export async function POST(req: NextRequest) {
  const token = req.headers.get("x-device-token");
  if (!token || !process.env.DEVICE_TOKEN || token !== process.env.DEVICE_TOKEN) {
    return NextResponse.json({ error: "unauthorized" }, { status: 401 });
  }

  const { requestId, status, body } = await req.json().catch(() => ({}));
  if (!requestId) {
    return NextResponse.json({ error: "chybí requestId" }, { status: 400 });
  }

  await redis.set(
    `res:${requestId}`,
    JSON.stringify({ status: status || 200, body: body ?? "" }),
    { ex: 30 }
  );
  return NextResponse.json({ ok: true });
}
