#!/usr/bin/env python3
"""Canciones de prueba para las capturas de Secuencias, sin tocar las del usuario.

  cancion_prueba.py crear [--nombre PruebaX] [--segundos 15] [--bpm 120] [--biblioteca RUTA]
  cancion_prueba.py listar [--biblioteca RUTA]
  cancion_prueba.py borrar [--biblioteca RUTA]

"crear" arma <biblioteca>/<nombre>/ con batería, bajo y voces sintéticas (48 kHz, estéreo, 16 bits), un song.json
con BPM y dos marcadores, y el archivo ".prueba" que la marca como desechable. Imprime el número que hay que pasar a
--cancion (la app pone primero las carpetas de setlist.json y después las demás, en orden alfabético).
"borrar" solo quita carpetas que tienen ".prueba": nunca toca una canción real.
Solo usa la biblioteca estándar de Python.
"""
import argparse
import json
import math
import os
import shutil
import struct
import subprocess
import sys
import wave


def biblioteca_por_defecto():
    musica = ""
    try:
        musica = subprocess.run(["xdg-user-dir", "MUSIC"], capture_output=True, text=True).stdout.strip()
    except OSError:
        pass
    if not musica or musica == os.path.expanduser("~"):
        musica = os.path.expanduser("~/Music")
    return os.path.join(musica, "Secuencias")


def escribir_wav(ruta, muestras, sr=48000):
    with wave.open(ruta, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(sr)
        datos = bytearray()
        for v in muestras:
            s = max(-32767, min(32767, int(v * 32767)))
            datos += struct.pack("<hh", s, s)
        w.writeframes(bytes(datos))


def sintetizar(segundos, bpm, sr=48000):
    n = int(segundos * sr)
    tiempo = 60.0 / bpm
    bateria, bajo, voces = [0.0] * n, [0.0] * n, [0.0] * n
    # Batería: un golpe corto por tiempo, más fuerte en el 1 (así la transiente y el compás se ven en la onda)
    golpe = int(0.08 * sr)
    k = 0
    while k * tiempo < segundos:
        inicio = int(k * tiempo * sr)
        fuerza = 0.9 if k % 4 == 0 else 0.5
        for i in range(golpe):
            if inicio + i >= n:
                break
            bateria[inicio + i] = fuerza * math.exp(-i / (0.015 * sr)) * math.sin(2 * math.pi * 60 * i / sr)
        k += 1
    # Bajo: una nota por compás (La, Fa, Do, Sol), Voces: un seno que sube y baja de volumen
    notas = [55.0, 43.65, 65.41, 49.0]
    for i in range(n):
        t = i / sr
        compas = int(t / (4 * tiempo))
        bajo[i] = 0.35 * math.sin(2 * math.pi * notas[compas % 4] * t)
        voces[i] = 0.25 * (0.5 + 0.5 * math.sin(2 * math.pi * 0.25 * t)) * math.sin(2 * math.pi * 440 * t)
    return bateria, bajo, voces


AUDIO = (".wav", ".aif", ".aiff", ".flac", ".mp3", ".ogg")
RESERVADAS = ("_mixes", "_bancos")


def es_cancion(raiz, c):
    """Como Library::load: una carpeta con archivos de audio que no sea _mixes ni _bancos."""
    carpeta = os.path.join(raiz, c)
    return (os.path.isdir(carpeta) and c not in RESERVADAS
            and any(f.lower().endswith(AUDIO) for f in os.listdir(carpeta)))


def carpetas_ordenadas(raiz):
    """Orden en que la app muestra las canciones: setlist.json primero, luego el resto en orden alfabético."""
    try:
        with open(os.path.join(raiz, "setlist.json"), encoding="utf-8") as f:
            lista = []
            for c in json.load(f):
                if c not in lista and es_cancion(raiz, c):
                    lista.append(c)
    except (OSError, ValueError):
        lista = []
    resto = sorted(c for c in os.listdir(raiz) if c not in lista and es_cancion(raiz, c))
    return lista + resto


def crear(args):
    raiz = args.biblioteca
    carpeta = os.path.join(raiz, args.nombre)
    if os.path.exists(carpeta) and not os.path.exists(os.path.join(carpeta, ".prueba")):
        sys.exit(f"{carpeta} existe y no es una canción de prueba: elige otro --nombre")
    os.makedirs(carpeta, exist_ok=True)
    bateria, bajo, voces = sintetizar(args.segundos, args.bpm)
    for archivo, muestras in (("drums.wav", bateria), ("bass.wav", bajo), ("vocals.wav", voces)):
        escribir_wav(os.path.join(carpeta, archivo), muestras)
    cancion = {
        "name": args.nombre, "bpm": args.bpm, "clickOffset": 0.0,
        "markers": [{"name": "Verso", "seconds": 0.0}, {"name": "Coro", "seconds": round(args.segundos / 2, 3)}],
        "stems": [{"name": n, "file": f} for n, f in (("Batería", "drums.wav"), ("Bajo", "bass.wav"), ("Voces", "vocals.wav"))],
    }
    with open(os.path.join(carpeta, "song.json"), "w", encoding="utf-8") as f:
        json.dump(cancion, f, ensure_ascii=False, indent=2)
    with open(os.path.join(carpeta, ".prueba"), "w", encoding="utf-8") as f:
        f.write("canción de prueba para capturas: se puede borrar\n")
    orden = carpetas_ordenadas(raiz)
    numero = orden.index(args.nombre) + 1 if args.nombre in orden else "?"
    print(f"Creada {carpeta}")
    print(f"--cancion={numero}")


def listar(args):
    for i, c in enumerate(carpetas_ordenadas(args.biblioteca), 1):
        marca = "  (prueba)" if os.path.exists(os.path.join(args.biblioteca, c, ".prueba")) else ""
        print(f"{i:3}  {c}{marca}")


def borrar(args):
    borradas = 0
    for c in os.listdir(args.biblioteca):
        carpeta = os.path.join(args.biblioteca, c)
        if os.path.isdir(carpeta) and os.path.exists(os.path.join(carpeta, ".prueba")):
            shutil.rmtree(carpeta)
            print(f"Borrada {carpeta}")
            borradas += 1
    if borradas == 0:
        print("No hay canciones de prueba")


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("accion", choices=["crear", "listar", "borrar"])
    p.add_argument("--biblioteca", default=biblioteca_por_defecto())
    p.add_argument("--nombre", default="PruebaX")
    p.add_argument("--segundos", type=float, default=15.0)
    p.add_argument("--bpm", type=float, default=120.0)
    args = p.parse_args()
    {"crear": crear, "listar": listar, "borrar": borrar}[args.accion](args)


if __name__ == "__main__":
    main()
