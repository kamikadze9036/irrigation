// Prohlížeč (přihlášený) se ptá, jestli ESP32 v poslední době pollovalo — pro
// online/offline indikátor v dashboardu.
export const runtime = "nodejs";

import { NextResponse } from "next/server";
import { redis } from "@/lib/redis";

// ESP32 v klidu polluje každých CLOUD_POLL_IDLE_MS (12 s, viz config.h) —
// práh musí být s rezervou větší, jinak indikátor bliká offline/online.
const ONLINE_THRESHOLD_MS = 30000;

export async function GET() {
  const last = await redis.get<number>("lastSeen");
  const lastSeen = last ? Number(last) : 0;
  const online = lastSeen > 0 && Date.now() - lastSeen < ONLINE_THRESHOLD_MS;
  return NextResponse.json({ online, lastSeen });
}
