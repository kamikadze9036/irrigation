import fs from "fs";
import path from "path";

// dashboard.template.html je totožné UI jako lokální webui.cpp na ESP32 (stejný
// HTML/CSS/JS), jen s upravenou api() funkcí, která místo přímého fetch() na
// relativní cestu volá /api/proxy — ten požadavek doručí zařízení přes polling tunel.
export function getDashboardHtml(): string {
  const filePath = path.join(process.cwd(), "lib", "dashboard.template.html");
  return fs.readFileSync(filePath, "utf-8");
}
