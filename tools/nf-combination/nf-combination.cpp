#include "llvm/Support/CommandLine.h"
#include "bdd-conflict-detect.h"

#include <list>

namespace {
llvm::cl::list<std::string> BDDFiles(llvm::cl::desc("<bdd files>"),
                                               llvm::cl::Positional);

} // namespace



/**
 * Clone a node, but without any links to other nodes
 *
 * @param node Shared pointer to a node
 * @return Shared pointer to a clone of the node
 */
BDD::BDDNode_ptr dupNode(BDD::BDDNode_ptr node){
  switch(node->get_type()){
    case BDD::Node::NodeType::BRANCH:
    {
      auto original = static_cast<BDD::Branch*>(node.get());
      auto clone = std::make_shared<BDD::Branch>(original->get_id(), original->get_condition());
      clone->set_constraints(node->get_constraints());
      return clone;
    }
    case BDD::Node::NodeType::CALL:
    {
      auto original = static_cast<BDD::Call*>(node.get());
      auto clone = std::make_shared<BDD::Call>(original->get_id(), original->get_call());
      clone->set_constraints(node->get_constraints());
      return clone;
    }
    case BDD::Node::NodeType::RETURN_PROCESS:
    {
      auto original = static_cast<BDD::ReturnProcess*>(node.get());
      auto clone = std::make_shared<BDD::ReturnProcess>(original->get_id(), original->get_return_value(), original->get_return_operation());
      clone->set_constraints(node->get_constraints());
      return clone;
    }
    default:
      assert(false && "Cannot duplicate unkown type of node.");
      break;
  }
}



/**
 * Insert <after> node after <root> node.
 *
 * @param root The node where it will be inserted the new node
 * @param after The node that will be inserted after <root>
 */
void insertAfter(BDD::BDDNode_ptr& root, BDD::BDDNode_ptr& after, bool onTrue = true){
  assert(after);
  assert(root);

  BDD::BDDNode_ptr next_root;
  if(root->get_type() == BDD::Node::NodeType::BRANCH){
    auto branch = static_cast<BDD::Branch*>(root.get());
    next_root = onTrue ? root->get_next() : branch->get_on_false();
  } else {
    next_root = root->get_next();
  }

  //root <-> after
  after->replace_prev(root);
  if(root->get_type() == BDD::Node::NodeType::BRANCH){
    auto branch = static_cast<BDD::Branch*>(root.get());
    if(onTrue) branch->replace_on_true(after); else branch->replace_on_false(after);
  } else {
    root->replace_next(after);
  }
  
  //after <-> root.next
  if(next_root){
    next_root->replace_prev(after);
    if(after->get_type() == BDD::Node::NodeType::BRANCH){
      auto branch = static_cast<BDD::Branch*>(after.get());
      branch->replace_on_true(next_root);
      branch->replace_on_false(next_root->clone(true));
    } else {
      after->replace_next(next_root);
    }
  }

}


bool node_equals(BDD::BDDNode_ptr n1, BDD::BDDNode_ptr n2){

  if(n1->get_type() != n2->get_type())
    return false;

  if(n1->get_constraints()[0].size() != n2->get_constraints()[0].size())
    return false;

  auto c_n1 = n1->get_constraints()[0].begin();
  auto c_n2 = n2->get_constraints()[0].begin();

  for(c_n1, c_n2; c_n1 != n1->get_constraints()[0].end(); c_n1++, c_n2++)
    if(!BDD::solver_toolbox.are_exprs_always_equal(*c_n1, *c_n2))
      return false;

  switch(n2->get_type()){
    case BDD::Node::NodeType::RETURN_PROCESS:
    {
      auto rp1 = static_cast<BDD::ReturnProcess*>(n1.get());
      auto rp2 = static_cast<BDD::ReturnProcess*>(n2.get());

      //global
      return (rp1->get_return_operation() == rp2->get_return_operation()) &&
              (rp1->get_return_value() == rp2->get_return_value());
    }
    case BDD::Node::NodeType::BRANCH:
    {
      auto b1 = static_cast<BDD::Branch*>(n1.get());
      auto b2 = static_cast<BDD::Branch*>(n2.get());

      //TODO: branch from packet and from data structures
      return BDD::solver_toolbox.are_exprs_always_equal(b1->get_condition(), b2->get_condition());
    }
    case BDD::Node::NodeType::CALL:
    {
      auto c1 = static_cast<BDD::Call*>(n1.get());
      auto c2 = static_cast<BDD::Call*>(n2.get());

      //global
      if(c1->get_call().function_name == "packet_borrow_next_chunk")
        return BDD::solver_toolbox.are_exprs_always_equal(
            c1->get_call().args["length"].expr,
            c2->get_call().args["length"].expr);

      //global
      if(c1->get_call().function_name == "packet_return_chunk"){
          auto out_c1 = c1->get_call().args["the_chunk"].in;
          auto out_c2 = c2->get_call().args["the_chunk"].in;
          return out_c1->getWidth() == out_c2->getWidth();
      }

      return BDD::solver_toolbox.are_calls_equal(c1->get_call(), c2->get_call()) &&
              c1->get_from() == c2->get_from();
    }
    default:
      assert(false && "Cannot compare two nodes with unkown types.");
      break;
  }
}



//return process being added before another return process
void addNode(BDD::BDDNode_ptr& root, BDD::BDDNode_ptr& new_node){

  if(node_equals(root, new_node))
    return;

  switch (root->get_type()) {
    case BDD::Node::NodeType::BRANCH: {
      
      auto branch = static_cast<BDD::Branch *>(root.get());
      std::cerr << "[Branch] prev->" << branch->get_prev() << " on_true->"
                << branch->get_on_true() << " on_false->"
                << branch->get_on_false() << std::endl;
      klee::ConstraintManager on_true_path_contrs;
      on_true_path_contrs.addConstraint(branch->get_condition());
      klee::ConstraintManager on_false_path_contrs;
      on_false_path_contrs.addConstraint(BDD::solver_toolbox.exprBuilder->Not(branch->get_condition()));

      // on true
      if (BDD::solver_toolbox.are_constraints_compatible(on_true_path_contrs,
                                                        new_node->get_constraints()[0])) {
        auto next_root = branch->get_on_true();
        if(!next_root || (next_root->get_type() == BDD::Node::NodeType::RETURN_PROCESS)){
          insertAfter(root, new_node);
        } else {
          addNode(next_root, new_node);
        }
      }

      // on false
      if (BDD::solver_toolbox.are_constraints_compatible(on_false_path_contrs,
                                                        new_node->get_constraints()[0])) {
        new_node = dupNode(new_node);
        auto next_root = branch->get_on_false();
        if(!next_root || (next_root->get_type() == BDD::Node::NodeType::RETURN_PROCESS)){
          insertAfter(root, new_node, false);
        } else {
          addNode(next_root, new_node);
        }
      }

      break;
    }
    case BDD::Node::NodeType::CALL:
    {
      auto call = static_cast<BDD::Call *>(root.get());
      std::cerr << "[Call] prev->" << call->get_prev() << " next->" << call->get_next() << std::endl;
      auto next_root = call->get_next();
      if(!next_root || (next_root->get_type() == BDD::Node::NodeType::RETURN_PROCESS)){
        insertAfter(root, new_node, false);
      } else {
        addNode(next_root, new_node);
      }

      break;
    }
    case BDD::Node::NodeType::RETURN_PROCESS:
    default:
      assert(false);
      break;
    };
}



int main(int argc, char **argv) {
  llvm::cl::ParseCommandLineOptions(argc, argv);

  std::vector<BDD::BDD> in_bdds;
  BDD::BDD out_bdd;

  BDD::solver_toolbox.build();

  assert(BDDFiles.size() >= 1 &&
         "Please provide either at least 1 bdd file");
  
  for (auto bdd : BDDFiles) {
    std::cerr << "Loading BDD: " << bdd << std::endl;
    in_bdds.push_back(BDD::BDD(bdd, in_bdds.size()));
  }


  BDD::PathExplorer e;
  std::vector<BDD::bdd_path_t *> paths_in;
  e.getPathsProcess(in_bdds[0], paths_in);

for (auto p: paths_in){
    std::cerr << "-------------------------------------------------------------------------\n";
    for (auto node : p->path) {
      
      if(out_bdd.get_process() == nullptr){
        out_bdd.add_process(node);
      } else {
        auto process_node = out_bdd.get_process();
        std::cerr << "**start add " << node << " of type " <<  node->get_type() << " **\n";
        addNode(process_node, node);
        std::cerr << "**end add**\n";
      }

    }
  }

  //inject test branch
  auto new_symbol = BDD::solver_toolbox.create_new_symbol("test", 8);
  auto inject =
      std::make_shared<BDD::Branch>(0,new_symbol);
  klee::ConstraintManager m;
  std::vector<klee::ConstraintManager> v;
  v.push_back(m);
  inject->set_constraints(v);

  auto node = dupNode(inject);
  auto process_node = out_bdd.get_process();
  addNode(process_node, node);

  std::vector<BDD::bdd_path_t *> paths_out;
  e.getPathsProcess(out_bdd, paths_out);

  for(auto p: paths_out)
    p->dump();

  auto file = std::ofstream("out_bdd.bdd");
  assert(file.is_open());

  uint64_t new_id = 0;
  out_bdd.get_process()->recursive_update_ids(new_id);



  BDD::GraphvizGenerator graphviz_generator(file);
  out_bdd.visit(graphviz_generator);

  return 0;
}