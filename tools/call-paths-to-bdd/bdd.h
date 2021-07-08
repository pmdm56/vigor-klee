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

public:
  BDD(std::vector<call_path_t *> _call_paths) : id(0), call_paths(_call_paths) {
    solver_toolbox.build();

    call_paths_t cp(call_paths);
    Node *root = populate(cp);

    nf_init = std::shared_ptr<Node>(populate_init(root));
    nf_process = std::shared_ptr<Node>(populate_process(root));

    delete root;
  }

  BDD(const BDD &bdd)
      : id(bdd.id), nf_init(bdd.nf_init), nf_process(bdd.nf_process),
        call_paths(bdd.call_paths) {}

  const Node *get_init() const { return nf_init.get(); }
  Node *get_init() { return nf_init.get(); }

  const Node *get_process() const { return nf_process.get(); }
  Node *get_process() { return nf_process.get(); }

  void replace_process(Node *_process) {
    nf_process = std::shared_ptr<Node>(_process);
  }

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

  std::shared_ptr<BDD> clone() const {
    BDD *bdd = new BDD(*this);

    auto init = bdd->nf_init.get();
    auto process = bdd->nf_process.get();

    auto init_clone = init->clone(true);
    auto process_clone = process->clone(true);

    assert(init_clone);
    assert(process_clone);

    bdd->nf_init = std::shared_ptr<Node>(init_clone);
    bdd->nf_process = std::shared_ptr<Node>(process_clone);

    return std::shared_ptr<BDD>(bdd);
  }

  void serialize(std::string out_file,
                 std::vector<std::string> call_paths_files) const {
    std::ofstream out(out_file);

    assert(out);
    assert(out.is_open());

    for (auto i = 0u; i < call_paths_files.size(); i++) {
      std::ifstream call_path_file(call_paths_files[i]);

      assert(call_path_file);
      assert(call_path_file.is_open());

      out << ";;-- Call path " << i + 1 << " -- \n";
      out << Node::process_call_path_filename(call_paths_files[i]);
      out << "\n";

      std::string line;
      while (std::getline(call_path_file, line)) {
        out << line << "\n";
      }
    }

    std::stringstream nodes_stream;
    std::stringstream edges_stream;

    std::vector<const Node *> nodes{ nf_init.get(), nf_process.get() };

    edges_stream << "[";
    nodes_stream << "[";

    while (nodes.size()) {
      auto node = nodes[0];
      nodes.erase(nodes.begin());

      nodes_stream << "\n  ";
      nodes_stream << "(";

      nodes_stream << node->get_id();
      nodes_stream << " ";

      nodes_stream << "[";

      auto filenames = node->get_call_paths_filenames();
      for (auto i = 0u; i < filenames.size(); i++) {
        auto filename = filenames[i];

        if (i != 0) {
          nodes_stream << " ";
        }

        nodes_stream << filename;
      }
      nodes_stream << "] ";

      switch (node->get_type()) {
      case Node::NodeType::CALL: {
        auto call_node = static_cast<const Call *>(node);

        nodes_stream << "CALL";
        nodes_stream << " ";
        nodes_stream << call_node->get_call().id;

        assert(node->get_next());

        edges_stream << "\n  ";
        edges_stream << "(";
        edges_stream << node->get_id();
        edges_stream << " -> ";
        edges_stream << node->get_next()->get_id();
        edges_stream << ")";

        nodes.push_back(node->get_next());
        break;
      }
      case Node::NodeType::BRANCH: {
        auto branch_node = static_cast<const Branch *>(node);

        nodes_stream << "BRANCH";

        auto discriminating_constraint =
            branch_node->get_discriminating_constraint();

        assert(!discriminating_constraint.expr.isNull());
        assert(discriminating_constraint.call_path);

        nodes_stream << " ";
        nodes_stream << Node::process_call_path_filename(
                            discriminating_constraint.call_path->file_name);

        nodes_stream << " ";
        nodes_stream << discriminating_constraint.constraint_id;

        assert(branch_node->get_on_true());
        assert(branch_node->get_on_false());

        edges_stream << "\n  ";
        edges_stream << "(";
        edges_stream << node->get_id();
        edges_stream << " -> ";
        edges_stream << branch_node->get_on_true()->get_id();
        edges_stream << " -> ";
        edges_stream << branch_node->get_on_false()->get_id();
        edges_stream << ")";

        nodes.push_back(branch_node->get_on_true());
        nodes.push_back(branch_node->get_on_false());
        break;
      }
      case Node::NodeType::RETURN_INIT: {
        auto return_init_node = static_cast<const ReturnInit *>(node);

        nodes_stream << "RETURN_INIT";
        nodes_stream << " ";
        nodes_stream << return_init_node->get_return_value();

        assert(!node->get_next());
        break;
      }
      case Node::NodeType::RETURN_PROCESS: {
        auto return_process_node = static_cast<const ReturnProcess *>(node);

        nodes_stream << "RETURN_PROCESS";
        nodes_stream << " ";

        switch (return_process_node->get_return_operation()) {
        case ReturnProcess::Operation::FWD:
          nodes_stream << "FWD";
          break;
        case ReturnProcess::Operation::DROP:
          nodes_stream << "DROP";
          break;
        case ReturnProcess::Operation::ERR:
          nodes_stream << "ERR";
          break;
        case ReturnProcess::Operation::BCAST:
          nodes_stream << "BCAST";
          break;
        }

        nodes_stream << " ";
        nodes_stream << return_process_node->get_return_value();

        assert(!node->get_next());
        break;
      }
      case Node::NodeType::RETURN_RAW: { assert(false); }
      }

      nodes_stream << ")";
    }

    nodes_stream << "\n]\n";
    edges_stream << "\n]\n";

    out << ";; -- nodes --\n";
    out << nodes_stream.str();

    out << ";; -- edges --\n";
    out << edges_stream.str();

    out << ";; -- roots --\n";
    out << "(init " << nf_init.get()->get_id() << ")\n";
    out << "(process " << nf_process.get()->get_id() << ")\n";

    out.close();
  }
};

} // namespace BDD
