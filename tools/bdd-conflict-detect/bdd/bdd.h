#pragma once

#include "nodes/node.h"
#include "symbol-factory.h"

namespace BDD {

class BDDVisitor;

class BDD {
public:
  BDD(std::vector<call_path_t *> call_paths)
      : id(0), total_call_paths(call_paths.size()) {
    solver_toolbox.build();

    call_paths_t cp(call_paths);
    auto root = populate(cp);

    nf_init = populate_init(root);
    nf_process = populate_process(root);

    rename_symbols();
    trim_constraints();
  }

  BDD(const BDD &bdd)
      : id(bdd.id), total_call_paths(bdd.total_call_paths),
        nf_init(bdd.nf_init), nf_process(bdd.nf_process) {}

  BDD(const std::string &file_path) : id(0), total_call_paths(0) {
    solver_toolbox.build();
    deserialize(file_path);
  }

  BDD(const std::string &file_path, const int _id) : total_call_paths(0) {
    solver_toolbox.build();
    deserialize(file_path);
    id = _id;
  }

  BDD &operator=(const BDD &) = default;

  uint64_t get_id() const { return id; }
  void set_id(uint64_t _id) {
    assert(_id >= id);
    id = _id;
  }

  unsigned get_total_call_paths() const { return total_call_paths; }
  unsigned get_number_of_nodes(BDDNode_ptr root) const;

  BDDNode_ptr get_init() const { return nf_init; }
  BDDNode_ptr get_process() const { return nf_process; }
  BDDNode_ptr get_node_by_id(uint64_t _id) const;

  BDD clone() const;

  void visit(BDDVisitor &visitor) const;
  void serialize(std::string file_path) const;

private:
  uint64_t id;
  unsigned total_call_paths;

  BDDNode_ptr nf_init;
  BDDNode_ptr nf_process;

  static std::vector<std::string> skip_conditions_with_symbol;

  static constexpr char INIT_CONTEXT_MARKER[] = "start_time";
  static constexpr char MAGIC_SIGNATURE[] = "===== VIGOR_BDD_SIG =====";

  // For deserialization
  BDD() : id(0), total_call_paths(0) { solver_toolbox.build(); }

  call_t get_successful_call(std::vector<call_path_t *> call_paths) const;
  BDDNode_ptr populate(call_paths_t call_paths);

  static std::string get_fname(const Node *node);
  static bool is_skip_function(const Node *node);
  static bool is_skip_condition(const Node *node);

  BDDNode_ptr populate_init(const BDDNode_ptr &root);
  BDDNode_ptr populate_process(const BDDNode_ptr &root, bool store = false);

  void add_node(call_t call);
  void dump(int lvl, BDDNode_ptr node) const;

  void rename_symbols() {
    SymbolFactory factory;

    rename_symbols(nf_init, factory);
    rename_symbols(nf_process, factory);
  }

  void trim_constraints() {
    trim_constraints(nf_init);
    trim_constraints(nf_process);
  }

  void rename_symbols(BDDNode_ptr node, SymbolFactory &factory);
  void trim_constraints(BDDNode_ptr node);

  void deserialize(const std::string &file_path);

public:
  friend class CallPathsGroup;
  friend class Call;
};

} // namespace BDD
