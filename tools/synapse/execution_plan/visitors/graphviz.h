#pragma once

#include "visitor.h"
#include "../execution_plan.h"
#include "../../log.h"

#include <ctime>
#include <unistd.h>

#include <fstream>

namespace synapse {

class Graphviz : public ExecutionPlanVisitor {
private:
  std::ofstream ofs;
  std::string   fpath;

  constexpr static int         fname_len = 15;
  constexpr static const char* prefix    = "/tmp/";
  constexpr static const char* alphanum  = "0123456789"
                                           "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                           "abcdefghijklmnopqrstuvwxyz";

  std::string get_rand_fname() {
    std::stringstream ss;
    static unsigned   counter = 1;

    ss << prefix;

    srand((unsigned) std::time(NULL) * getpid() + counter);

    for (int i = 0; i < fname_len; i++) {
      ss << alphanum[rand() % (strlen(alphanum) - 1)];
    }

    ss << ".gv";

    counter++;
    return ss.str();
  }

public:
  Graphviz(const std::string& path) : fpath(path) {
    ofs.open(fpath);
    assert(ofs);
  }

  Graphviz() : Graphviz(get_rand_fname()) {}

  void visit(ExecutionPlan ep) override {
    ofs << "digraph mygraph {"                  << "\n";
    ofs << "  compound=true;"                   << "\n";
    ofs << "  node [style=filled,color=white];" << "\n";

    ExecutionPlanVisitor::visit(ep);

    ofs << "}\n";

    Log::dbg() << "gv dumped to " << fpath << "\n";
  }

  void visit(const targets::x86::MapGet* node) override {}
  void visit(const targets::x86::CurrentTime* node) override {}

  ~Graphviz() {
    ofs.close();
  }
};

}
