#include <string.h>

#include "App.h"

// Function-local statics with constant initialisers are ready before any
// dynamic initialisation runs, so REGISTER_APP can execute in any order.
App*& AppRegistry::head() {
  static App* h = nullptr;
  return h;
}

bool& AppRegistry::unsorted() {
  static bool u = false;
  return u;
}

void AppRegistry::add(App& app) {
  // No logging here: this runs during static initialisation.
  for (App* a = head(); a != nullptr; a = a->_next) {
    if (a == &app) {
      return;
    }
  }
  app._next = head();
  head() = &app;
  unsorted() = true;
}

static int compareApps(const App* a, const App* b) {
  if (a->info().order != b->info().order) {
    return a->info().order < b->info().order ? -1 : 1;
  }
  return strcmp(a->id(), b->id());
}

void AppRegistry::sortIfNeeded() {
  if (!unsorted()) {
    return;
  }
  unsorted() = false;
  App* sorted = nullptr;  // insertion sort: the list is tiny
  App* a = head();
  while (a != nullptr) {
    App* nxt = a->_next;
    if (sorted == nullptr || compareApps(a, sorted) < 0) {
      a->_next = sorted;
      sorted = a;
    } else {
      App* p = sorted;
      while (p->_next != nullptr && compareApps(p->_next, a) <= 0) {
        p = p->_next;
      }
      a->_next = p->_next;
      p->_next = a;
    }
    a = nxt;
  }
  head() = sorted;
}

size_t AppRegistry::count() {
  size_t n = 0;
  for (App* a = head(); a != nullptr; a = a->_next) {
    n++;
  }
  return n;
}

App* AppRegistry::find(const char* id) {
  if (id == nullptr) {
    return nullptr;
  }
  for (App* a = head(); a != nullptr; a = a->_next) {
    if (strcmp(a->id(), id) == 0) {
      return a;
    }
  }
  return nullptr;
}

App* AppRegistry::first() {
  sortIfNeeded();
  return head();
}
