// SPDX-License-Identifier: Apache-2.0
//
// A pointer used after delete, g_free() or free() without being given a new
// value first.
//
// Confidence: high for locals; for members, a callee that resets the member
// is not seen, so check the path by hand.

virtual report

@r exists@
expression E, E2;
position p1, p2;
@@

(
delete E@p1;
|
delete[] E@p1;
|
g_free(E@p1);
|
free(E@p1);
)
... when != E = E2
(
E = E2
|
E@p2
)

@script:python depends on r@
p1 << r.p1;
p2 << r.p2;
@@
# The free itself, reached again through a loop's back edge: a range-for
# variable gets a new value each turn, which Coccinelle does not see
if (p1[0].line, p1[0].column) != (p2[0].line, p2[0].column):
    coccilib.report.print_report(p2[0], "use-after-free: used after it was freed on line %s" % p1[0].line)
