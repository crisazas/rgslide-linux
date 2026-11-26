#!/bin/sh
exec rgslide-ui --view "Estado de la Slide" "rgslide-ctl status all; echo; rgslide-ctl net" 3
