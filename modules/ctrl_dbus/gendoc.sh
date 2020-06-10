#!/bin/bash
gdbus-codegen --generate-docbook doc com.creytiv.Baresip.xml
pandoc --from docbook --to html --output ctrl_dbus.html doc-com.creytiv.Baresip.xml
rm doc-com.creytiv.Baresip.xml
