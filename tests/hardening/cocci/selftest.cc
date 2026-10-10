// SPDX-License-Identifier: Apache-2.0
//
// Cases for the rules in this directory. static-analysis.sh --selftest runs
// every rule over this file and fails unless each rule reports exactly the
// lines marked "expect: <rule>". Not compiled.

struct App { int x; void Run(); };
struct Mgr { App* FindAppById(const char*); App* FindAppByInstanceId(const char*); };
App* FindAppById(const char*);

void bad1(Mgr* m) { App* a = m->FindAppById("x"); a->Run(); }  // expect: unchecked-lookup
void good1(Mgr* m) { App* a = m->FindAppById("x"); if (!a) return; a->Run(); }
void bad2(App* a) { int y = a->x; if (a == nullptr) return; (void)y; }  // expect: null-check-after-deref
void good2(App* a) { if (!a) return; a->Run(); }
void bad3() { App* a = new App; delete a; a->Run(); }  // expect: use-after-free
void good3() { App* a = new App; delete a; a = nullptr; }
void bad4() { App* a = FindAppById("y"); a->Run(); }  // expect: unchecked-lookup
void bad5(Mgr* m) { App* a; a = m->FindAppByInstanceId("z"); int v = a->x; (void)v; }  // expect: unchecked-lookup
void good5(Mgr* m) { if (App* a = m->FindAppById("x")) a->Run(); }
void bad6(App* a) { a->Run(); if (!a) return; }  // expect: null-check-after-deref
void good6(Mgr* m) { App* a = m->FindAppByInstanceId("i"); if (a == nullptr || a->x != 1) return; }
void good7(Mgr* m) { App* a = m->FindAppById("i"); if (a && a->x) a->Run(); }
void good8(Mgr* m, App* o) { App* a = m->FindAppById("i"); if (a && a != o && a->x) return; }
void good9(App** l, int n) { for (int i = 0; i < n; i++) { App* a = l[i]; delete a; } }
