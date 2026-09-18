export const runtime = "nodejs";

import { NextRequest, NextResponse } from "next/server";
import { createSession, safeEqual } from "@/lib/auth";
import { redis } from "@/lib/redis";

// Ochrana proti hádání hesla: po MAX_FAILS neúspěšných pokusech z jedné IP
// se další přihlášení na WINDOW_S sekund odmítá (počítadlo je v Redisu,
// protože serverless instance nesdílejí paměť).
const MAX_FAILS = 10;
const WINDOW_S = 15 * 60;

function clientIp(req: NextRequest): string {
  return req.headers.get("x-forwarded-for")?.split(",")[0].trim() || "unknown";
}

export async function POST(req: NextRequest) {
  const { password } = await req.json().catch(() => ({ password: "" }));

  if (!process.env.SITE_PASSWORD || !process.env.AUTH_SECRET) {
    return NextResponse.json(
      { ok: false, error: "Appka není nakonfigurovaná (chybí SITE_PASSWORD/AUTH_SECRET)" },
      { status: 500 }
    );
  }

  const failKey = `login:fail:${clientIp(req)}`;
  const fails = Number((await redis.get<number>(failKey)) ?? 0);
  if (fails >= MAX_FAILS) {
    return NextResponse.json(
      { ok: false, error: "Příliš mnoho pokusů — zkus to za 15 minut" },
      { status: 429 }
    );
  }

  if (typeof password !== "string" || !password || !safeEqual(password, process.env.SITE_PASSWORD)) {
    const n = await redis.incr(failKey);
    if (n === 1) await redis.expire(failKey, WINDOW_S);
    return NextResponse.json({ ok: false, error: "Špatné heslo" }, { status: 401 });
  }

  await redis.del(failKey);
  const token = await createSession(process.env.AUTH_SECRET);
  const res = NextResponse.json({ ok: true });
  res.cookies.set("session", token, {
    httpOnly: true,
    // Na Vercelu vždy HTTPS; při lokálním `next dev` (http://localhost) by se
    // secure cookie vůbec neuložila a přihlášení by "nefungovalo".
    secure: process.env.NODE_ENV === "production",
    sameSite: "lax",
    path: "/",
    maxAge: 60 * 60 * 24 * 30,
  });
  return res;
}
