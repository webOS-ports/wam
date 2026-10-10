// SPDX-License-Identifier: Apache-2.0
//
// FindAppById() and FindAppByInstanceId() return nullptr when no app
// matches, which is the normal answer to a stale or forged id from the bus.
// Reports a lookup whose result is dereferenced before anything looks at it.
//
// Confidence: high. A result first passed to a helper that checks it is not
// reported (the helper is a use), so a miss is possible, a false alarm is not
// expected.

virtual report

// A dereference guarded in the same expression ("app && app->Id()",
// "!app || app->Id() != id") is not reported.
@g@
symbol nullptr;
expression x, E1, E2;
identifier f;
position pg;
@@

(
x && <+... x@pg->f ...+>
|
x && E1 && <+... x@pg->f ...+>
|
x && E1 && E2 && <+... x@pg->f ...+>
|
x != NULL && <+... x@pg->f ...+>
|
x != nullptr && <+... x@pg->f ...+>
|
!x || <+... x@pg->f ...+>
|
x == NULL || <+... x@pg->f ...+>
|
x == nullptr || <+... x@pg->f ...+>
|
x ? <+... x@pg->f ...+> : ...
)

@r exists@
identifier fn = {FindAppById, FindAppByInstanceId};
identifier x, f;
expression O;
position p != g.pg;
type T;
@@

(
T x = fn(...);
|
T x = O->fn(...);
|
T x = O.fn(...);
|
x = fn(...);
|
x = O->fn(...);
|
x = O.fn(...);
)
... when != x
(
x@p->f(...)
|
x@p->f
)

@script:python depends on r@
p << r.p;
fn << r.fn;
@@
coccilib.report.print_report(p[0], "unchecked-lookup: result of %s() dereferenced without a null check" % fn)
