// ESP32 volá tohle periodicky (viz cloud_sync.cpp). Autentizace přes X-Device-Token
// hlavičku, ne přes session cookie — zařízení se nepřihlašuje jako člověk.
export const runtime = "nodejs";

import { NextRequest, NextResponse } from "next/server";
import { isDeviceAuthorized } from "@/lib/deviceAuth";
import { takeNextRequest } from "@/lib/queue";

export async function GET(req: NextRequest) {
  if (!isDeviceAuthorized(req)) {
    return NextResponse.json({ error: "unauthorized" }, { status: 401 });
  }
  return NextResponse.json(await takeNextRequest());
}
