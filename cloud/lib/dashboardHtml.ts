import fs from "fs";
import path from "path";

// dashboard.template.html je 1:1 kopie HTML z webui.cpp na ESP32 — generuje ji
// skript ../tools/sync_cloud_template.sh (neupravovat ručně, změny dělej ve
// webui.cpp a skript spusť znovu). Jediný rozdíl za běhu: nastavíme
// window.CLOUD_MODE, podle kterého stránka posílá požadavky přes /api/proxy
// místo přímo na ESP32 a ukáže odhlášení + online/offline indikátor zařízení.
const CLOUD_MODE_MARKER = "<!--CLOUD_MODE-->";
const CLOUD_MODE_SCRIPT = "<script>window.CLOUD_MODE=true</script>";

let cached: string | null = null;

export function getDashboardHtml(): string {
  if (cached) return cached;
  const filePath = path.join(process.cwd(), "lib", "dashboard.template.html");
  const html = fs.readFileSync(filePath, "utf-8");
  if (!html.includes(CLOUD_MODE_MARKER)) {
    throw new Error("dashboard.template.html neobsahuje <!--CLOUD_MODE--> — spusť tools/sync_cloud_template.sh");
  }
  cached = html.replace(CLOUD_MODE_MARKER, CLOUD_MODE_SCRIPT);
  return cached;
}
