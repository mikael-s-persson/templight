// RUN: rm -f %t.trace.pbf

// RUN: %templight_cc1 %s -Xtemplight -profiler \
// RUN:   -Xtemplight -output=%t.trace.pbf

// grep the filename in the output trace file. That shouldn't be mangled.
// RUN: grep "%s" %t.trace.pbf
