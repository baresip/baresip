How to use
----------
- Increase the `DEBUG_LEVEL` to `6` and re-build `re`

- At the call target automatic answer mode should be configured.
  E.g. if baresip then the account parameter `;answermode=auto`

- Generate the plots
```
cd tools/jbuf
cp env-template .env
# edit .env
./generate_plot.sh
```

- The plots are collected in sub-directory plots.

Units in the Plots
------------------

The variables `rdiff`, `wish` and `n` are given in number of packets.
The events "underflow", "overrun", "too late", "duplicate", "out of sequence"
and "lost" are printed only as dimensionless dots with a `y` value somewhere
in the middle of the diagram.
