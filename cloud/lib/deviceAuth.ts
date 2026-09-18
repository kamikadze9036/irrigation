import { NextRequest } from "next/server";
import { safeEqual } from "@/lib/auth";

// ESP32 se prokazuje hlavičkou X-Device-Token (viz cloud_sync.cpp), ne session cookie.
export function isDeviceAuthorized(req: NextRequest): boolean {
  const token = req.headers.get("x-device-token");
  const expected = process.env.DEVICE_TOKEN;
  if (!token || !expected) return false;
  return safeEqual(token, expected);
}
