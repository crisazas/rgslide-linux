#!/usr/bin/env python3
"""Genera gamelist.xml (nombre, descripcion, caratula simple propia) de las ROMs libres para EmulationStation.
Salida: rootfs/overlay/usr/share/rgslide/gamelists/<sistema>/{gamelist.xml,images/*.png}. Datos de docs: roms-libres/CREDITOS.md."""
import os, html
from PIL import Image, ImageDraw, ImageFont
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "rootfs/overlay/usr/share/rgslide/gamelists")
FONT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "../third_party/EmulationStation/resources/opensans_hebrew_condensed_regular.ttf")
G = {
 "gb": ("Game Boy", (60,90,50), [
  ("adjustris.gb","Adjustris","Dave VanEe (tbsp)","2021","Puzzle","Un Tetris en el que ajustas las piezas a una cuadricula que cambia. Software libre (CC0 1.0)."),
  ("libbet.gb","Libbet and the Magic Floor","Damian Yerrick","2019","Puzzle","Libbet debe pisar las baldosas del suelo magico sin quedarse atrapado. Software libre (zlib)."),
  ("rgtest.gb","RG Test (prueba del proyecto)","Proyecto rgslide","2026","Prueba","ROM minima de comprobacion de la RG Slide: solo comprueba que el emulador arranca.")]),
 "gbc": ("Game Boy Color", (90,50,110), [
  ("ucity.gbc","uCity","Antonio Nino Diaz","2018","Simulacion","Construye y gestiona una ciudad en tu Game Boy Color. Codigo GPL-3.0; graficos y musica CC BY-SA 4.0.")]),
 "gba": ("Game Boy Advance", (50,50,130), [
  ("glacia.gba","Glacia Dungeon","NotImplementedLife","2022","Aventura","Explora mazmorras heladas en esta aventura hecha para la GBAJam 2022. Software libre (GPL-3.0)."),
  ("notebook.gba","Notebook Adventure","NotImplementedLife","2023","Plataformas","Plataformas dibujado sobre las hojas de un cuaderno (Retro Platform Jam #4). Software libre (GPL-3.0).")]),
 "nes": ("NES", (130,40,40), [
  ("nova.nes","Nova the Squirrel","NovaSquirrel","2018","Plataformas","Una ardilla con poderes recorre niveles de plataformas. Codigo GPL-3.0; recursos CC BY-NC-SA 4.0 (uso no comercial)."),
  ("thwaite.nes","Thwaite","Damian Yerrick","2011","Accion","Defiende tus ciudades de los misiles que caen, al estilo Missile Command. Software libre (GPL-3.0).")]),
 "nds": ("Nintendo DS", (120,120,125), [
  ("delve.nds","Delve","Michal Pandyra","2025","Aventura","Juego de exploracion de mazmorras para Nintendo DS (BlocksDS). Codigo MIT; arte y musica CC0.")]),
}
def cover(path, title, sysname, col):
    im = Image.new("RGB", (480, 640), col); d = ImageDraw.Draw(im)
    d.rectangle([12,12,467,627], outline=(235,235,235), width=4)
    f = ImageFont.truetype(FONT, 44); s = ImageFont.truetype(FONT, 28)
    words, lines, cur = title.split(), [], ""
    for w in words:
        t = (cur+" "+w).strip()
        if d.textlength(t, font=f) > 400 and cur: lines.append(cur); cur = w
        else: cur = t
    lines.append(cur)
    y = 260 - 28*len(lines)
    for l in lines:
        d.text((240, y), l, font=f, fill=(255,255,255), anchor="ma"); y += 56
    d.text((240, 560), sysname, font=s, fill=(225,225,225), anchor="ma"); im.save(path)
for sysid, (sysname, col, games) in G.items():
    base = os.path.join(OUT, sysid); os.makedirs(os.path.join(base, "images"), exist_ok=True)
    x = ['<?xml version="1.0"?>', '<gameList>']
    for fn, name, dev, year, genre, desc in sorted(games, key=lambda g: g[1].lower()):
        stem = os.path.splitext(fn)[0]; cover(os.path.join(base, "images", stem+".png"), name, sysname, col)
        e = html.escape
        x += ['\t<game>', f'\t\t<path>./{fn}</path>', f'\t\t<name>{e(name)}</name>', f'\t\t<desc>{e(desc)}</desc>',
              f'\t\t<image>~/.emulationstation/gamelists/{sysid}/images/{stem}.png</image>', f'\t\t<releasedate>{year}0101T000000</releasedate>',
              f'\t\t<developer>{e(dev)}</developer>', f'\t\t<genre>{e(genre)}</genre>', '\t</game>']
    x.append('</gameList>')
    open(os.path.join(base, "gamelist.xml"), "w").write("\n".join(x)+"\n")
print("ok")
