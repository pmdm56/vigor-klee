#pragma once

#include "./bdd-nodes.h"
#include "symbol-factory.h"

namespace BDD {

class BDD {
public:
  friend class CallPathsGroup;
  friend class Call;

private:
  uint64_t id;

  BDDNode_ptr nf_init;
  BDDNode_ptr nf_process;

  std::vector<call_path_t *> call_paths;

  static std::vector<std::string> skip_conditions_with_symbol;
  static constexpr char INIT_CONTEXT_MARKER[] = "start_time";

  // For deserialization
  BDD() { solver_toolbox.build(); }

private:
  call_t get_successful_call(std::vector<call_path_t *> call_paths) const;
  BDDNode_ptr populate(call_paths_t call_paths);

  static std::string get_fname(const Node *node);
  static bool is_skip_function(const Node *node);
  static bool is_skip_condition(const Node *node);

  BDDNode_ptr populate_init(const BDDNode_ptr &root);
  BDDNode_ptr populate_process(const BDDNode_ptr &root, bool store = false);

  void add_node(call_t call);
  void dump(int lvl, BDDNode_ptr node) const;

public:
  BDD(std::vector<call_path_t *> _call_paths) : id(0), call_paths(_call_paths) {
    solver_toolbox.build();

    call_paths_t cp(call_paths);
    auto root = populate(cp);

    nf_init = populate_init(root);
    nf_process = populate_process(root);
  }

  BDD(const BDD &bdd)
      : id(bdd.id), nf_init(bdd.nf_init), nf_process(bdd.nf_process),
        call_paths(bdd.call_paths) {}

  BDD &operator=(const BDD &) = default;

  const BDDNode_ptr &get_init() const { return nf_init; }
  BDDNode_ptr get_init() { return nf_init; }

  const BDDNode_ptr &get_process() const { return nf_process; }
  BDDNode_ptr get_process() { return nf_process; }

  void replace_process(const BDDNode_ptr &_process) { nf_process = _process; }

  uint64_t get_and_inc_id() {
    uint64_t _id = id;
    id++;
    return _id;
  }

  const std::vector<call_path_t *> &get_call_paths() const {
    return call_paths;
  }

  void visit(BDDVisitor &visitor) const { visitor.visit(*this); }

  uint64_t get_id() const { return id; }
  void set_id(uint64_t _id) { id = _id; }

  BDDNode_ptr get_node_by_id(uint64_t _id) const {
    std::vector<BDDNode_ptr> nodes{ nf_init, nf_process };
    BDDNode_ptr node;

    while (nodes.size()) {
      node = nodes[0];
      nodes.erase(nodes.begin());

      if (node->get_id() == _id) {
        return node;
      }

      if (node->get_type() == Node::NodeType::BRANCH) {
        auto branch_node = static_cast<Branch *>(node.get());

        nodes.push_back(branch_node->get_on_true());
        nodes.push_back(branch_node->get_on_false());
      } else if (node->get_next()) {
        nodes.push_back(node->get_next());
      }
    }

    return node;
  }

  BDD clone() const {
    BDD bdd = *this;

    assert(bdd.nf_init);
    assert(bdd.nf_process);

    bdd.nf_init = bdd.nf_init->clone(true);
    bdd.nf_process = bdd.nf_process->clone(true);

    return bdd;
  }

  static void serialize(const BDD &bdd, std::string file_path);
  static BDD deserialize(std::string file_path);
};

} // namespace BDD
