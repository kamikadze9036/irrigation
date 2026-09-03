import { NextRequest, NextResponse } from "next/server";
import { verifySession } from "@/lib/auth";

// Cesty, kam se ESP32 nebo neautentizovaný prohlížeč musí dostat bez session cookie.
// ESP32 se prokazuje vlastním X-Device-Token uvnitř handleru, ne cookie.
const PUBLIC_PATHS = new Set([
  "/login",
  "/api/login",
  "/api/device/poll",
  "/api/device/response",
]);

export async function middleware(req: NextRequest) {
  const { pathname } = req.nextUrl;

  if (PUBLIC_PATHS.has(pathname)) {
    return NextResponse.next();
  }

  const cookie = req.cookies.get("session")?.value;
  const secret = process.env.AUTH_SECRET;
  const valid = cookie && secret ? await verifySession(cookie, secret) : false;

  if (!valid) {
    if (pathname.startsWith("/api/")) {
      return NextResponse.json({ error: "unauthorized" }, { status: 401 });
    }
    return NextResponse.redirect(new URL("/login", req.url));
  }

  return NextResponse.next();
}

export const config = {
  matcher: ["/", "/api/:path*"],
};
