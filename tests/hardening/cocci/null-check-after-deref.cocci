// SPDX-License-Identifier: Apache-2.0
//
// A pointer that is dereferenced and only afterwards compared with null:
// either the check is dead code or the dereference can crash.
//
// Only variables are followed: a call such as app->Page() may return a
// different pointer each time. Confidence: moderate. A range-for variable
// gets a new value each turn, which Coccinelle does not see: a check at the
// top of such a loop after a dereference at its bottom is a false alarm.

virtual report

@r exists@
symbol nullptr;
idexpression E;
expression E2;
identifier f;
position p1, p2;
@@

E@p1->f
... when != E = E2
    when != &E
(
E@p2 == NULL
|
E@p2 != NULL
|
E@p2 == nullptr
|
E@p2 != nullptr
|
!E@p2
)

@script:python depends on r@
p1 << r.p1;
p2 << r.p2;
@@
coccilib.report.print_report(p2[0], "null-check-after-deref: compared with null after the dereference on line %s" % p1[0].line)
