#!/bin/bash
gdbus-codegen --generate-c-code baresipbus --c-namespace DBus \
    --interface-prefix com.github. com.github.Baresip.xml
if pandoc -v &> /dev/null
then
    gdbus-codegen --generate-docbook doc com.github.Baresip.xml
    pandoc --from docbook --to html --output ctrl_dbus.html \
        doc-com.github.Baresip.xml
    rm doc-com.github.Baresip.xml
fi
