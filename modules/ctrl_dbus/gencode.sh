#!/bin/bash

gdbus-codegen --generate-c-code baresipbus --c-namespace DBus --interface-prefix com.creytiv. com.creytiv.Baresip.xml
