#!/bin/bash
gdbus-codegen --generate-c-code baresipbus --c-namespace DBus \
    --interface-prefix com.creytiv. com.creytiv.Baresip.xml
if pandoc -v &> /dev/null
then
    gdbus-codegen --generate-docbook doc com.creytiv.Baresip.xml
    pandoc --from docbook --to html --output ctrl_dbus.html \
        doc-com.creytiv.Baresip.xml
rm doc-com.creytiv.Baresip.xml
fi
