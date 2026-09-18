// Podepsané session tokeny přes Web Crypto (funguje jak v Edge middleware, tak v Node.js) —
// žádná externí auth knihovna, jen jedno sdílené heslo + HMAC podpis.

const encoder = new TextEncoder();

async function hmacKey(secret: string) {
  return crypto.subtle.importKey(
    "raw",
    encoder.encode(secret),
    { name: "HMAC", hash: "SHA-256" },
    false,
    ["sign", "verify"]
  );
}

function toHex(buf: ArrayBuffer): string {
  return Array.from(new Uint8Array(buf))
    .map((b) => b.toString(16).padStart(2, "0"))
    .join("");
}

// Porovnání v konstantním čase — běžné `===` skončí u prvního rozdílného znaku,
// takže z doby odpovědi jde (teoreticky) odvozovat heslo/token po znacích.
// Funguje i v Edge runtime, kde není node:crypto timingSafeEqual.
export function safeEqual(a: string, b: string): boolean {
  const ab = encoder.encode(a);
  const bb = encoder.encode(b);
  let diff = ab.length ^ bb.length;
  const n = Math.max(ab.length, bb.length);
  for (let i = 0; i < n; i++) diff |= (ab[i] ?? 0) ^ (bb[i] ?? 0);
  return diff === 0;
}

const SESSION_TTL_SECONDS = 60 * 60 * 24 * 30; // 30 dní

export async function createSession(secret: string): Promise<string> {
  const expires = Math.floor(Date.now() / 1000) + SESSION_TTL_SECONDS;
  const key = await hmacKey(secret);
  const sig = await crypto.subtle.sign("HMAC", key, encoder.encode(String(expires)));
  return `${expires}.${toHex(sig)}`;
}

export async function verifySession(token: string, secret: string): Promise<boolean> {
  const [expiresStr, sig] = token.split(".");
  if (!expiresStr || !sig) return false;
  const expires = parseInt(expiresStr, 10);
  if (!Number.isFinite(expires) || Math.floor(Date.now() / 1000) > expires) return false;
  const key = await hmacKey(secret);
  const expectedSig = await crypto.subtle.sign("HMAC", key, encoder.encode(expiresStr));
  return safeEqual(toHex(expectedSig), sig);
}
