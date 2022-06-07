#pragma once

#include <assert.h>
#include <ctime>
#include <fstream>
#include <iostream>
#include <limits>
#include <math.h>
#include <unistd.h>

#include "../bdd.h"
#include "../nodes/nodes.h"
#include "../visitor.h"

namespace BDD {

class GraphvizGenerator : public BDDVisitor {
private:
  std::ostream &os;
  std::unordered_set<uint64_t> processed;
  const Node *next;
  bool show_init_graph;

  const char *COLOR_PROCESSED = "gray";
  const char *COLOR_NEXT = "cyan";

public:
  GraphvizGenerator(std::ostream &_os)
      : os(_os), next(nullptr), show_init_graph(true) {}

  GraphvizGenerator(std::ostream &_os,
                    const std::unordered_set<uint64_t> &_processed,
                    const Node *_next)
      : os(_os), processed(_processed), next(_next), show_init_graph(true) {}

  void set_show_init_graph(bool _show_init_graph) {
    show_init_graph = _show_init_graph;
  }

  static void visualize(const BDD &bdd, bool interrupt = true,
                        bool process_only = true) {
    constexpr int fname_len = 15;
    constexpr const char *prefix = "/tmp/";
    constexpr const char *alphanum = "0123456789"
                                     "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                     "abcdefghijklmnopqrstuvwxyz";

    auto random_fname_generator = []() {
      std::stringstream ss;
      static unsigned counter = 1;

      ss << prefix;

      srand((unsigned)std::time(NULL) * getpid() + counter);

      for (int i = 0; i < fname_len; i++) {
        ss << alphanum[rand() % (strlen(alphanum) - 1)];
      }

      ss << ".gv";

      counter++;
      return ss.str();
    };

    auto open_graph = [](const std::string &fpath) {
      std::string file_path = __FILE__;
      std::string dir_path = file_path.substr(0, file_path.rfind("/"));
      std::string script = "open_graph.sh";
      std::string cmd = dir_path + "/" + script + " " + fpath;

      system(cmd.c_str());
    };

    auto random_fname = random_fname_generator();
    auto file = std::ofstream(random_fname);
    assert(file.is_open());

    GraphvizGenerator gv(file);
    gv.set_show_init_graph(!process_only);
    gv.visit(bdd);

    file.close();

#ifdef NDEBUG
    std::cerr << "Opening " << random_fname << "\n";
#endif

    open_graph(random_fname);

    if (interrupt) {
      std::cout << "Press Enter to continue ";
      std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    }
  }

  std::string callpaths_list_to_str(const Node *node) {
    std::vector<int> call_path_ids;
    std::stringstream ss;

    if (node->get_call_paths_filenames().size() == 0) {
      return ss.str();
    }

    for (auto cp : node->get_call_paths_filenames()) {
      int call_path_id;
      sscanf(cp.c_str(), "test%d", &call_path_id);
      call_path_ids.push_back(call_path_id);
    }

    std::sort(call_path_ids.begin(), call_path_ids.end());

    if (call_path_ids.size() == 1) {
      ss << call_path_ids[0];
      return ss.str();
    }

    int start = call_path_ids[0];
    int end = start;
    bool started = false;
    bool print = false;

    auto dump = [](std::stringstream &ss, int start, int end, bool &started) {
      if (started) {
        ss << ",";
      } else {
        started = true;
      }

      if (start != end) {
        ss << start << "-" << end;
      } else {
        ss << start;
      }
    };

    for (int i = 1; i < call_path_ids.size(); i++) {
      auto id = call_path_ids[i];

      if (id == end + 1) {
        end = id;
      } else {
        dump(ss, start, end, started);
        start = end = id;
      }

      if (i == call_path_ids.size() - 1) {
        dump(ss, start, end, started);
      }
    }

    return ss.str();
  }

  void visit(const BDD &bdd) override {
    os << "digraph mygraph {\n";
    os << "\tnode [shape=box style=rounded border=0];\n";

    if (show_init_graph) {
      assert(bdd.get_init());
      visitInitRoot(bdd.get_init().get());
    }

    assert(bdd.get_process());
    visitProcessRoot(bdd.get_process().get());

    os << "}";
  }

  Action visitBranch(const Branch *node) override {
    if (node->get_next()) {
      assert(node->get_on_true()->get_prev());
      assert(node->get_on_true()->get_prev()->get_id() == node->get_id());

      assert(node->get_on_false()->get_prev());
      assert(node->get_on_false()->get_prev()->get_id() == node->get_id());
    }

    auto condition = node->get_condition();

    assert(node->get_on_true());
    node->get_on_true()->visit(*this);

    assert(node->get_on_false());
    node->get_on_false()->visit(*this);

    os << "\t\t" << get_gv_name(node);
    os << " [shape=Mdiamond, label=\"";

    os << node->get_id() << ":";
    os << pretty_print_expr(condition);

    auto i = 0u;
    os << "\\ncps={";
    os << callpaths_list_to_str(node);
    os << "}";
    os << "\"";

    if (processed.find(node->get_id()) != processed.end()) {
      os << ", color=" << COLOR_PROCESSED;
    } else if (next && node->get_id() == next->get_id()) {
      os << ", color=" << COLOR_NEXT;
    } else {
      os << ", color=yellow";
    }

    os << "];\n";

    os << "\t\t" << get_gv_name(node);
    os << " -> ";
    os << get_gv_name(node->get_on_true().get());
    os << " [label=\"True\"];\n";

    os << "\t\t" << get_gv_name(node);
    os << " -> ";
    os << get_gv_name(node->get_on_false().get());
    os << " [label=\"False\"];\n";

    return STOP;
  }

  Action visitCall(const Call *node) override {
    if (node->get_next()) {
      if (!node->get_next()->get_prev()) {
        std::cerr << "ERROR IN " << node->dump(true) << "\n";
        std::cerr << " => " << node->get_next()->dump(true) << "\n";
      }
      assert(node->get_next()->get_prev());
      assert(node->get_next()->get_prev()->get_id() == node->get_id());
    }
    auto call = node->get_call();

    assert(node->get_next());
    node->get_next()->visit(*this);

    os << "\t\t" << get_gv_name(node);
    os << " [label=\"";
    auto i = 0u;

    os << node->get_id() << ":";
    os << call.function_name;
    os << "(";

    i = 0;
    for (const auto &pair : call.args) {
      if (call.args.size() > 1) {
        os << "\\l";
        os << std::string(2, ' ');
      }

      os << pair.first << ":";
      arg_t arg = pair.second;

      if (arg.fn_ptr_name.first) {
        os << arg.fn_ptr_name.second;
      } else {
        os << pretty_print_expr(arg.expr);

        if (!arg.in.isNull() || !arg.out.isNull()) {
          os << "[";

          if (!arg.in.isNull()) {
            os << pretty_print_expr(arg.in);
          }

          if (!arg.out.isNull() &&
              (arg.in.isNull() ||
               !solver_toolbox.are_exprs_always_equal(arg.in, arg.out))) {
            os << " -> ";
            os << pretty_print_expr(arg.out);
          }

          os << "]";
        }
      }

      if (i != call.args.size() - 1) {
        os << ",";
      }

      // os << pretty_print_expr(arg.expr);

      i++;
    }

    os << ")\\l";

    i = 0;
    os << " cps={";
    os << callpaths_list_to_str(node);
    os << "}\\l";

    os << "\", ";

    if (processed.find(node->get_id()) != processed.end()) {
      os << "color=" << COLOR_PROCESSED;
    } else if (next && node->get_id() == next->get_id()) {
      os << "color=" << COLOR_NEXT;
    } else {
      os << "color=cornflowerblue";
    }

    os << "];\n";

    os << "\t\t" << get_gv_name(node);
    os << " -> ";
    os << get_gv_name(node->get_next().get());
    os << ";\n";

    return STOP;
  }

  Action visitReturnInit(const ReturnInit *node) override {
    auto value = node->get_return_value();

    os << "\t\t\"return ";
    switch (value) {
    case ReturnInit::ReturnType::SUCCESS: {
      os << "1\"";

      if (processed.find(node->get_id()) != processed.end()) {
        os << " [color=" << COLOR_PROCESSED << "]";
      } else if (next && node->get_id() == next->get_id()) {
        os << " [color=" << COLOR_NEXT << "]";
      } else {
        os << " [color=chartreuse2]";
      }

      break;
    }
    case ReturnInit::ReturnType::FAILURE: {
      os << "0\"";

      if (processed.find(node->get_id()) != processed.end()) {
        os << " [color=" << COLOR_PROCESSED << "]";
      } else if (next && node->get_id() == next->get_id()) {
        os << " [color=" << COLOR_NEXT << "]";
      } else {
        os << " [color=brown1]";
      }

      break;
    }
    default: {
      assert(false);
    }
    }
    os << ";\n";

    return STOP;
  }

  Action visitReturnProcess(const ReturnProcess *node) override {
    auto value = node->get_return_value();
    auto operation = node->get_return_operation();

    auto i = 0u;
    std::stringstream cps;
    cps << "\\lcps={";
    cps << callpaths_list_to_str(node);
    cps << "}\\l";

    os << "\t\t" << get_gv_name(node);
    os << " [label=\"";
    os << node->get_id() << ":";
    switch (operation) {
    case ReturnProcess::Operation::FWD: {
      os << "fwd(" << value << ")";
      os << cps.str();
      os << "\", ";

      if (processed.find(node->get_id()) != processed.end()) {
        os << " color=" << COLOR_PROCESSED << "]";
      } else if (next && node->get_id() == next->get_id()) {
        os << " color=" << COLOR_NEXT << "]";
      } else {
        os << "color=chartreuse2]";
      }

      break;
    }
    case ReturnProcess::Operation::DROP: {
      os << "drop()";
      os << cps.str();
      os << "\", ";

      if (processed.find(node->get_id()) != processed.end()) {
        os << " color=" << COLOR_PROCESSED << "]";
      } else if (next && node->get_id() == next->get_id()) {
        os << " color=" << COLOR_NEXT << "]";
      } else {
        os << "color=brown1]";
      }

      break;
    }
    case ReturnProcess::Operation::BCAST: {
      os << "bcast()";
      os << cps.str();
      os << "\", ";

      if (processed.find(node->get_id()) != processed.end()) {
        os << " color=" << COLOR_PROCESSED << "]";
      } else if (next && node->get_id() == next->get_id()) {
        os << " color=" << COLOR_NEXT << "]";
      } else {
        os << "color=purple]";
      }

      break;
    }
    default: {
      assert(false);
    }
    }
    os << ";\n";
    return STOP;
  }

  void visitInitRoot(const Node *root) override {
    os << "\tsubgraph clusterinit {\n";
    os << "\t\tlabel=\"nf_init\";\n";
    os << "\t\tnode [style=\"rounded,filled\",color=white];\n";

    root->visit(*this);

    os << "\t}\n";
  }

  void visitProcessRoot(const Node *root) override {
    os << "\tsubgraph clusterprocess {\n";
    if (show_init_graph) {
      os << "\t\tlabel=\"nf_process\"\n";
    }
    os << "\t\tnode [style=\"rounded,filled\",color=white];\n";

    root->visit(*this);

    os << "\t}\n";
  }

private:
  std::string get_gv_name(const Node *node) const {
    assert(node);

    std::stringstream stream;

    if (node->get_type() == Node::NodeType::RETURN_INIT) {
      const ReturnInit *ret = static_cast<const ReturnInit *>(node);

      stream << "\"return ";
      switch (ret->get_return_value()) {
      case ReturnInit::ReturnType::SUCCESS: {
        stream << "1";
        break;
      }
      case ReturnInit::ReturnType::FAILURE: {
        stream << "0";
        break;
      }
      default: {
        assert(false);
      }
      }
      stream << "\"";

      return stream.str();
    }

    stream << node->get_id();
    return stream.str();
  }
};
} // namespace BDD
