// Vrací stejné UI jako lokální ESP32 web (viz cloud/lib/dashboard.template.html) — ne přes
// React strom, ale jako syrový HTML dokument, aby zůstala 1:1 shoda s originálem.
export const runtime = "nodejs";

import { NextResponse } from "next/server";
import { getDashboardHtml } from "@/lib/dashboardHtml";

export async function GET() {
  return new NextResponse(getDashboardHtml(), {
    headers: { "content-type": "text/html; charset=utf-8" },
  });
}
