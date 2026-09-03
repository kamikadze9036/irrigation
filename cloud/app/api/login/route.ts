import { NextRequest, NextResponse } from "next/server";
import { createSession } from "@/lib/auth";

export async function POST(req: NextRequest) {
  const { password } = await req.json().catch(() => ({ password: "" }));

  if (!process.env.SITE_PASSWORD || !process.env.AUTH_SECRET) {
    return NextResponse.json(
      { ok: false, error: "Appka není nakonfigurovaná (chybí SITE_PASSWORD/AUTH_SECRET)" },
      { status: 500 }
    );
  }
  if (!password || password !== process.env.SITE_PASSWORD) {
    return NextResponse.json({ ok: false, error: "Špatné heslo" }, { status: 401 });
  }

  const token = await createSession(process.env.AUTH_SECRET);
  const res = NextResponse.json({ ok: true });
  res.cookies.set("session", token, {
    httpOnly: true,
    secure: true,
    sameSite: "lax",
    path: "/",
    maxAge: 60 * 60 * 24 * 30,
  });
  return res;
}
