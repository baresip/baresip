How to use
----------
- Build re with
```
cmake -B build -DUSE_TRACE=ON -DCMAKE_C_FLAGS="-DRE_JBUF_TRACE"
cmake --build build
```

- At the call target automatic answer mode should be configured.
  E.g. if baresip then the account parameter `;answermode=auto`

- Generate the plots
```
cd tools/jbuf
cp env-template .env
# edit .env
./plot_loop.sh
```

- The plots are collected in sub-directory plots.

Units in the Plots
------------------

The variables `rdiff` and `n` are given in number of packets.
While `wish` and `nf` are given in number of frames.
The events "underflow", "overrun", "too late", "duplicate", "out of sequence"
and "lost" are printed only as dimensionless dots with a `y` value somewhere
in the middle of the diagram.
