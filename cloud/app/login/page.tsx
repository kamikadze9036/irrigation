"use client";
import { useState, type FormEvent } from "react";

export default function LoginPage() {
  const [password, setPassword] = useState("");
  const [error, setError] = useState("");
  const [loading, setLoading] = useState(false);

  async function submit(e: FormEvent) {
    e.preventDefault();
    setLoading(true);
    setError("");
    const r = await fetch("/api/login", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ password }),
    });
    setLoading(false);
    if (r.ok) {
      window.location.href = "/";
    } else {
      const d = await r.json().catch(() => ({}));
      setError(d.error || "Přihlášení selhalo");
    }
  }

  return (
    <div
      style={{
        minHeight: "100vh",
        display: "flex",
        alignItems: "center",
        justifyContent: "center",
        background: "#1a1a2e",
        fontFamily: "Arial, sans-serif",
      }}
    >
      <form
        onSubmit={submit}
        style={{
          background: "#16213e",
          padding: 32,
          borderRadius: 10,
          width: 300,
          border: "1px solid #0f3460",
        }}
      >
        <h1 style={{ color: "#00d4aa", fontSize: 18, marginBottom: 16 }}>
          💧 Závlaha — Přihlášení
        </h1>
        <input
          type="password"
          placeholder="Heslo"
          value={password}
          onChange={(e) => setPassword(e.target.value)}
          autoFocus
          style={{
            width: "100%",
            padding: 8,
            borderRadius: 5,
            border: "1px solid #2980b9",
            background: "#0f3460",
            color: "#eee",
            marginBottom: 12,
            boxSizing: "border-box",
          }}
        />
        {error && (
          <div style={{ color: "#e74c3c", fontSize: 13, marginBottom: 12 }}>{error}</div>
        )}
        <button
          type="submit"
          disabled={loading}
          style={{
            width: "100%",
            padding: 10,
            borderRadius: 5,
            border: "none",
            background: "#00d4aa",
            color: "#000",
            fontWeight: "bold",
            cursor: "pointer",
          }}
        >
          {loading ? "Přihlašuji…" : "Přihlásit"}
        </button>
      </form>
    </div>
  );
}
