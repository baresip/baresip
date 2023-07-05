#!/usr/bin/gnuplot
#
# How to generate a plot
# ======================
# This gnuplot script plots DEBUG_LEVEL 6 output of ajb.c. You have to
# increment the DEBUG_LEVEL in ajb.c if you want to get the table for
# ajb.dat. Then call baresip like this:
#
# ./baresip 2>&1 | grep -Eo "plot_ajb.*" > ajb.dat
#
# Call this script. Then compare the plot legend with the variables in ajb.c!
#
#
# Description of the plot
# =======================
# The plot is a time based diagram. The values avbuftime should lie between
# bufmin and bufmax. If it runs somewhere out of these boundaries a "Low" /
# "High" situation is detected.
#
# "Good" means: The number of frames in the audio buffer is ok.
#
# "Low" means:  The number is too low. Then the number of frames are
#               incremented by holding one frame back.
#
# "High" means: The number is to high. Then one frame is dropped. This reduces
#               the audio delay.
#
# The number of "Low"/"High" situations should be low while buffer under-runs
# should be avoided completely.

# On the x-axes of the plot there is the time in milliseconds. See function
# `ajb_calc()`! We note the variables in ajb.c here in parentheses.
#  E.g. (var jitter).
#
# - The orange line is the computed network jitter (var jitter). This is a
#   moving average of the difference (var d) between the real time diff
#   (var tr - var tr0) and the RTP timestamps diff (var ts - var ts0).
#   See RFC-3550 RTP - A.8!
#   We suggest a fast rise of the moving average and a slow shrink. Thus
#   avoiding buffer under-runs have a higher priority than reducing the audio
#   delay.
#
# - The buftime (var buftime) is the whole time period stored in the aubuf.
#   The buftime (light-grey) changes very fast during periods of jitter. To be
#   applicable for detecting "Low" or "High" situations it has to be smoothed.
#   The blue line avbuftime (var avbuftime) is a moving average of the buftime
#   and is used to detect "Low"/"High". Thus the ajb algorithm tries to keep
#   the avbuftime between the following boundaries.
#
# - The green lines bufmin and bufmax (var bufmin, bufmax) are boundaries for
#   avbuftime.They are computed by constant factors (> 1.) from the jitter.
#
#
# Copyright (C) 2020,2022 commend.com - Christian Spielberger, Michael Peitler


# Choose your preferred gnuplot terminal or use e.g. evince to view the
# ajb.eps!

#set terminal qt persist
set terminal postscript eps size 15,10 enhanced color
set output 'ajb.eps'
#set terminal png size 1280,480
#set output 'ajb.png'
set datafile separator ","
set key outside
set xlabel "time/[ms]"
set ylabel "[ms]"

stats "ajb.dat" using ($6/1000) name "B"
stats "ajb.dat" using 3 name "X"

bufst(y) = y>0 ? B_max*0.8 + y*B_max*0.09 : NaN
text1(y) = y==1 ? "LOW" : y==2 ? "HIGH" : ""
text2(y) = y==1 ? "UNDERRUN" : ""
underr(y) =  B_max*0.7 + y*B_max*0.09

plot \
'ajb.dat' using 3:($5/1000) title 'jitter' with linespoints lc "orange", \
'ajb.dat' using 3:($7/1000) title 'avbuftime' with linespoints lc "skyblue", \
'ajb.dat' using 3:($8/1000) title 'bufmin' with linespoints lc "sea-green", \
'ajb.dat' using 3:($9/1000) title 'bufmax' with linespoints lc "sea-green", \
'ajb.dat' using 3:($6/1000) title 'buftime' lc "light-grey", \
'ajb.dat' using 3:(bufst($10)) title 'Low/High' lc "red", \
"<echo 1 1 1" using (X_max):(bufst(2)):(text1(2)) notitle w labels tc  "red", \
"<echo 1 1 1" using (X_max):(bufst(1)):(text1(1)) notitle w labels tc  "red", \
'underrun.dat' using 3:(underr($4)) title 'underrun' pt 7 ps 1.5 lc "red", \
"<echo 1 1 1" using (X_max):(underr(1)):(text2(1)) notitle w labels tc "red"
