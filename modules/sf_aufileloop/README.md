# sf_aufileloop

`sf_aufileloop` is a Sipfront Baresip audio source module for gapless looping of
short MP3 or WAV files during calls.

It exists because the stock `aufile` source stops at end-of-file.  That makes
Sipfront test calls depend on long pre-expanded audio files.  This module keeps
the standard Baresip source contract, but makes the source infinite by wrapping
the file read cursor back to the first sample whenever the end of the decoded
file buffer is reached.

Unlike the stock `aufile` source, this module owns the playback preparation for
looped test media.  It decodes the configured file with FFmpeg when the source
is allocated, converts it to packed signed 16-bit PCM, and resamples/remixes it
to the active transmit codec's sample rate and channel count.  That means the
same input file can be used for narrowband PCMA/PCMU calls and wideband or
stereo Opus calls without a separate agent-side conversion step.

## Runtime usage

Load the module:

```text
module                 sf_aufileloop.so
```

Use it as the call audio source:

```text
audio_source           sf_aufileloop,/usr/share/sipfront-baresip/testcall-w-clicks-22kHz.mp3
```

Or switch an established call through the `menu`/`ctrl_tcp` `ausrc` command:

```text
ausrc sf_aufileloop,/usr/share/sipfront-baresip/testcall-w-clicks-22kHz.mp3
```

## Supported input

The module accepts local files with these suffixes:

- `.mp3`
- `.wav`

The file is decoded into the exact 16-bit PCM format needed by the active call
once at source allocation time.  Runtime audio frame production is only bounded
copying from the decoded buffer, with cursor wraparound when the frame spans
the end of the file.

## Performance properties

This module is designed for high call volume:

- no per-loop file I/O
- no per-loop decoder setup
- no external process
- no GStreamer/FFmpeg pipeline
- no per-audio-frame resampling
- one source thread per active Baresip source, matching the stock `aufile`
  source model

CPU cost is paid once when Baresip switches to the source.  Memory use is the
decoded PCM size after conversion to the active transmit format.  That is
intentional: Sipfront should use this module with short files and loop them,
instead of pre-generating hour-long media files.

## Baresip update model

The module is intentionally out-of-tree.  Updating `sipfront/baresip` should not
require replaying an `aufile` patch.  The only coupling is the Baresip module
API used here:

- `ausrc_register()`
- `struct ausrc_prm`
- `ausrc_read_h`
- `audio_codec()`

If a future Baresip/libre update changes those APIs, this module should fail at
compile time inside the `sipfront-agent-base` build, instead of silently changing
runtime behavior.

The module also links against FFmpeg's `avformat`, `avcodec`, `avutil`, and
`swresample` libraries.  Those build and runtime dependencies are provided by
the `sipfront-agent-base` Docker image.
