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

stats "jbuf.dat" using ($6) name "N"
event_h(i) = (0.5*N_max) + 0.3*N_max*(i/6.0)

plot \
'jbuf.dat' using 3:4 title 'rdiff' with linespoints lc "orange", \
'jbuf.dat' using 3:5 title 'wish' with linespoints lc "sea-green", \
'jbuf.dat' using 3:6 title 'n' with linespoints lc "skyblue", \
'jbuf.dat' using 3:7 title 'nf' with linespoints lc "light-blue", \
'overrun.dat' using 3:(event_h(1)) title 'overrun' pt 7 ps 1.5 lc "#FF0000", \
'underflow.dat' using 3:(event_h(2)) title 'underflow' pt 7 ps 1.5 lc "#FF4444", \
'toolate.dat' using 3:(event_h(3)) title  'toolate' pt 7 ps 1.5 lc "#BB5454", \
'duplicate.dat' using 3:(event_h(4)) title 'duplicate' pt 7 ps 1.5 lc "#E919C6", \
'oosequence.dat' using 3:(event_h(5)) title 'out of seq' pt 7 ps 1.5 lc "#A5A5A5", \
'lost.dat' using 3:(event_h(6)) title 'lost' pt 7 ps 1.5 lc "#C08DBC"
