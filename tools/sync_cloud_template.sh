#!/usr/bin/env sh
# Vygeneruje cloud/lib/dashboard.template.html z HTML šablony v webui.cpp.
# webui.cpp je jediný zdroj pravdy — cloud appka servíruje totéž HTML, jen
# s doplněným window.CLOUD_MODE (viz cloud/lib/dashboardHtml.ts).
#
#   tools/sync_cloud_template.sh          # přegeneruje šablonu
#   tools/sync_cloud_template.sh --check  # jen ověří, že je aktuální (pro CI / pre-commit)
set -eu
cd "$(dirname "$0")/.."

SRC=webui.cpp
DST=cloud/lib/dashboard.template.html

extract() {
  awk '/R"rawhtml\(/{f=1;next} /\)rawhtml"/{f=0} f' "$SRC"
}

if [ "${1:-}" = "--check" ]; then
  if extract | diff -q - "$DST" >/dev/null; then
    echo "OK: $DST je aktuální"
  else
    echo "CHYBA: $DST neodpovídá HTML v $SRC — spusť tools/sync_cloud_template.sh" >&2
    exit 1
  fi
  exit 0
fi

extract > "$DST"
echo "OK: $DST vygenerováno ($(wc -l < "$DST" | tr -d ' ') řádků)"
