#!/usr/bin/env python3
# Orden de carga por dependencias (insmod no resuelve deps) de los modulos USB que anade el initramfs a la ramdisk de vendor.
# Lee modules.dep del dlkm y de la ramdisk de vendor; escribe initramfs/modules/rgslide-usb.load.
import os, sys
R = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VR = R + '/dump/boot/rd_vendor/lib/modules'
DL = R + '/dump/live/dlkm'
dep = {}
for f in (VR + '/modules.dep', DL + '/modules.dep'):
    for l in open(f):
        if ':' in l:
            k, v = l.strip().split(':', 1)
            dep[os.path.basename(k)] = [os.path.basename(x) for x in v.split()]
stock = [l.strip() for l in open(VR + '/modules.load') if l.strip()]
# aw32257_charger.ko registra el regulador `vddvbus` (otg-vbus en i2c6) que dwc3-sprd pide ('unable to get vbus supply'): sin el no hay UDC.
# Orden de fabrica del recovery (modules.load.recovery): ... dwc3-sprd, sprd_typec_displayport, sprd_tcpm, sc27xx_typec, sc27xx_pd,
# sprd_battery_info, sc27xx_fuel_gauge, sprd-charger-manager, aw32257_charger.
targets = ['extcon-usb-gpio.ko', 'pinctrl-sprd.ko', 'pinctrl-sprd-qogirn6pro.ko', 'phy-sprd-commonphy.ko', 'sprd-bc1p2.ko',
           'phy-sprd-dummy.ko', 'phy-sprd-qogirn6pro.ko', 'phy-sprd-usb31-qogirn6pro.ko', 'sprd-pamu3.ko',
           'sprd_battery_info.ko', 'sprd-charger-manager.ko', 'aw32257_charger.ko', 'sc27xx_pd.ko', 'sprd_typec_displayport.ko',
           'dwc3-sprd.ko', 'sprd_usb_f_rndis.ko']
order, seen = [], set(stock)
def visit(m):
    if m in seen:
        return
    seen.add(m)
    for d in dep.get(m, []):
        visit(d)
    order.append(m)
for t in targets:
    visit(t)
have = set(os.listdir(VR)) | set(os.listdir(R + '/initramfs/modules'))
falta = [m for m in order if m not in have]
if falta:
    sys.exit('faltan .ko: %s (copiarlos de dump/live/dlkm a initramfs/modules)' % falta)
open(R + '/initramfs/modules/rgslide-usb.load', 'w').write('\n'.join(order) + '\n')
print(' '.join(order))
