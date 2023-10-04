#!/usr/bin/gnuplot
#
# Use generate_plot.sh!
#
# Description of the plot
# =======================
# The plot is a time based diagram.
#
# Events:
# - overrun
# - underflow
# - packet too late
# - duplicate packet
# - out of sequence
# - lost packet
# Copyright (C) 2023 commend.com - Christian Spielberger


# Choose your preferred gnuplot terminal or use e.g. evince to view the
# jbuf.eps!

#set terminal qt persist
set terminal postscript eps size 15,10 enhanced color
set output 'jbuf.eps'
#set terminal png size 1280,480
#set output 'jbuf.png'
set datafile separator ","
set key outside
set xlabel "time/[ms]"
set ylabel "#packets"

stats "jbuf.dat" using ($4) name "N"
event_h(i) = (0.5*N_max) + 0.3*N_max*(i/6.0)

plot \
'jbuf.dat' using 3:4 title 'n' with linespoints lc "skyblue", \
'jbuf.dat' using 3:5 title 'nf' with linespoints lc "blue", \
'jbuf.dat' using 3:6 title 'ncf' with linespoints lc "green", \
'jbuf.dat' using 3:7 title 'nfa' with linespoints lc "pink", \
'overrun.dat' using 3:(event_h(1)) title 'overrun' pt 7 ps 1.5 lc "#FF0000", \
'underrun.dat' using 3:(event_h(2)) title 'underrun' pt 7 ps 1.5 lc "#A52222", \
'toolate.dat' using 3:(event_h(3)) title  'toolate' pt 7 ps 1.5 lc "#BB5454", \
'duplicate.dat' using 3:(event_h(4)) title 'duplicate' pt 7 ps 1.5 lc "#E919C6", \
'oosequence.dat' using 3:(event_h(5)) title 'out of seq' pt 7 ps 1.5 lc "#BB7777", \
'lost.dat' using 3:(event_h(6)) title 'lost' pt 7 ps 1.5 lc "#C08DBC", \
'x.dat' using 3:(event_h(.5)) title 'x' pt 7 ps 1.5 lc "#407740", \
'y.dat' using 3:(event_h(.7)) title 'y' pt 7 ps 1.5 lc "#404077"
