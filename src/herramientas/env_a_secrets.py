#!/usr/bin/env python3
"""Convertir src/.env en src/teco/secrets.h para compilar en Arduino IDE.

Uso (desde la carpeta src):
    python3 herramientas/env_a_secrets.py
"""
from pathlib import Path

SRC = Path(__file__).resolve().parent.parent
ENV = SRC / ".env"
OUT = SRC / "teco" / "secrets.h"
KEYS = ("BLYNK_TEMPLATE_ID", "BLYNK_TEMPLATE_NAME", "BLYNK_AUTH_TOKEN")

if not ENV.exists():
    raise SystemExit("No existe src/.env. Copia .env.example como .env y llena tus datos.")

vals = {}
for line in ENV.read_text(encoding="utf-8").splitlines():
    line = line.strip()
    if not line or line.startswith("#") or "=" not in line:
        continue
    k, v = line.split("=", 1)
    vals[k.strip()] = v.strip().strip('"').strip("'")

lines = ["// Generado por herramientas/env_a_secrets.py - NO subir a git", "#pragma once"]
for k in KEYS:
    if vals.get(k):
        v = vals[k].replace("\\", "\\\\").replace('"', '\\"')
        lines.append(f'#define {k} "{v}"')
OUT.write_text("\n".join(lines) + "\n", encoding="utf-8")
print(f"Listo: {OUT.relative_to(SRC)} ({sum(1 for k in KEYS if vals.get(k))} valores)")
