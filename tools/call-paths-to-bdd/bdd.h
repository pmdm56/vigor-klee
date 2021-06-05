#pragma once

#include "./bdd-nodes.h"

namespace BDD {

class BDD {
public:
  friend class CallPathsGroup;

private:
  solver_toolbox_t solver_toolbox;
  uint64_t id;

  std::shared_ptr<Node> nf_init;
  std::shared_ptr<Node> nf_process;

  std::vector<call_path_t *> call_paths;

  static std::vector<std::string> skip_conditions_with_symbol;

  static constexpr char INIT_CONTEXT_MARKER[] = "start_time";

private:
  call_t get_successful_call(std::vector<call_path_t *> call_paths) const;
  Node *populate(call_paths_t call_paths);

  static std::string get_fname(const Node *node);
  static bool is_skip_function(const Node *node);
  static bool is_skip_condition(const Node *node);

  Node *populate_init(const Node *root);
  Node *populate_process(const Node *root, bool store = false);

  void add_node(call_t call);
  void dump(int lvl, const Node *node) const;

  uint64_t get_and_inc_id() {
    uint64_t _id = id;
    id++;
    return _id;
  }

public:
  BDD(std::vector<call_path_t *> _call_paths) : id(0), call_paths(_call_paths) {
    call_paths_t cp(call_paths);
    Node *root = populate(cp);

    nf_init = std::shared_ptr<Node>(populate_init(root));
    nf_process = std::shared_ptr<Node>(populate_process(root));

    delete root;
  }

  const Node *get_init() const { return nf_init.get(); }
  const Node *get_process() const { return nf_process.get(); }
  const std::vector<call_path_t *> &get_call_paths() const {
    return call_paths;
  }

  const solver_toolbox_t &get_solver_toolbox() const { return solver_toolbox; }

  void visit(BDDVisitor &visitor) const { visitor.visit(*this); }
};

} // namespace BDD
