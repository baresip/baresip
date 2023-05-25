Commend Commits not for mainline
================================

onvif
-----

A commend module. Makes no sense for non-SYBF devices.


auogg
-----

Currently supports only the deprecated speex codec. No one (at Commend) is
interested on an auogg module that supports a modern codec like opus.


speex
-----

All commits that belong to the module speex which was removed mainline.
The speex codec is deprecated. We use it only on SYBF devices.


comvideo
--------

It is very special and coupled tightly to the camerad for SYMX. 


commod
------

Commend specific commands and UA event filter. General interesting modules
should go to the repository `baresip-apps` which should be free of Commend
specific commits.


alsa\_audiocore
---------------

The SYBF audio source and player module.


multicast
---------

The SYBF part of multicast are not interesting for main line.


idlepipe
--------

Used only on SYBF. Is not interesting for main line. It is used for onvif and
the SYBF multicast solution.


ac\_symphony
------------

The softclient (skidata) integration of the SYMX audiocore.


audiocore
---------

The SYBF integration of the SYBF audiocore.


