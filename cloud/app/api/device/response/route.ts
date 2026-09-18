// ESP32 sem posílá výsledek požadavku, který si vyzvedlo přes /api/device/poll.
export const runtime = "nodejs";

import { NextRequest, NextResponse } from "next/server";
import { redis } from "@/lib/redis";
import { isDeviceAuthorized } from "@/lib/deviceAuth";

export async function POST(req: NextRequest) {
  if (!isDeviceAuthorized(req)) {
    return NextResponse.json({ error: "unauthorized" }, { status: 401 });
  }

  const { requestId, status, body } = await req.json().catch(() => ({}));
  if (!requestId || typeof requestId !== "string") {
    return NextResponse.json({ error: "chybí requestId" }, { status: 400 });
  }

  await redis.set(
    `res:${requestId}`,
    JSON.stringify({ status: status || 200, body: body ?? "" }),
    { ex: 60 }
  );
  return NextResponse.json({ ok: true });
}
